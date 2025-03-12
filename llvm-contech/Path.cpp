#define DEBUG_TYPE "Contech"

#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/DebugLoc.h"
#define ALWAYS_INLINE (Attribute::AttrKind::AlwaysInline)

#include "llvm/Support/raw_ostream.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "llvm/Analysis/Interval.h"
#include "llvm/Analysis/LoopInfo.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/Support/CommandLine.h"

#include "BufferCheckAnalysis.h"
#include "Contech.h"

extern uint64_t tailCount;

//
// Scan through each instruction in the basic block
//   For each op used by the instruction, this instruction is 1 deeper than it
//
unsigned int Contech::getCriticalPathLen(BasicBlock& B)
{
    map<Instruction*, unsigned int> depthOfInst;
    unsigned int maxDepth = 0;

    for (BasicBlock::iterator it = B.begin(), et = B.end(); it != et; ++it)
    {
        unsigned int currIDepth = 0;
        for (User::op_iterator itUse = it->op_begin(), etUse = it->op_end();
                             itUse != etUse; ++itUse)
        {
            unsigned int tDepth;
            Instruction* inU = dyn_cast<Instruction>(itUse->get());
            map<Instruction*, unsigned int>::iterator depI = depthOfInst.find(inU);

            if (depI == depthOfInst.end())
            {
                // Dependent on value from a different basic block
                tDepth = 1;
            }
            else
            {
                // The path in this block is 1 more than the dependent instruction's depth
                tDepth = depI->second + 1;
            }

            if (tDepth > currIDepth) currIDepth = tDepth;
        }

        depthOfInst[&*it] = currIDepth;
        if (currIDepth > maxDepth) maxDepth = currIDepth;
    }

    return maxDepth;
}

void Contech::setElideInBlock(BasicBlock* bb, Instruction* stop, bool skipStoreBB = false)
{
    for (auto inst = bb->begin(); inst != bb->end(); ++inst)
    {
        if ((&*inst)->getMetadata(cct.ContechMDID) &&
            dyn_cast<CallInst>(&*inst) != NULL)
        {
            Value* cOne = ConstantInt::get(cct.int8Ty, 1);
            switch ((&*inst)->getNumOperands())
            {
                case 5: // storeBasicBlock
                    if (!skipStoreBB) {(&*inst)->setOperand(3, cOne);}
                break;
                case 6: // storeMemOp
                    if (!skipStoreBB){(&*inst)->setOperand(3, cOne);}
                    else {(&*inst)->setOperand(4, cOne);}
                break;
                case 7: // storeBasicBlockComplete
                    if (!skipStoreBB){(&*inst)->setOperand(3, cOne);}
                    else {(&*inst)->setOperand(5, cOne);}
                break;
                default:
                break;
            }
            //errs () << *inst << "\n";
        }
        if (&*inst == stop) break;
    }
}

void Contech::visitVertex(
    BasicBlock* v, 
    vector<BasicBlock*>& topologicalOrdering,
    map<BasicBlock*, unsigned char>& visited,
    map<BasicBlock*, bool> isPathTerminator)
{
    if (visited[v] == 2) return;
    if (visited[v] == 1) 
    {
        errs() << "NOT A DAG - THERE WAS A CYCLE.";
        return;
    }
    // Enter vertex v.
    visited[v] = 1;
    if (!isPathTerminator[v]) 
    {
        // Visit all the children of vertex v if it's not a terminating vertex.
        for (auto succ = succ_begin(v), end = succ_end(v); succ != end; ++succ) 
        {
            BasicBlock* w = *succ;
            visitVertex(w, topologicalOrdering, visited, isPathTerminator);
        }
    }
    // Exit vertex v.
    visited[v] = 2;
    topologicalOrdering.push_back(v);
}

//
//  ChainBufferCalls
//    This routine achieves a long standing dream of reusing some of the common
//    operations between basic blocks, such as requesting the buffer or its position.
//    The idea is to search all instrumentation and find cases where it can be
//    chained together, thus skipping the redundant calls and thereby saving operations.
//
int Contech::chainBufferCalls(Function* F, map<int, llvm_inst_block>& costPerBlock, int bbid)
{
//#define DISABLE_PATH
#ifdef DISABLE_PATH
    return bbid;
#endif
    
    hash<BasicBlock*> blockHash{};
    map<BasicBlock*, Instruction*> blockPosCall;
    map<BasicBlock*, int> blockChainCount;
    map<BasicBlock*, bool> pathTerminatorBlocks;
    map<BasicBlock*, bool> isStartChain;

    //errs() << "BASE ID: " << bbid << "\n";
    
    // For each block in the function,
    //   If the block does not have an arbitrary function call,
    //   nor a non-basic block instrumentation call (incl checking size), then 
    //   its buffer / position are valid in its successors.
    //   
    bool setDefLocF = true;
    for (auto it = F->begin(), et = F->end(); it != et; ++it)
    {
        BasicBlock* bb = &*it;
        int bb_val = blockHash(bb);
        auto costInfo = costPerBlock.find(bb_val);
        
        if (costInfo == costPerBlock.end())
        {
            errs() << "Not found: " << *bb << "\n";
        }
        
        Instruction* bbPos = dyn_cast<Instruction>(costInfo->second.posValue);
        if (bbPos == NULL) {errs() << "CBC - bbPos NULL\n"; continue;}
        if (cfgInfoMap[bb]->containCall) {/*errs() << "CBC - contain call\n";*/ continue;}
        
        if (setDefLocF)
        {
            setDefLocF = false;
            defLoc = bb->getFirstNonPHIOrDbgOrLifetime()->getDebugLoc();
        }
        
        // Verify that the position value is still valid to use in subsequent blocks
        //   Move past the Contech generated instruction.
        auto bbit = ++convertInstToIter(bbPos);
        bool noInst = true;
        for (auto bbet = bb->end(); bbit != bbet; ++bbit)
        {
            if ((&*bbit)->getMetadata(cct.ContechMDID) &&
                dyn_cast<CallInst>(&*bbit) != NULL) {noInst = false; break;}
        }
        if (!noInst) {/*errs() << "CBC - inst CT\n";*/ continue;}
        blockPosCall[bb] = bbPos;
        int count = 1;
        for (auto sbbit = succ_begin(bb), sbbet = succ_end(bb); sbbit != sbbet; ++sbbit)
        {
            BasicBlock* succ = *sbbit;
            count++;
        }
        blockChainCount[bb] = count;
    }
        
    //  
    // For each block in the function,
    //   Check every one of its predecessors, can all predecessors provide
    //   valid buffer / position values?  If so, then chain them together.
    //
    for (auto it = F->begin(), et = F->end(); it != et; ++it)
    {
        BasicBlock* bb = &*it;
        map<BasicBlock*, Instruction*> chainQueue;
        
        // Counting whether its predecessor(s) can pass valid buffer / position
        int chainCount = 0, count = 0;
        for (auto predit = pred_begin(bb), predet = pred_end(bb); predit != predet; ++predit)
        {
            BasicBlock* pred = *predit;
            
            count++;
            auto elem = blockPosCall.find(pred);
            if (elem == blockPosCall.end()) break;
            if (pred == bb) break;
            chainQueue[elem->first] = elem->second;
            chainCount++;
        }
        
        // If the counts do not match, then a predecessor cannot chain.
        //   Or the counts match, but no predecessor can chain.
        if (count != chainCount) continue;
        if (chainCount == 0) continue;
        
        // If an entry is found, then that entry must be false, 
        //   thus if end, then true
        if (pathTerminatorBlocks.find(bb) == pathTerminatorBlocks.end())
        {
            pathTerminatorBlocks[bb] = true;
        }
        isStartChain[bb] = false;
        
        // At this point, all predecessors of bb can chain to it.
        int bb_val = blockHash(bb);
        auto costInfo = costPerBlock.find(bb_val);
        Instruction* bbPos = dyn_cast<Instruction>(costInfo->second.posValue);
        
        // If there is one predecessor, then pass the values directly,
        //   otherwise, create PHI nodes to receive the different paths.
        if (chainQueue.size() == 1)
        {
            // No PHI required
            auto elem = chainQueue.begin();
            
            dyn_cast<Instruction>(bbPos->getOperand(1))->replaceAllUsesWith(elem->second);
            dyn_cast<Instruction>(bbPos->getOperand(2))->replaceAllUsesWith(elem->second->getOperand(2));
            
            // Mark the predecessor's position as used.  After all paths are chained
            //  then the basic block complete routine can skip writing the updated
            //  position to memory and instead pass the revised value instead.
            blockChainCount[elem->first] -= 1;
            if (blockChainCount[elem->first] == 1)
            {
                Value* skipStore = ConstantInt::get(cct.int8Ty, 1);
                elem->second->setOperand(4, skipStore);
                pathTerminatorBlocks[elem->first] = false;
                if (isStartChain.find(elem->first) == isStartChain.end())
                {
                    isStartChain[elem->first] = true;
                }
            }
        }
        else
        {
            Instruction* istPt = bb->getFirstNonPHI();
            PHINode* phiBuf = PHINode::Create(cct.voidPtrTy, chainQueue.size(), "", istPt);
            PHINode* phiPos = PHINode::Create(cct.int32Ty, chainQueue.size(), "", istPt);
            
            // For each predecessor, chain its buffer / position to the current block.
            for (auto predit = pred_begin(bb), predet = pred_end(bb); predit != predet; ++predit)
            {
                auto chit = chainQueue.find(*predit);
                BasicBlock* pred = chit->first;
                Instruction* predPos = chit->second;
                
                phiBuf->addIncoming(predPos->getOperand(2), pred);
                phiPos->addIncoming(predPos, pred);
                
                // Mark the predecessor's position as used.  After all paths are chained
                //  then the basic block complete routine can skip writing the updated
                //  position to memory and instead pass the revised value instead.
                blockChainCount[pred] -= 1;
                if (blockChainCount[pred] == 1)
                {
                    Value* skipStore = ConstantInt::get(cct.int8Ty, 1);
                    predPos->setOperand(4, skipStore);
                    pathTerminatorBlocks[pred] = false;
                    if (isStartChain.find(pred) == isStartChain.end())
                    {
                        isStartChain[pred] = true;
                    }
                }
            }
            
            dyn_cast<Instruction>(bbPos->getOperand(2))->replaceAllUsesWith(phiBuf);
            dyn_cast<Instruction>(bbPos->getOperand(1))->replaceAllUsesWith(phiPos);
            
            MarkInstAsContechInst(phiBuf);
            MarkInstAsContechInst(phiPos);
        }
    }

    // We are going to compute, for each starting vertex, v, the value to add to
    //   each edge in the chain starting at v.
    map<BasicBlock*, map<pair<BasicBlock*, BasicBlock*>, int>> edgeValues;

    // Additionally, we want to keep track of the number of potential paths in
    //   the chain startinga at v. We use this since we restrict the number of
    //   bits used to represent the path.
    map<BasicBlock*, int> numPossiblePaths;

    // When we are instrumenting the blocks, we will want to know the blocks in
    //   each chain.
    map<BasicBlock*, vector<BasicBlock*>> chainMembers;

    for (auto it = isStartChain.begin(), et = isStartChain.end(); 
        it != et; 
        ++it)
    {
        // Each component "chain" begins with a "start" block. Ignore the non-
        //   start blocks.
        if (!it->second) continue;
     
        // Collect the topological ordering of the basic blocks in each chain.   
        vector<BasicBlock*> topologicalOrdering;
        map<BasicBlock*, unsigned char> visited;
        auto start = it->first;

        visitVertex(start, topologicalOrdering, visited, pathTerminatorBlocks);

        //errs() << "# blocks in chain: " << topologicalOrdering.size() << "\n";

        // Count the number of paths out of each vertex, and assign values to
        //   the edges such that the sum of the edges in the path uniquely and
        //   minimally defines the path.
        map<BasicBlock*, int> numPaths;
        map<pair<BasicBlock*, BasicBlock*>, int> edgeValue;

        for (unsigned int i = 0; i < topologicalOrdering.size(); i++)
        {
            auto v = topologicalOrdering[i];

            if (pathTerminatorBlocks[v])
            {
                // Vertex was a leaf; the number of paths is 1, since at this
                //   point the path is known, so there is only 1 possible path.
                numPaths[v] = 1;
            }
            else 
            {
                numPaths[v] = 0;

                // For each edge, v -> w, assign a value to v -> w corresponding
                //   to the possible number of paths accumulated so far, and add
                //   to numPaths[v] the number of paths possible after taking 
                //   v -> w.
                for (auto succ = succ_begin(v), end = succ_end(v); 
                    succ != end; 
                    ++succ) 
                {
                    auto w = *succ;
                    edgeValue[make_pair(v, w)] = numPaths[v];
                    
                    cfgInfoMap[v]->path_ids.push_back(make_pair(cfgInfoMap[w]->id, numPaths[v]));

                    numPaths[v] += numPaths[w];
                }
                cfgInfoMap[v]->path_ids.push_back(make_pair(-1, numPaths[v]));
            }
        }

        // Save the edge values and number of possible paths out of the start.
        edgeValues[start] = edgeValue;
        numPossiblePaths[start] = numPaths[start];
        chainMembers[start] = topologicalOrdering;

        // errs() << "Found path starting at " << cfgInfoMap[start]->id << " with " <<
            // numPaths[start] << " possible paths\n";
    }

    // From the starts and terminators, we can construct the paths
    //   Then apply them here.
    for (auto it = isStartChain.begin(), et = isStartChain.end(); it != et; ++it)
    {

        if (!it->second) continue;

        //
        // Initially, assume that the start of a path is singular
        //   and that paths cannot merge.
        //
        BasicBlock* startPath = it->first;
        
        // If there are more than 1024 conservative paths, then skip this path 
        //   start.
        if (numPossiblePaths[startPath] > 1024)
        {
            errs() << "Starting at " << cfgInfoMap[startPath]->id << "\n" <<
                "generated " << numPossiblePaths[startPath] << " paths.\n";
            continue;
        }
        else if (numPossiblePaths[startPath] == 1)
        {
            continue;
        }
        else
        {
            //errs() << "Paths: " << numPossiblePaths[startPath] << "\n";
        }
        
        //errs() << "Start: " << *it->first << "\n";
        
        vector<BasicBlock*> condBranchBlocks;
        map<BasicBlock*, bool> visit;
        set<BasicBlock*> parentSet;
        deque<BasicBlock*> visitQueue;
        visitQueue.push_back(startPath);

        bool mergePath = false;
        do {
            BasicBlock* visitBlock = visitQueue.front();
            visitQueue.pop_front();
            
            if (visit.find(visitBlock) != visit.end()) continue;
            
            Instruction* ti = visitBlock->getTerminator();
            visit[visitBlock] = true;
            
            // Given a single start block, every block then should be reachable.
            //   And every predecessor needs to be on this path.
            if (isStartChain[visitBlock] == false)
            {
                for (auto predit = pred_begin(visitBlock), predet = pred_end(visitBlock); 
                     predit != predet; ++predit)
                {
                    BranchInst* bi = dyn_cast<BranchInst>((*predit)->getTerminator());
                    if (bi == NULL)
                    {
                        mergePath = true;
                        break;
                    }
                    parentSet.insert(*predit);
                }
            }
            else if (visitBlock != startPath)
            {
                mergePath = true;
                break;
            }
            
            // Now add the successors, if not at the end.
            if (pathTerminatorBlocks[visitBlock] == false)
            {
                for (auto sbbit = succ_begin(visitBlock), sbbet = succ_end(visitBlock); 
                     sbbit != sbbet; ++sbbit)
                {
                    if (visit.find(*sbbit) != visit.end()) continue;
                    visitQueue.push_back(*sbbit);
                }
            }
        } while (mergePath == false &&
                 !visitQueue.empty());

        // If a parent is not visited, then it is a start that is not part of the path.
        for (auto it = parentSet.begin(), et = parentSet.end(); it != et; ++it)
        {
            if (visit.find(*it) == visit.end())
            {
                mergePath = true;
                break;
            }
        }

        auto blocks = chainMembers[startPath];

        // Here we make sure that no block in the chain (besides the start
        //   block) has predecessors that are terminal blocks. If this were the
        //   case, We would have entered the chain elsewhere than the start, so
        //   the path ID will be incorrect. Thus, we reject these paths.
        for (int i = blocks.size() - 1; i >= 0; i--) 
        {
            auto w = blocks[i];
            if (w != startPath)
            {
                for (auto predit = pred_begin(w), predet = pred_end(w); 
                    predit != predet; 
                    ++predit)
                {
                    auto v = *predit;
                    if (pathTerminatorBlocks[v]) 
                    {
                        // Predecessor to non-start block was a termilal block.
                        /*errs() << "Path rejected due to terminal block that " <<
                            "led to non start block.\n" <<
                             cfgInfoMap[v]->id << " (terminal) -> " <<
                             cfgInfoMap[w]->id << "\n";*/
                        mergePath = true;
                        break;
                    } 
                }
            }
        }
                 
        if (mergePath == true) 
        {
            continue;
        }
        
        // Record the path ID and edge values within the path. Each branch adds 
        //   the value of the edge taken to the path ID, uniquely and minimally
        //   identifying the path taken.
        {
            llvm_path_info lpis;
            lpis.id = bbid;
            lpis.pathDepth = numPossiblePaths[startPath];//condBranchBlocks.size(); // TODO: remove this.
            lpis.condBranchBlocks = condBranchBlocks; // TODO: remove this.
            lpis.edgeValues = edgeValues[startPath];
            pathInfoMap[startPath] = lpis;
        }

        // Keep track of the path ID at each block.
        map<BasicBlock*, Value*> pathId;
        
        // Now assign the base path number, using the next "bbid"
        // TODO: base path ID
        pathId[startPath] = ConstantInt::get(cct.int32Ty, bbid); 

        bbid += numPossiblePaths[startPath];

        // We keep track of the start buffer and position to be written to at
        //   the end of the path. These will be initialized in the first
        //   iteration of the following loop, which always visits the start
        //   block first as is comes first in topological order.
        Value* startBuf = NULL;
        Value* startPos;

        // For each block in the chain, if the block is the start, set the path
        //   ID to the block ID, otherwise, create a phi node that adds the edge
        //   value of the incoming edge. The blocks in the chain are given in
        //   reverse topological order, so when we reverse the iteration, we
        //   ensure that we visit only vertices whose predecessors have been
        //   processed.
        for (int i = blocks.size() - 1; i >= 0; i--) 
        {
            auto w = blocks[i];

            if (w == startPath)
            {
                // The start path has to get treated differently. There is no
                //   phi instruction for the first block as it has no 
                //   predecessors, and we must keep track of the block ID, and
                //   the buffer.
                for (auto inst = startPath->begin(); 
                    inst != startPath->end(); 
                    ++inst)
                {
                    if ((&*inst)->getMetadata(cct.ContechMDID) &&
                        dyn_cast<CallInst>(&*inst) != NULL)
                    {
                        // N.B. Add four bytes of path position as path is
                        //   written later thus it cannot overlap.
                        Value* cThree = ConstantInt::get(cct.int32Ty, 3);
                        switch ((&*inst)->getNumOperands())
                        {
                            case 5:
                            {
                                startBuf = (&*inst)->getOperand(2);
                                startPos = (&*inst)->getOperand(1);
                                
                                // Everything thinks the block ID is three bytes,
                                //   but path IDs need four, so add one extra.
                                Value* cOne = ConstantInt::get(cct.int32Ty, 1);
                                Instruction* posPathAdd = BinaryOperator::Create(
                                    Instruction::Add,
                                    startPos,
                                    cOne,
                                    "",
                                    &*inst);
                                startPos->replaceAllUsesWith(posPathAdd);
                                posPathAdd->setOperand(0, startPos);
                                MarkInstAsContechInst(posPathAdd);
                                
                                // Remove the store BBID instruction, but shift 
                                //   the space to reserve for the path ID.
                                Instruction* posAdd = BinaryOperator::Create(
                                    Instruction::Add,
                                    posPathAdd,
                                    cThree,
                                    "", 
                                    &*inst);
                                MarkInstAsContechInst(posAdd);
                                
                                Value* argGP[] = {posAdd, startBuf};
                                Instruction* getPtr = CallInst::Create(
                                    cct.getBufPtrFunction, 
                                    ArrayRef<Value*>(argGP, 2), 
                                    "", 
                                    &*inst);
                                MarkInstAsContechInst(getPtr);
                                (&*inst)->replaceAllUsesWith(getPtr);
                                (&*inst)->eraseFromParent();
                            }
                            break;
                        }
                    }
                    if (startBuf != NULL) break;
                }
                continue;
            }

            // Get the number of predecessors.
            // TODO: there must be an easier way of doing this....
            int predCount = 0;
            for (auto predit = pred_begin(w), predet = pred_end(w); 
                predit != predet; 
                ++predit)
            {
                predCount++;
            }

            // Create a phi node for keeping track of the path ID via each
            //   incoming edge.
            PHINode* phiPathId = PHINode::Create(
                cct.int32Ty, 
                predCount, 
                "phi_for_" + to_string(cfgInfoMap[w]->id), 
                w->getFirstNonPHI());
            MarkInstAsContechInst(phiPathId);
            map<pair<BasicBlock*, BasicBlock*>, Value*> phiBranches;

            // Add a branch to the phi node for each incoming edge.
            for (auto predit = pred_begin(w), predet = pred_end(w); 
                predit != predet; 
                ++predit)
            {
                auto v = *predit;
                
                auto predecessorPathId = pathId[v];

                // Create an instruction that adds the edge value to the pathId.
                Instruction* addToPath = BinaryOperator::Create(
                    Instruction::Add, 
                    predecessorPathId,
                    ConstantInt::get(
                        cct.int32Ty, 
                        edgeValues[startPath][make_pair(v, w)]),
                    "add_to_path_" + to_string(cfgInfoMap[v]->id) + 
                        "_" + to_string(cfgInfoMap[w]->id), 
                    v->getFirstNonPHI());

                MarkInstAsContechInst(addToPath);

                phiPathId->addIncoming(addToPath, v);
            }
            
            // Set the path ID for this block.
            pathId[w] = phiPathId;

            // Mark this block as having its ID elided.
            auto costInfo = costPerBlock.find(blockHash(w));
            Instruction* posValue = dyn_cast<Instruction>(
                costInfo->second.posValue);
            setElideInBlock(w, posValue);

            // If this block is the end of a path, store the path ID to the 
            //   original block's buffer position.
            if (pathTerminatorBlocks[w])
            {
                // Write pPath into original path location
                for (auto inst = w->begin(); inst != w->end(); ++inst)
                {
                    Value* cZero = ConstantInt::get(cct.int8Ty, 0);
                    if ((&*inst)->getMetadata(cct.ContechMDID) &&
                        dyn_cast<CallInst>(&*inst) != NULL)
                    {
                        switch ((&*inst)->getNumOperands())
                        {
                            case 5:
                            {
                                // Put a getBufferPtr function in to replace
                                //   the address calculation from storeBasicBlock.
                                //   The store's position is changing to the start
                                //   of the buffer, so its return is invalid.
                                Value* tStartBuf = (&*inst)->getOperand(2);
                                Value* tStartPos = (&*inst)->getOperand(1);
                                
                                Value* argGP[] = {tStartPos, tStartBuf};
                                Instruction* getPtr = CallInst::Create(
                                    cct.getBufPtrFunction, 
                                    ArrayRef<Value*>(argGP, 2), 
                                    "", 
                                    &*inst);
                                MarkInstAsContechInst(getPtr);
                                (&*inst)->replaceAllUsesWith(getPtr);
                                
                                // Original buffer.
                                (&*inst)->setOperand(2, startBuf); 
                                // Original position.
                                (&*inst)->setOperand(1, startPos); 
                                // The path ID.
                                (&*inst)->setOperand(0, phiPathId); 
                                // Do not elide this ID.
                                (&*inst)->setOperand(3, cZero); 
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    return bbid;
}


//
// Check each predecessor for whether current block's ID can be elided
//   - All predecessors have no function calls or atomics (that require events)
//   - All predecessors have unconditional branches to current block
//
bool Contech::checkAndApplyElideId(BasicBlock* B, uint32_t bbid, map<int, llvm_inst_block>& costOfBlock)
{
#ifdef DISABLE_PATH
    return false;
#endif
    
    bool elideBasicBlockId = false;
    BasicBlock* pred;
    int predCount = 0;
    //return false; // elide toggle
	
	for (BasicBlock *pred : predecessors(B))
    {
        Instruction* ti = pred->getTerminator();
        
        // No self loops
        if (pred == B) {return false;}
        
        if (dyn_cast<BranchInst>(ti) == NULL) return false;
        if (ti->getNumSuccessors() != 1) {return false;}
        //if (ti->isSpecialTerminator()) return false;
        
        // Furthermore, Contech splits basic blocks for function calls
        //   Any tail duplication must not undo that split.
        //   Check if the terminator was introduced by Contech
        if (ti->getMetadata(cct.ContechMDID)) 
        {
            return false;
        }
        
        // Is it possible to record eliding ID?
        auto bbInfo = cfgInfoMap.find(pred);
        if (bbInfo == cfgInfoMap.end()) {return false;}
        
        if (bbInfo->second->containCall == true) return false;
        if (bbInfo->second->containAtomic == true) return false;
        predCount++;
    }
    
    if (predCount == 0) return false;
    elideBasicBlockId = true;
    //errs() << "BBID: " << bbid << " has ID elided.\n";
    
    hash<BasicBlock*> blockHash{};
        
#if LLVM_VERSION_MINOR>=9
    for (BasicBlock *pred : predecessors(B))
    {
#else
    for (pred_iterator pit = pred_begin(B), pet = pred_end(B); pit != pet; ++pit)
    {
        pred = *pit;
#endif
        auto bbInfo = cfgInfoMap.find(pred);
        bbInfo->second->next_id = (int32_t)bbid;
        
        int bb_val = blockHash(pred);
        costOfBlock[bb_val].preElide = true;
    }
    
    return elideBasicBlockId;
}

bool Contech::attemptTailDuplicate(BasicBlock* bbTail)
{
    // If this block has multiple predecessors
    //   And each predecessor has an unconditional branch
    //   Then we can duplicate this block and merge with its predecessors
    BasicBlock* pred;
    unsigned predCount = 0;
    
    // LLVM already tries to merge returns into a single basic block
    //   Don't undo this
    if (dyn_cast<ReturnInst>(bbTail->getTerminator()) != NULL) return false;
    
    // Simplication, only duplicate with a single successor.
    //   TODO: revisit successor update code to remove this assumption.
    unsigned numSucc = bbTail->getTerminator()->getNumSuccessors();
    if (numSucc > 1) return false;
    
    //
    // If this block's successor has multiple predecessors, then skip
    //   TODO: Handle this case
    if (numSucc == 1 && 
        bbTail->getUniqueSuccessor()->getUniquePredecessor() == NULL) return false;
    
    // If something requires this block's address, then it cannot be duplicated away
    //
    if (bbTail->hasAddressTaken()) return false;
    
    // Code taken from llvm::MergeBlockIntoPredecessor in BasicBlockUtils.cpp
    // Can't merge if there is PHI loop.
    for (BasicBlock::iterator BI = bbTail->begin(), BE = bbTail->end(); BI != BE; ++BI) 
    {
        if (PHINode *PN = dyn_cast<PHINode>(BI)) 
        {
            
            for (Value *IncValue : PN->incoming_values())
            {
                if (IncValue == PN) return false;
            }
        } 
        else
        {
            break;
        }
    }
    
    //
    // Go through each predecessor and verify that a tail duplicate can be merged
    //
    for (BasicBlock *pred : predecessors(bbTail))
    {
        Instruction* ti = pred->getTerminator();
        
        // No self loops
        if (pred == bbTail) return false;
        
        if (dyn_cast<BranchInst>(ti) == NULL) return false;
        if (ti->getNumSuccessors() != 1) return false;
		// TODO: was is exception, but that is gone and doc mentions special
		//   but fails to build on shark
        //if (ti->isSpecialTerminator()) return false; 
        
        // Furthermore, Contech splits basic blocks for function calls
        //   Any tail duplication must not undo that split.
        //   Check if the terminator was introduced by Contech
        if (ti->getMetadata(cct.ContechMDID)) 
        {
            return false;
        }
        predCount++;
    }
    
    if (predCount <= 1) return false;
    //return false; // tailDup toggle
    //
    // Setup new PHINodes in the successor block in preparation for the duplication.
    //
    BasicBlock* bbSucc = bbTail->getTerminator()->getSuccessor(0);
    map <unsigned, PHINode*> phiFixUp;
    unsigned instPos = 0;
    for (Instruction &II : *bbTail)
    {
        vector<Instruction*> instUsesToUpdate;
        Value* v = dyn_cast<Value>(&II);
        
        //for (User::op_iterator itUse = II.op_begin(), etUse = II.op_end(); itUse != etUse; ++itUse)
        for (auto itUse = v->user_begin(), etUse = v->user_end(); itUse != etUse; ++itUse)
        {
            Instruction* iUse = dyn_cast<Instruction>(*itUse);
            if (iUse == NULL) continue;
            
            //errs() << *iUse << "\n";
            if (iUse->getParent() == bbTail) continue;
            
            instUsesToUpdate.push_back(iUse);
        }
        
        // If the value is not used outside of this block, then it will not converge
        if (instUsesToUpdate.empty())
        {
            instPos++;
            continue;
        }
        
        // This value is used in another block
        //   Therefore, its value will converge from each duplicate
        PHINode* pn = PHINode::Create(II.getType(), 0, "", bbSucc->getFirstNonPHI());
        II.replaceUsesOutsideBlock(pn, bbTail);
        pn->addIncoming(&II, bbTail);
        phiFixUp[instPos] = pn;
        
        instPos++;
    }
    
    bool firstPred = true;
    for (pred_iterator pit = pred_begin(bbTail), pet = pred_end(bbTail); pit != pet; )
    {
        pred = *pit;
        ++pit;
        Instruction* ti = pred->getTerminator();
        
        //
        // One predecessor must be left untouched.
        //
        if (firstPred)
        {
            firstPred = false;
            continue;
        }
        
        ValueToValueMapTy VMap;
        // N.B. Clone does not update uses in new block
        //   Each instruction will still use its old def and not any new instruction.
        BasicBlock* bbAlt = CloneBasicBlock(bbTail, VMap, bbTail->getName() + "dup", bbTail->getParent(), NULL);
        if (bbAlt == NULL) return false;

        // Adapted from CloneFunctionInto:CloneFunction.cpp
        //   This fixes the instruction uses
        for (Instruction &II : *bbAlt)
        {
            RemapInstruction(&II, VMap, RF_None, NULL, NULL);
        }
        
        // Get all successors into a vector
        //vector<BasicBlock*> Succs(bbAlt->succ_begin(), bbAlt->succ_end());
        //BasicBlock* bbSucc = bbAlt->getTerminator()->getSuccessor(0);
        
        // Fix PHINode
        //  In duplicating the block, the PHINodes were destroyed.  However, there may still be
        //  a later point where the blocks converge requiring new PHINodes to be created.
        //  Probably the successor to this block.
        // However, at this point, there are only values without uses
        instPos = 0;
        for (Instruction &II : *bbAlt)
        {
            if (phiFixUp.find(instPos) == phiFixUp.end()) {instPos++; continue;}
            PHINode* pn = phiFixUp[instPos];
            pn->addIncoming(&II, bbAlt);
            instPos++;
        }
        
        //
        // Before merging, every PHINode in the successor needs to only have one incoming value.
        //   The merge utility assumes that the first value in every PHINode comes from the
        //   predecessor, which is rarely true in this case.  So new PHINodes are created.
        //
        ti->setSuccessor(0, bbAlt);
        for (auto it = bbAlt->begin(), et = bbAlt->end(); it != et; ++it)
        {
            if (PHINode* pn = dyn_cast<PHINode>(&*it))
            {
                // Remove incoming value may invalidate the iterators.
                // Instead create a new PHINode
                Value* inBlock = pn->getIncomingValueForBlock(pred);
                PHINode* pnRepl = PHINode::Create(inBlock->getType(), 1, pn->getName() + "dup", pn);
                pnRepl->addIncoming(inBlock, pred);
                pn->replaceAllUsesWith(pnRepl);
                pn->eraseFromParent();
                it = convertInstToIter(pnRepl);
            }
            else
            {
                break;
            }
        }
        
        // TODO: verify that merge will update the PHIs
        bool mergeV = MergeBlockIntoPredecessor(bbAlt);
        assert(mergeV && "Successful merge of bbAlt");
        for (auto it = bbTail->begin(), et = bbTail->end(); it != et; ++it)
        {
            if (PHINode* pn = dyn_cast<PHINode>(&*it))
            {
                int idx = pn->getBasicBlockIndex(pred);
                if (idx == -1)
                {
                    continue;
                }
                pn->removeIncomingValue(idx, false);
            }
            else
            {
                break;
            }
        }
    }
    
    pred = *(pred_begin(bbTail));
    assert(firstPred == false);
    
    bool mergeV = MergeBlockIntoPredecessor(bbTail);
    if (!mergeV) {errs() << *pred << "\n" << *bbTail << "\n";}
    assert(mergeV && "Successful merge of bbTail");
    
    for (auto it = phiFixUp.begin(), et = phiFixUp.end(); it != et; ++it)
    {
        PHINode* pn = it->second;
        if (pn->getNumIncomingValues() != predCount)
        {
            errs() << *pn << "\n";
            errs() << *(pn->getParent()) << "\n";
            assert(0);
        }
    }
    //errs() << "TD " << *pred << "\n";
    tailCount++;
    
    return true;
}
