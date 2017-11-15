#define DEBUG_TYPE "Contech"

#include "llvm/Config/llvm-config.h"
#if LLVM_VERSION_MAJOR==2
#error LLVM Version 3.8 or greater required
#else
#if LLVM_VERSION_MINOR>=8
#define NDEBUG
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
#else
#error LLVM Version 3.8 or greater required
#endif
#endif
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
using namespace llvm;
using namespace std;

#include <unordered_set>

extern map<BasicBlock*, llvm_basic_block*> cfgInfoMap;
extern uint64_t tailCount;

//
// addCheckAfterPhi
//   Adds a check buffer function call after the last Phi instruction in a basic block
//   Trying to add the check call as early as possible, but some instructions must come first
//
void Contech::addCheckAfterPhi(BasicBlock* B)
{
    if (B == NULL) return;

    for (BasicBlock::iterator I = B->begin(), E = B->end(); I != E; ++I){
        if (/*PHINode *pn = */dyn_cast<PHINode>(&*I)) {
            continue;
        }
        else if (/*LandingPadInst *lpi = */dyn_cast<LandingPadInst>(&*I)){
            continue;
        }
        else
        {
            debugLog("checkBufferFunction @" << __LINE__);
            CallInst::Create(cct.checkBufferFunction, "", convertIterToInst(I));
            return;
        }
    }
}

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

//
//  ChainBufferCalls
//    This routine achieves a long standing dream of reusing some of the common
//    operations between basic blocks, such as requsting the buffer or its position.
//    The idea is to search all instrumentation and find cases where it can be
//    chained together, thus skipping the redundant calls and thereby saving operations.
//
void Contech::chainBufferCalls(Function* F, map<int, llvm_inst_block>& costPerBlock)
{
    hash<BasicBlock*> blockHash{};
    map<BasicBlock*, Instruction*> blockPosCall;
    map<BasicBlock*, int> blockChainCount;
    
    for (auto it = F->begin(), et = F->end(); it != et; ++it)
    {
        BasicBlock* bb = &*it;
        int bb_val = blockHash(bb);
        auto costInfo = costPerBlock.find(bb_val);
        
        Instruction* bbPos = dyn_cast<Instruction>(costInfo->second.posValue);
        if (bbPos == NULL) {errs() << "CBC - bbPos NULL\n"; continue;}
        if (cfgInfoMap[bb]->containCall) {errs() << "CBC - contain call\n"; continue;}
        
        // Verify that the position value is still valid to use in subsequent blocks
        //   Move past the Contech generated instruction.
        auto bbit = ++convertInstToIter(bbPos);
        bool noInst = true;
        for (auto bbet = bb->end(); bbit != bbet; ++bbit)
        {
            if ((&*bbit)->getMetadata(cct.ContechMDID) &&
                dyn_cast<CallInst>(&*bbit) != NULL) {noInst = false; break;}
        }
        if (!noInst) {errs() << "CBC - inst CT\n"; continue;}
        blockPosCall[bb] = bbPos;
        int count = 1;
        for (auto sbbit = succ_begin(bb), sbbet = succ_end(bb); sbbit != sbbet; ++sbbit)
        {
            BasicBlock* succ = *sbbit;
            count++;
        }
        blockChainCount[bb] = count;
    }
        
    for (auto it = F->begin(), et = F->end(); it != et; ++it)
    {
        BasicBlock* bb = &*it;
        map<BasicBlock*, Instruction*> chainQueue;
        
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
        
        if (count != chainCount) continue;
        if (chainCount == 0) continue;
        
        // At this point, all predecessors of bb can chain to it.
        int bb_val = blockHash(bb);
        auto costInfo = costPerBlock.find(bb_val);
        Instruction* bbPos = dyn_cast<Instruction>(costInfo->second.posValue);
        if (chainQueue.size() == 1)
        {
            // No PHI required
            auto elem = chainQueue.begin();
            
            dyn_cast<Instruction>(bbPos->getOperand(1))->replaceAllUsesWith(elem->second);
            dyn_cast<Instruction>(bbPos->getOperand(2))->replaceAllUsesWith(elem->second->getOperand(2));
            
            blockChainCount[elem->first] -= 1;
            if (blockChainCount[elem->first] == 1)
            {
                Value* skipStore = ConstantInt::get(cct.int8Ty, 1);
                elem->second->setOperand(4, skipStore);
            }
        }
        else
        {
            Instruction* istPt = bb->getFirstNonPHI();
            PHINode* phiBuf = PHINode::Create(cct.voidPtrTy, chainQueue.size(), "", istPt);
            PHINode* phiPos = PHINode::Create(cct.int32Ty, chainQueue.size(), "", istPt);
            
            //for (auto chit = chainQueue.begin(), chet = chainQueue.end(); chit != chet; ++chit)
            for (auto predit = pred_begin(bb), predet = pred_end(bb); predit != predet; ++predit)
            {
                auto chit = chainQueue.find(*predit);
                BasicBlock* pred = chit->first;
                Instruction* predPos = chit->second;
                
                phiBuf->addIncoming(predPos->getOperand(2), pred);
                phiPos->addIncoming(predPos, pred);
                
                blockChainCount[pred] -= 1;
                if (blockChainCount[pred] == 1)
                {
                    Value* skipStore = ConstantInt::get(cct.int8Ty, 1);
                    predPos->setOperand(4, skipStore);
                }
            }
            
            dyn_cast<Instruction>(bbPos->getOperand(2))->replaceAllUsesWith(phiBuf);
            dyn_cast<Instruction>(bbPos->getOperand(1))->replaceAllUsesWith(phiPos);
        }
    }
    
    /*    int chainCount = 0, count = 0;
        for (auto sbbit = succ_begin(bb), sbbet = succ_end(bb); sbbit != sbbet; ++sbbit)
        {
            BasicBlock* succ = *sbbit;
            count++;
            if (!DT->dominates(bb, succ)) continue;
            if (succ->getSinglePredecessor() == NULL) continue;
            
            int succ_val = blockHash(succ);
            auto succInfo = costPerBlock.find(succ_val);
            Instruction* sucPos = dyn_cast<Instruction>(succInfo->second.posValue);
            if (sucPos == NULL) continue;
            
            Instruction* oldGetPos = dyn_cast<Instruction>(sucPos->getOperand(1));
            if (oldGetPos == NULL) continue;
            
            oldGetPos->replaceAllUsesWith(bbPos);
            
            Instruction* bbGetBuf = dyn_cast<Instruction>(bbPos->getOperand(2));
            if (bbGetBuf == NULL) continue;
            
            Instruction* oldGetBuf = dyn_cast<Instruction>(sucPos->getOperand(2));
            if (oldGetBuf == NULL) continue;
            oldGetBuf->replaceAllUsesWith(bbGetBuf);
            chainCount++;
            errs() << "REPLACE from " << cfgInfoMap[bb]->id << " to " << cfgInfoMap[succ]->id << "\n";
        }
        
        if (count == chainCount)
        {
            Value* skipStore = ConstantInt::get(cct.int8Ty, 1);
            bbPos->setOperand(4, skipStore);
        }
    }*/
}

//
//  Determine if the global value (Constant*) has already been elided
//    If it has, return its id
//    If not, then add it to the elide constant function
//
int Contech::assignIdToGlobalElide(Constant* consGV, Module &M)
{
    GlobalValue* gv = dyn_cast<GlobalValue>(consGV);
    assert(NULL != gv);

    auto id = elidedGlobalValues.find(consGV);
    if (id == elidedGlobalValues.end())
    {
        int nextId = lastAssignedElidedGVId + 1;
        
        // All Elide GV IDs must fit in 2 bytes.
        if (nextId > 0xffff) return -1;
        
        // Thread locals are not really global.  Their assembly is equivalent, but
        //   the fs: (or other approach) generates the unique address
        if (gv->isThreadLocal()) return -1;
        
        Function* f = dyn_cast<Function>(cct.writeElideGVEventsFunction);
        if (f == NULL) return -1;
        Instruction* iPt;
        if (f->empty())
        {
            BasicBlock* bbEntry = BasicBlock::Create(M.getContext(), "", f, NULL);
            iPt = ReturnInst::Create(M.getContext(), bbEntry);
        }
        else 
        {
            iPt = f->getEntryBlock().getFirstNonPHIOrDbgOrLifetime();
        }
         
        Constant* constID = ConstantInt::get(cct.int32Ty, nextId);
        Function::ArgumentListType& argList = f->getArgumentList();
        auto it = argList.begin();
        Instruction* addrI = new BitCastInst(consGV, cct.voidPtrTy, Twine("Cast as void"), iPt);
        Value* gvEventArgs[] = {dyn_cast<Value>(it), addrI, constID};
        CallInst* ci = CallInst::Create(cct.storeGVEventFunction, ArrayRef<Value*>(gvEventArgs, 3), "", iPt);
        
        lastAssignedElidedGVId = nextId;
        elidedGlobalValues[consGV] = nextId;
        return nextId;
    }
    
    return id->second;
}

//
//  Wrapper call that appropriately adds the operations to record the memory operation
//
pllvm_mem_op Contech::insertMemOp(Instruction* li, Value* addr, bool isWrite, unsigned int memOpPos, 
                                  Value* pos, bool elide, Module &M, map<Instruction*, int>& loopIVOp)
{
    pllvm_mem_op tMemOp = new llvm_mem_op;

    tMemOp->next = NULL;
    tMemOp->isWrite = isWrite;
    tMemOp->isDep = false;
    tMemOp->isGlobal = false;
    tMemOp->isLoopElide = false;
    tMemOp->depMemOp = 0;
    tMemOp->depMemOpDelta = 0;
    tMemOp->size = getSimpleLog(getSizeofType(addr->getType()->getPointerElementType()));

    if (tMemOp->size > 4)
    {
        errs() << "MemOp of size: " << tMemOp->size << "\n";
    }

    while (CastInst* ci = dyn_cast<CastInst>(addr))
    {
        if (ci->isLosslessCast() == true)
        {
            addr = ci->getOperand(0);
        }
        else
        {
            break;
        }
    }
    
    if (/*GlobalValue* gv = */NULL != dyn_cast<GlobalValue>(addr) &&
        NULL == dyn_cast<GetElementPtrInst>(addr))
    {
        tMemOp->isGlobal = true;
        //errs() << "Is global - " << *addr << "\n";
        
        if (li != NULL)
        {
            int elideGVId = assignIdToGlobalElide(dyn_cast<Constant>(addr), M);
            if (elideGVId != -1)
            {
                tMemOp->isDep = true;
                tMemOp->depMemOp = elideGVId;
                tMemOp->depMemOpDelta = 0;
                return tMemOp;
            }
        }
        
        // Fall through to instrumentation
    }
    else
    {
        GetElementPtrInst* gepAddr = dyn_cast<GetElementPtrInst>(addr);
        if (gepAddr != NULL &&
            NULL != dyn_cast<GlobalValue>(gepAddr->getPointerOperand()) &&
            gepAddr->hasAllConstantIndices())
        {
            tMemOp->isGlobal = true;
            if (li != NULL)
            {
                Value* taddr = gepAddr->getPointerOperand();
                
                APInt* constOffset = new APInt(64, 0, true);
                gepAddr->accumulateConstantOffset(*currentDataLayout, *constOffset);
                if (!constOffset->isSignedIntN(32)) return tMemOp;
                int64_t offset = constOffset->getSExtValue();
                //errs() << offset << "\n";
                int elideGVId = assignIdToGlobalElide(dyn_cast<Constant>(taddr), M);
                delete constOffset;
                
                if (elideGVId != -1)
                {
                    tMemOp->isDep = true;
                    tMemOp->depMemOp = elideGVId;
                    tMemOp->depMemOpDelta = offset;
                
                    return tMemOp;
                }
            }
        }
        else
        {
            tMemOp->isGlobal = false;
        }
        //errs() << "Is not global - " << *addr << "\n";
    }

    if (li != NULL)
    {
        auto livo = loopIVOp.find(li);
        if (livo != loopIVOp.end())
        {
            auto lis = LoopMemoryOps[livo->second];
            auto ilte = loopInfoTrack.find(lis->headerBlock);
            if (ilte == loopInfoTrack.end() ||
                (ilte->second->memIV == lis->memIV &&
                 ilte->second->stepIV == lis->stepIV))
            {
                tMemOp->isLoopElide = true;
                tMemOp->isDep = true;
            }
        }
        
        if (tMemOp->isLoopElide == false)
        {
            Constant* cPos = ConstantInt::get(cct.int32Ty, memOpPos);
            Constant* cElide = ConstantInt::get(cct.int8Ty, elide);
            Instruction* addrI = new BitCastInst(addr, cct.voidPtrTy, Twine("Cast as void"), li);
            MarkInstAsContechInst(addrI);

            Value* argsMO[] = {addrI, cPos, pos, cElide};
            debugLog("storeMemOpFunction @" << __LINE__);
            CallInst* smo = CallInst::Create(cct.storeMemOpFunction, ArrayRef<Value*>(argsMO, 4), "", li);
            MarkInstAsContechInst(smo);

            assert(smo != NULL);
            smo->getCalledFunction()->addFnAttr( ALWAYS_INLINE );
        }
    }

    return tMemOp;
}

//
// Check each predecessor for whether current block's ID can be elided
//   - All predecessors have no function calls or atomics (that require events)
//   - All predecessors have unconditional branches to current block
//
bool Contech::checkAndApplyElideId(BasicBlock* B, uint32_t bbid, map<int, llvm_inst_block>& costOfBlock)
{
    bool elideBasicBlockId = false;
    BasicBlock* pred;
    int predCount = 0;
    //return false; // elide toggle
#if LLVM_VERSION_MINOR>=9
    for (BasicBlock *pred : predecessors(B))
    {
#else
    for (pred_iterator pit = pred_begin(B), pet = pred_end(B); pit != pet; ++pit)
    {
        pred = *pit;
#endif
        TerminatorInst* ti = pred->getTerminator();
        
        // No self loops
        if (pred == B) {return false;}
        
        if (dyn_cast<BranchInst>(ti) == NULL) return false;
        if (ti->getNumSuccessors() != 1) {return false;}
        if (ti->isExceptional()) return false;
        
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
    errs() << "BBID: " << bbid << " has ID elided.\n";
    
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
    
// If the basic for loop is used with 3.9, the module fails with undefined symbol, unless NDEBUG matches
//    compiled value.
#if LLVM_VERSION_MINOR>=9
    for (BasicBlock *pred : predecessors(bbTail))
    {
#else
    for (pred_iterator pit = pred_begin(bbTail), pet = pred_end(bbTail); pit != pet; ++pit)
    {
        pred = *pit;
#endif
        TerminatorInst* ti = pred->getTerminator();
        
        // No self loops
        if (pred == bbTail) return false;
        
        if (dyn_cast<BranchInst>(ti) == NULL) return false;
        if (ti->getNumSuccessors() != 1) return false;
        if (ti->isExceptional()) return false;
        
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
        TerminatorInst* ti = pred->getTerminator();
        
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
        BasicBlock* bbSucc = bbAlt->getTerminator()->getSuccessor(0);
        
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

//
// convertValueToConstantEx
//
//   Given a value v, determine if it is a function of a single base value and other
//     constants.  If so, then return the base value.
//
Value* Contech::convertValueToConstantEx(Value* v, int64_t* offset, int64_t* multFactor, Value* stopInst)
{
    bool zeroOrOneConstant = true;
    
    do {
        Instruction* inst = dyn_cast<Instruction>(v);
        if (inst == NULL) break;
        if (v == stopInst) break;
        
        switch(inst->getOpcode())
        {
            case Instruction::Add:
            {
                Value* vOp[2];
                zeroOrOneConstant = false;
                for (int i = 0; i < 2; i++)
                {
                    vOp[i] = inst->getOperand(i);
                    if (ConstantInt* ci = dyn_cast<ConstantInt>(vOp[i]))
                    {
                        *offset += (*multFactor * ci->getZExtValue());
                        zeroOrOneConstant = true;
                    }
                    else
                    {
                        v = vOp[i];
                    }
                }
                
                if (zeroOrOneConstant == false)
                {
                    v = inst;
                }
            }
            break;
            case Instruction::Sub:
            {
                if (ConstantInt* ci = dyn_cast<ConstantInt>(inst->getOperand(1)))
                {
                    *offset -= (*multFactor * ci->getZExtValue());
                    zeroOrOneConstant = true;
                    v = inst->getOperand(0);
                }
                else if (ConstantInt* ci = dyn_cast<ConstantInt>(inst->getOperand(0)))
                {
                    *offset += (*multFactor * ci->getZExtValue());
                    *multFactor *= -1;
                    zeroOrOneConstant = true;
                    v = inst->getOperand(1);
                }
                else
                {
                    zeroOrOneConstant = false;
                }
            }
            break;
            case Instruction::Mul:
            {
                Value* vOp[2];
                zeroOrOneConstant = false;
                for (int i = 0; i < 2; i++)
                {
                    vOp[i] = inst->getOperand(i);
                    if (ConstantInt* ci = dyn_cast<ConstantInt>(vOp[i]))
                    {
                        *multFactor *= ci->getZExtValue();
                        zeroOrOneConstant = true;
                    }
                    else
                    {
                        v = vOp[i];
                    }
                }
                
                if (zeroOrOneConstant == false)
                {
                    v = inst;
                }
            }
            break;
            case Instruction::Shl:
            {
                if (ConstantInt* ci = dyn_cast<ConstantInt>(inst->getOperand(1)))
                {
                    *multFactor <<= ci->getZExtValue();
                    zeroOrOneConstant = true;
                    v = inst->getOperand(0);
                }
                else
                {
                    zeroOrOneConstant = false;
                }
            }
            break;
            default:
            {
                zeroOrOneConstant = false;
            }
            break;
        }
        
    } while (zeroOrOneConstant);
    
    return v;
}

//
//  updateOffsetEx
//
// GetElementPtr supports computing a constant offset; however, it requires
//   all fields to be constant.  The code is reproduced here, in order to compute
//   a partial constant offset along with the delta from the other GEP inst.
//
int64_t Contech::updateOffsetEx(gep_type_iterator gepit, int64_t val, int64_t* multFactor)
{
    int64_t offset = 0;
    if (StructType *STy = dyn_cast<StructType>(*gepit))
    {
        unsigned ElementIdx = val;
        const StructLayout *SL = currentDataLayout->getStructLayout(STy);
        offset = SL->getElementOffset(ElementIdx);
    }
    else
    {
        int64_t typeSize = currentDataLayout->getTypeAllocSize(gepit.getIndexedType());
        offset = val * typeSize;
        *multFactor *= typeSize;
    }
    
    return offset;
}

Value* Contech::castWalk(Value* v)
{
    while (CastInst* ci = dyn_cast<CastInst>(v))
    {
        if (ci->isLosslessCast() == true)
        {
            v = ci->getOperand(0);
        }
        else
        {
            break;
        }
    }
    
    return v;
}

//
// findSimilarMemoryInstEx
//
//   A key performance feature, this function will determine whether a given memory operation
//     is statically offset from another memory operation within that same basic block.  If so,
//     then the later operation's address can be omitted and computed after execution.
//   If such a pair is found, then this function will compute the static offset between the two
//     memory accesses.
//
Value* Contech::findSimilarMemoryInstExt(Instruction* memI, Value* addr, int64_t* offset)
{
    //unordered_multiset<pair<Value*,int64_t> > addrComponents;
    multimap<Value*, int64_t> addrComponents;
    int64_t tOffset = 0, baseOffset = 0;

    *offset = 0;
    addr = castWalk(addr);
    
    // Given addr, find the values that it depends on
    GetElementPtrInst* gepAddr = dyn_cast<GetElementPtrInst>(addr);
    
    while (gepAddr != NULL)
    {
        for (auto itG = gep_type_begin(gepAddr), etG = gep_type_end(gepAddr); itG != etG; ++itG)
        {
            Value* gepI = itG.getOperand();
            int64_t multFactor = 1;
            tOffset = 0;

            // If the index of GEP is a Constant, then it can vary between mem ops
            if (ConstantInt* aConst = dyn_cast<ConstantInt>(gepI))
            {
                baseOffset += updateOffsetEx(itG, aConst->getZExtValue(), &multFactor);
                continue;
            }
            else
            {
                gepI = convertValueToConstantEx(gepI, &tOffset, &multFactor, NULL);
                baseOffset += updateOffsetEx(itG, tOffset, &multFactor);
            }

            addrComponents.insert(std::pair<Value*, int64_t>(gepI, multFactor));
        }
        addr = gepAddr->getPointerOperand();
        addr = castWalk(addr);
        gepAddr = dyn_cast<GetElementPtrInst>(addr);
    }
    
    for (auto it = memI->getParent()->begin(), et = memI->getParent()->end(); it != et; ++it)
    {
        GetElementPtrInst* gepAddrT = NULL;
        // If the search has reached the current memory operation, then no match exists
        if (memI == dyn_cast<Instruction>(&*it)) break;

        Value* addrT = NULL;
        if (LoadInst *li = dyn_cast<LoadInst>(&*it))
        {
            addrT = li->getPointerOperand();
        }
        else if (StoreInst *si = dyn_cast<StoreInst>(&*it))
        {
            addrT = si->getPointerOperand();
        }

        if (addrT == NULL) continue;
        addrT = castWalk(addrT);
        gepAddrT = dyn_cast<GetElementPtrInst>(addrT);
        
        //unordered_multiset<pair<Value*,int64_t> > addrComponentsT = addrComponents;
        multimap<Value*, int64_t> addrComponentsT = addrComponents;
        tOffset = 0;
        bool unmatchedValue = false;
        
        while (gepAddrT != NULL)
        {
            addrT = gepAddrT->getPointerOperand();
            for (auto itG = gep_type_begin(gepAddrT), etG = gep_type_end(gepAddrT); itG != etG; ++itG)
            {
                Value* gepI = itG.getOperand();
                int64_t multFactor = 1;

                // If the index of GEP is a Constant, then it can vary between mem ops
                if (ConstantInt* gConst = dyn_cast<ConstantInt>(gepI))
                {
                    tOffset += updateOffsetEx(itG, gConst->getZExtValue(), &multFactor);
                    continue;
                }
                else
                {
                    int64_t sOffset = 0;
                    gepI = convertValueToConstantEx(gepI, &sOffset, &multFactor, NULL);
                    tOffset += updateOffsetEx(itG, sOffset, &multFactor);
                    
                    auto rg = addrComponentsT.equal_range(gepI);
                    if (rg.first == addrComponentsT.end())
                    {
                        unmatchedValue = true;
                        goto finish_gep_walk;
                    }
                    for (auto it = rg.first; it != rg.second; ++it)
                    {
                        if (it->second == multFactor)
                        {
                            addrComponentsT.erase(it);
                            break;
                        }
                        else
                        {
                            unmatchedValue = true;
                            goto finish_gep_walk;
                        }
                    }
                }
            }
            addrT = castWalk(addrT);
            //if (addrT == addr) { errs() << "ADDR: " << *addrT << "\t" << *addr<< "\n"; errs() << *gepAddrT << "\n"; break;}
            gepAddrT = dyn_cast<GetElementPtrInst>(addrT);
        }

finish_gep_walk:        
        if (unmatchedValue == true) continue;
        if (addrComponentsT.size() != 0) continue;
        if (addrT != addr) continue;
        
        *offset = baseOffset - tOffset;
        return &*it;
    }
    
    return NULL;
}

// OpenMP is calling ompMicroTask with a void* struct
//   Create a new routine that is invoked with a different struct that
//   will invoke the original routine with the original parameter
Function* Contech::createMicroTaskWrapStruct(Function* ompMicroTask, Type* argTy, Module &M)
{
    FunctionType* baseFunType = ompMicroTask->getFunctionType();
    Type* argTyAr[] = {cct.voidPtrTy};
    FunctionType* extFunType = FunctionType::get(ompMicroTask->getReturnType(),
                                                 ArrayRef<Type*>(argTyAr, 1),
                                                 false);

    Function* extFun = Function::Create(extFunType,
                                        ompMicroTask->getLinkage(),
                                        Twine("__ct", ompMicroTask->getName()),
                                        &M);

    BasicBlock* soloBlock = BasicBlock::Create(M.getContext(), "entry", extFun);

    Function::ArgumentListType& argList = extFun->getArgumentList();

    Instruction* addrI = new BitCastInst(dyn_cast<Value>(argList.begin()), argTy->getPointerTo(), Twine("Cast to Type"), soloBlock);
    MarkInstAsContechInst(addrI);

    // getElemPtr 0, 0 -> arg 0 of type*

    Value* args[2] = {ConstantInt::get(cct.int32Ty, 0), ConstantInt::get(cct.int32Ty, 0)};
    Instruction* ppid = createGEPI(NULL, addrI, ArrayRef<Value*>(args, 2), "ParentIdPtr", soloBlock);
    MarkInstAsContechInst(ppid);

    Instruction* pid = new LoadInst(ppid, "ParentId", soloBlock);
    MarkInstAsContechInst(pid);

    // getElemPtr 0, 1 -> arg 1 of type*
    args[1] = ConstantInt::get(cct.int32Ty, 1);
    Instruction* parg = createGEPI(NULL, addrI, ArrayRef<Value*>(args, 2), "ArgPtr", soloBlock);
    MarkInstAsContechInst(parg);

    Instruction* argP = new LoadInst(parg, "Arg", soloBlock);
    MarkInstAsContechInst(argP);

    Instruction* argV = new BitCastInst(argP, baseFunType->getParamType(0), "Cast to ArgTy", soloBlock);
    MarkInstAsContechInst(argV);

    Value* cArg[] = {pid};
    Instruction* callOTCF = CallInst::Create(cct.ompThreadCreateFunction, ArrayRef<Value*>(cArg, 1), "", soloBlock);
    MarkInstAsContechInst(callOTCF);

    Value* cArgCall[] = {argV};
    CallInst* wrappedCall = CallInst::Create(ompMicroTask, ArrayRef<Value*>(cArgCall, 1), "", soloBlock);
    MarkInstAsContechInst(wrappedCall);

    Instruction* callOTJF = CallInst::Create(cct.ompThreadJoinFunction, ArrayRef<Value*>(cArg, 1), "", soloBlock);
    MarkInstAsContechInst(callOTJF);

    Instruction* retI = NULL;
    if (ompMicroTask->getReturnType() != cct.voidTy)
        retI = ReturnInst::Create(M.getContext(), wrappedCall, soloBlock);
    else
        retI = ReturnInst::Create(M.getContext(), soloBlock);
    MarkInstAsContechInst(retI);

    return extFun;
}

Function* Contech::createMicroTaskWrap(Function* ompMicroTask, Module &M)
{
    if (ompMicroTask == NULL) {errs() << "Cannot create wrapper from NULL function\n"; return NULL;}
    FunctionType* baseFunType = ompMicroTask->getFunctionType();
    if (ompMicroTask->isVarArg()) { errs() << "Cannot create wrapper for varg function\n"; return NULL;}

    Type** argTy = new Type*[1 + baseFunType->getNumParams()];
    for (unsigned int i = 0; i < baseFunType->getNumParams(); i++)
    {
        argTy[i] = baseFunType->getParamType(i);
    }
    argTy[baseFunType->getNumParams()] = cct.int32Ty;
    FunctionType* extFunType = FunctionType::get(ompMicroTask->getReturnType(),
                                                 ArrayRef<Type*>(argTy, 1 + baseFunType->getNumParams()),
                                                 false);

    Function* extFun = Function::Create(extFunType,
                                        ompMicroTask->getLinkage(),
                                        Twine("__ct", ompMicroTask->getName()),
                                        &M);

    BasicBlock* soloBlock = BasicBlock::Create(M.getContext(), "entry", extFun);

    Function::ArgumentListType& argList = extFun->getArgumentList();
    unsigned argListSize = argList.size();

    Value** cArgExt = new Value*[argListSize - 1];
    auto it = argList.begin();
    for (unsigned i = 0; i < argListSize - 1; i ++)
    {
        cArgExt[i] = dyn_cast<Value>(it);
        ++it;
    }

    Value* cArg[] = {dyn_cast<Value>(--(argList.end()))};
    Instruction* callOTCF = CallInst::Create(cct.ompThreadCreateFunction, ArrayRef<Value*>(cArg, 1), "", soloBlock);
    MarkInstAsContechInst(callOTCF);

    CallInst* wrappedCall = CallInst::Create(ompMicroTask, ArrayRef<Value*>(cArgExt, argListSize - 1), "", soloBlock);
    MarkInstAsContechInst(wrappedCall);

    Instruction* callOTJF = CallInst::Create(cct.ompThreadJoinFunction, ArrayRef<Value*>(cArg, 1), "", soloBlock);
    MarkInstAsContechInst(callOTJF);

    Instruction* retI = NULL;
    if (ompMicroTask->getReturnType() != cct.voidTy)
        retI = ReturnInst::Create(M.getContext(), wrappedCall, soloBlock);
    else
        retI = ReturnInst::Create(M.getContext(), soloBlock);
    MarkInstAsContechInst(retI);

    delete [] argTy;
    delete [] cArgExt;

    return extFun;
}

Function* Contech::createMicroDependTaskWrap(Function* ompMicroTask, Module &M, size_t taskOffset, size_t numDep)
{
    if (ompMicroTask == NULL) {errs() << "Cannot create wrapper from NULL function\n"; return NULL;}
    FunctionType* baseFunType = ompMicroTask->getFunctionType();
    if (ompMicroTask->isVarArg()) { errs() << "Cannot create wrapper for varg function\n"; return NULL;}

    Type** argTy = new Type*[baseFunType->getNumParams()];
    for (unsigned int i = 0; i < baseFunType->getNumParams(); i++)
    {
        argTy[i] = baseFunType->getParamType(i);
    }
    FunctionType* extFunType = FunctionType::get(ompMicroTask->getReturnType(),
                                                 ArrayRef<Type*>(argTy, baseFunType->getNumParams()),
                                                 false);

    Function* extFun = Function::Create(extFunType,
                                        ompMicroTask->getLinkage(),
                                        Twine("__ct", ompMicroTask->getName()),
                                        &M);

    BasicBlock* soloBlock = BasicBlock::Create(M.getContext(), "entry", extFun);

    Function::ArgumentListType& argList = extFun->getArgumentList();
    unsigned argListSize = argList.size();

    Value** cArgExt = new Value*[argListSize];
    auto it = argList.begin();
    for (unsigned i = 0; i < argListSize; i ++)
    {
        cArgExt[i] = dyn_cast<Value>(it);
        ++it;
    }

    Constant* c1 = ConstantInt::get(cct.int32Ty, 1);
    Constant* tSize = ConstantInt::get(cct.pthreadTy, taskOffset);
    Constant* nDeps = ConstantInt::get(cct.int32Ty, numDep);
    Value* cArgs[] = {cArgExt[1], tSize, nDeps, c1};

    Instruction* callOSDF = CallInst::Create(cct.ompStoreInOutDepsFunction, ArrayRef<Value*>(cArgs, 4), "", soloBlock);
    MarkInstAsContechInst(callOSDF);

    CallInst* wrappedCall = CallInst::Create(ompMicroTask, ArrayRef<Value*>(cArgExt, argListSize), "", soloBlock);
    MarkInstAsContechInst(wrappedCall);

    Constant* c0 = ConstantInt::get(cct.int32Ty, 0);
    cArgs[3] = c0;
    callOSDF = CallInst::Create(cct.ompStoreInOutDepsFunction, ArrayRef<Value*>(cArgs, 4), "", soloBlock);
    MarkInstAsContechInst(callOSDF);

    Instruction* retI = NULL;
    if (ompMicroTask->getReturnType() != cct.voidTy)
    {
        retI = ReturnInst::Create(M.getContext(), wrappedCall, soloBlock);
    }
    else
    {
        retI = ReturnInst::Create(M.getContext(), soloBlock);
    }
    MarkInstAsContechInst(retI);

    delete [] cArgExt;

    return extFun;
}

Value* Contech::castSupport(Type* castType, Value* sourceValue, Instruction* insertBefore)
{
    if (castType == sourceValue->getType()) return sourceValue;
    auto castOp = CastInst::getCastOpcode (sourceValue, false, castType, false);
    debugLog("CastInst @" << __LINE__);
    Instruction* ret = CastInst::Create(castOp, sourceValue, castType, "Cast to Support Type", insertBefore);
    MarkInstAsContechInst(ret);
    return ret;
}

//
// findCilkStructInBlock
//
//   This routine determines whether a Contech cilk struct has been created in the given basic block
//     and therefore for that function.  If the struct is not present, then it can be inserted.  As
//     Cilk can switch which thread is executing a frame, Contech's tracking information must be placed
//     on the stack instead of in a thread-local as is used with pthreads and OpenMP.
//
Value* Contech::findCilkStructInBlock(BasicBlock& B, bool insert)
{
    Value* v = NULL;
    
    for (auto it = B.begin(), et = B.end(); it != et; ++it)
    {
        Value* iV = dyn_cast<Value>(&*it);
        if (iV == NULL) continue;

        if (iV->getName().equals("ctInitCilkStruct"))
        {
            v = iV;
            break;
        }
    }

    if (v == NULL && insert == true)
    {
        auto iPt = B.getFirstInsertionPt();

        // Call init
        debugLog("cilkInitFunction @" << __LINE__);
        Instruction* cilkInit = CallInst::Create(cct.cilkInitFunction, "ctInitCilkStruct", convertIterToInst(iPt));
        MarkInstAsContechInst(cilkInit);

        v = cilkInit;
    }

    return v;
}

GetElementPtrInst* Contech::createGEPI(Type* t, Value* v, ArrayRef<Value*> ar, const Twine& tw, BasicBlock* B)
{
    #if LLVM_VERSION_MINOR<6
    return GetElementPtrInst::Create(v, ar, tw, B);
    #else
    return GetElementPtrInst::Create(t, v, ar, tw, B);
    #endif
}

GetElementPtrInst* Contech::createGEPI(Type* t, Value* v, ArrayRef<Value*> ar, const Twine& tw, Instruction* I)
{
    return GetElementPtrInst::Create(t, v, ar, tw, I);
}

int Contech::getLineNum(Instruction* I)
{
    DILocation* dil = I->getDebugLoc();
    if (dil == NULL) return 1;
    return dil->getLine();
}

bool Contech::blockContainsFunctionName(BasicBlock* B, _CONTECH_FUNCTION_TYPE cft)
{
    for (BasicBlock::iterator I = B->begin(), E = B->end(); I != E; ++I)
    {
        Function* f = NULL;
        if (CallInst *ci = dyn_cast<CallInst>(&*I))
        {
            f = ci->getCalledFunction();
            if (f == NULL)
            {
                Value* v = ci->getCalledValue();
                f = dyn_cast<Function>(v->stripPointerCasts());
                if (f == NULL)
                {
                    continue;
                }
            }
        }
        else if (InvokeInst *ci = dyn_cast<InvokeInst>(&*I))
        {
            f = ci->getCalledFunction();
            if (f == NULL)
            {
                Value* v = ci->getCalledValue();
                f = dyn_cast<Function>(v->stripPointerCasts());
                if (f == NULL)
                {
                    continue;
                }
            }
        }
        if (f == NULL) continue;

        // call is indirect
        // TODO: add dynamic check on function called


        int status;
        const char* fmn = f->getName().data();
        char* fdn = abi::__cxa_demangle(fmn, 0, 0, &status);
        const char* fn = fdn;
        if (status != 0)
        {
            fn = fmn;
        }

        CONTECH_FUNCTION_TYPE funTy = classifyFunctionName(fn);

        if (status == 0)
        {
            free(fdn);
        }

        if (funTy == cft)
        {
            return true;
        }
    }
    return false;
}

// collect the state of whether a block is a loop exits
// and record the corresponding loop pointer if it is an exit
void Contech::collectLoopExits(Function* fblock, map<int, Loop*>& loopExits,
                               LoopInfo* LI)
{
    hash<BasicBlock*> blockHash{};
    for (Function::iterator B = fblock->begin(); B != fblock->end(); ++B) 
    {
        BasicBlock &bb = *B;
        int bb_val = blockHash(&bb);
        // get the loop it belongs to
        Loop* motherLoop = LI->getLoopFor(&bb);
        if (motherLoop != nullptr && motherLoop->isLoopExiting(&bb)) 
        {
            // the loop exits and is exit
            loopExits[bb_val] = motherLoop;
        }
    }

}

// collect the state information of which loop a basic block belongs to
void Contech::collectLoopBelong(Function* fblock, map<int, Loop*>& loopBelong,
                                LoopInfo* LI)
{
    hash<BasicBlock*> blockHash{};
    for (Function::iterator B = fblock->begin(); B != fblock->end(); ++B) 
    {
        BasicBlock* bb = &*B;
        Loop* motherLoop = LI->getLoopFor(bb);
        if (motherLoop != nullptr) 
        {
            // the loop exists
            loopBelong[blockHash(bb)] = motherLoop;
        }
    }
}

// see if a block is an entry to a loop
Loop* Contech::isLoopEntry(BasicBlock* bb, unordered_set<Loop*>& lps)
{
    for (Loop* lp : lps) 
    {
        if (bb == (*lp->block_begin())) 
        {
            return lp;
        }
    }

    return nullptr;
}


// collect the state information of whether a basic block is an entry
// to the loop
unordered_map<Loop*, int> Contech::collectLoopEntry(Function* fblock,
                                                    LoopInfo* LI)
{
    // first collect all loops inside a function
    unordered_map<Loop*, int> loopEntry{};
    hash<BasicBlock*> blockHash{};
    unordered_set<Loop*> allLoops{};
    for (Function::iterator bb = fblock->begin(); bb != fblock->end(); ++bb) 
    {
        BasicBlock* bptr = &*bb;
        Loop* motherLoop = LI->getLoopFor(bptr);
        if (motherLoop != nullptr) 
        {
            allLoops.insert(motherLoop);
        }
    }

    // then iterate through all blocks to collect information
    for (Function::iterator B = fblock->begin(); B != fblock->end(); ++B) 
    {
        if (B != fblock->end()) 
        {
            BasicBlock* bb = &*B;
            for (auto NB = succ_begin(bb); NB != succ_end(bb); ++NB) 
            {
                BasicBlock* next_bb = *NB;
                Loop* entryLoop = isLoopEntry(next_bb, allLoops);
                if (entryLoop != nullptr) 
                {
                    int next_bb_val = blockHash(next_bb);
                    loopEntry[entryLoop] = next_bb_val;
                    break;
                }
            }
        }
    }

    return move(loopEntry);
}

void Contech::addToLoopTrack(pllvm_loopiv_block llb, BasicBlock* bbid, Instruction* memOp, Value* addr, unsigned short* memOpPos, int64_t* memOpDelta, int* loopIVSize)
{
    int64_t multFactor = 1;
    vector<pair<Value*, int> > ac;
    auto ilte = loopInfoTrack.find(bbid);
    llvm_loop_track* llt = NULL;
    bool createEntry = false;
    if (ilte == loopInfoTrack.end())
    {
        llt = new llvm_loop_track;
        llt->loopUsed = true;
        llt->stepIV = llb->stepIV;
        llt->startIV = llb->startIV;
        llt->stepBlock = llb->stepBlock;
        llt->memIV = llb->memIV;
        llt->exitBlocks = llb->exitBlocks;
        loopInfoTrack[bbid] = llt;
        createEntry = true;
    }
    else
    {
        llt = ilte->second;
        //assert(llt->startIV == llb->startIV);
        assert(llt->memIV == llb->memIV);
    }
    
    *loopIVSize = 1;
    Value* baseAddr = NULL;
    GetElementPtrInst* gepAddr = dyn_cast<GetElementPtrInst>(addr);
    
    if (gepAddr == NULL)
    {
        CastInst* ci = dyn_cast<CastInst>(addr);
        if (ci == NULL) return;
        if (ci->isLosslessCast() == false) return;
        if (ci->getSrcTy()->isPointerTy() == false) return;
        
        //errs() << "CAST: " << *ci << "\n";
        //errs() << *ci->getOperand(0) << "\n";
        gepAddr = dyn_cast<GetElementPtrInst>(ci->getOperand(0));
    }
    
    if (gepAddr != NULL)
    {
        baseAddr = gepAddr->getPointerOperand();
        
        int64_t offset = 0;
        bool isIVLast = false;
        for (auto itG = gep_type_begin(gepAddr), etG = gep_type_end(gepAddr); itG != etG; ++itG)
        {
            Value* gepI = itG.getOperand();
            Instruction* nCI = dyn_cast<Instruction>(gepI);
            Value* nextIV = llb->memIV;
            multFactor = 1;
            
            // If the index of GEP is a Constant, then it can vary between mem ops
            if (ConstantInt* aConst = dyn_cast<ConstantInt>(gepI))
            {
                offset += updateOffsetEx(itG, aConst->getZExtValue(), &multFactor);
                continue;
            }
            
            // The main question in this code is applying offset(s) that
            //   will either trace to the PHI, or will cross the block
            //   where event lib will apply step first.  The add walking code
            //   will try to cross the step instruction, but it should stop
            //   there if the step would already have been applied, which is
            //   if that step instruction dominates the mem op and occurs in
            //   a different block.  Same block delays the event lib step.
            PHINode* pn = dyn_cast<PHINode>(nextIV);
            if (pn != NULL)
            {
                for (int i = 0; i < pn->getNumIncomingValues(); i++)
                {
                    Value* inCV = pn->getIncomingValue(i);
                    Instruction* inCVInst = dyn_cast<Instruction>(inCV);
                    
                    // If this is not the start value, then it is the step value.
                    //   If this value dominates the load or store
                    if (inCV != llb->startIV &&
                        (inCVInst == NULL ||
                         (DT->dominates(inCVInst, memOp) == true &&
                          inCVInst->getParent() != memOp->getParent())))
                    {
                        nextIV = inCV;
                    }
                }
            }
            
            int64_t tOffset = 0;
            if (nCI != llb->memIV)
            {
                // memIV is always a PHI, but the item used may be an offset
                //   of the IV, or even the next step of the IV so we tell 
                //   the conversion not to capture this step.
                
                // Only this path needs to update multFactor, otherwise the PHI
                //   is directly used by the GEPI
                gepI = convertValueToConstantEx(gepI, &tOffset, &multFactor, nextIV);
                
                // If this element is really an additional component, it is not the IV
                if (std::find(llb->addtComponents.begin(), 
                              llb->addtComponents.end(), gepI) != llb->addtComponents.end())
                {
                    int64_t scale = updateOffsetEx(itG, 1, &multFactor);
                    offset += scale * tOffset;
 
                    //llt->compMap[gepI] = scale;
                    ac.push_back(make_pair(gepI, scale));
                    continue;
                }
            }
            
            // If the convert did not end on the step instruction and
            // If the step block dominates us here then it will
            //   be applied before reaching here, while the IR is not
            //   applying it.
            Instruction* nextIVInst = dyn_cast<Instruction>(nextIV);
            if (gepI != nextIV &&
                llb->stepBlock != memOp->getParent() &&
                nextIVInst != NULL &&
                nextIVInst->getParent() == llb->stepBlock &&
                DT->dominates(nextIVInst, memOp))
            {
                tOffset += -1 * llb->stepIV;
            }
            
            // N.B. UpdateOffsetEx updates multFactor, while not using it.
            //   This order applies the factor to the IV
            *loopIVSize = updateOffsetEx(itG, multFactor, &multFactor);
            offset += updateOffsetEx(itG, tOffset, &multFactor);
            
            isIVLast = true;
        }
        
        *memOpDelta = offset;
    }
    
    if (baseAddr == NULL)
    {
        errs() << "No base addr found: " << *addr << "\n";
        assert(0);
    }
    
    // Find the base address, does another loop op share it ?
    *memOpPos = 0;
    if (ac.size() == 0)
    {
        for (auto it = llt->baseAddr.begin(), et = llt->baseAddr.end(); it != et; ++it)
        {
            if (*it == baseAddr) break;
            *memOpPos = *memOpPos + 1;
        }
    }
    else
    {
        *memOpPos = llt->baseAddr.size();
    }
    
    if (*memOpPos == llt->baseAddr.size())
    {
        if (baseAddr == llb->memIV) 
        {
            llt->baseAddr.push_back(NULL);
            if (createEntry == true)
            {
                loopInfoTrack[bbid]->stepIV *= getSizeofType(gepAddr->getPointerOperand()->getType()->getPointerElementType());
            }
            
            // N.B. This code is duplicated from above.
            //   If the memop depends on the IV address, then it was
            //   the step was a GEPI with a constant.
            PHINode* pn = dyn_cast<PHINode>(llb->memIV);
            if (pn != NULL)
            {
                for (int i = 0; i < pn->getNumIncomingValues(); i++)
                {
                    Value* inCV = pn->getIncomingValue(i);
                    Instruction* inCVInst = dyn_cast<Instruction>(inCV);
                    
                    // If this is not the start value, then it is the step value.
                    //   If this value dominates the load or store
                    if (inCV != llb->startIV &&
                        (inCVInst == NULL ||
                         (DT->dominates(inCVInst, memOp) == true &&
                          inCVInst->getParent() != memOp->getParent())))
                    {
                        *memOpDelta += -1 * loopInfoTrack[bbid]->stepIV;
                    }
                }
            }
        }
        else 
        {
            llt->baseAddr.push_back(baseAddr);
        }
        if (ac.size() > 0) {llt->compMap[*memOpPos] = ac;}
    }
}