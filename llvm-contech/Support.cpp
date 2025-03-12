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
using namespace llvm;
using namespace std;

#include <unordered_set>

extern map<BasicBlock*, llvm_basic_block*> cfgInfoMap;

// TODO: Integrate with CTLoopInvariant
bool Contech::verifyFunctionInvariant(Function* F)
{
    for(auto bb = F->begin(); bb != F->end(); ++bb) 
    {
        BasicBlock* b = (&*bb);
        for(BasicBlock::iterator I = b->begin(); I != b->end(); ++I) 
        {
            const char* fn = NULL;
            char* fdn = NULL;
            int status = 0;
            int ret = 0;
            
            if (CallInst *ci = dyn_cast<CallInst>(&*I))
            {
                ret = ComputeFunctionName(ci, &fn, &fdn, &status);
            }
            else if (InvokeInst *ii = dyn_cast<InvokeInst>(&*I))
            {
                ret = ComputeFunctionName(ii, &fn, &fdn, &status);
            }
            else
            {
                continue;
            }
            
            if (ret != 0)
            {
                continue;
            }
            
            CONTECH_FUNCTION_TYPE funTy = classifyFunctionName(fn);
            
            if (status == 0)
            {
                free(fdn);
            }
            
            switch (funTy)
            {
                case (OMP_CALL):
                case (OMP_FORK):
                case (OMP_FOR_ITER):
                case (OMP_TASK_CALL):
                case (OMP_END):
                case (CILK_FRAME_CREATE):
                case (CILK_FRAME_DESTROY):
                    return false;
                
                default:
                    break;
            }
        }
        
    }
    
    return true;
}

//
// This is the hack version that just tries to find possible matches and eliding them
//
void Contech::crossBlockCalculation(Function* F, map<int, llvm_inst_block>& costPerBlock)
{
    hash<BasicBlock*> blockHash{};
    vector<Value*> addrSet;
    Value* cNOne = ConstantInt::get(cct.int32Ty, -1);
    map<BasicBlock*, vector<Value*> > opsInBlock;
    int crossElideCount = 0;
    bool skipF = verifyFunctionInvariant(F);
    
    
    for (auto it = F->begin(), et = F->end(); it != et; ++it)
    {
        BasicBlock* bb = &*it;
        
        auto blockInfo = cfgInfoMap[bb];
        pllvm_mem_op mem_op = blockInfo->first_op;
        
        if (skipF == true)
        {
        
#ifndef DISABLE_MEM
            while (mem_op != NULL)
            {
                // Simple deps and globals should already be handled by prior analysis
                //   Loop invariants could elide ops that follow the loop, and are worth
                //   analyzing here.
                if (mem_op->isGlobal == true ||
                    mem_op->isDep == true ||
                    mem_op->isTSC == true)
                {
                    mem_op = mem_op->next;
                    continue;
                }
                
                //
                // IF the op is already a loop elide, we don't need to find a match,
                //   but we want to preserve it in case it was a loop invariant op.
                //
                bool foundDup = false;
                if (mem_op->isLoopElide == false)
                {
                    // For each block that dominates this block,
                    //   check if its vector of ops can duplicate ops in this block.
                    for (auto bit = F->begin(), bet = F->end(); bit != bet && !foundDup; ++bit)
                    {
                        if (bit == it) break;
                        
                        
                        int64_t offset = 0;
                        auto opsEntry = opsInBlock.find(&*bit);
                        if (opsEntry == opsInBlock.end()) continue;
                        vector<Value*>* domOps = &(opsEntry->second);
                        if (domOps == NULL || domOps->size() == 0) continue;
                        if (!DT->dominates(&*(&*bit->begin()), bb)) 
                        {
                            continue;
                        }
                        
                        Value* match = findSimilarMemoryInstExt(mem_op->inst, mem_op->addr, &offset, domOps);
                        
                        if (match != NULL)
                        {
                            // Scan the block for the op, match
                            //   Then either mark it as the next crossOp or retreive its ID.
                            pllvm_mem_op matchOp = cfgInfoMap[&*bit]->first_op;
                            int matchOpID = -1;
                            while (matchOp != NULL)
                            {
                                if (matchOp->addr == match)
                                {
                                    if (matchOp->isCrossPresv)
                                        matchOpID = matchOp->crossBlockID;
                                    break;
                                }
                                matchOp = matchOp->next;
                            }
                            if (matchOp == NULL)
                            {
                                errs() << "Fail to match: " << *match << "\n";
                                errs() << "  in : " << *&*bit << "\n";
                                assert(matchOp != NULL);
                            }
                            
                            matchOp->isCrossPresv = true;
                            if (matchOpID == -1)
                            {
                                matchOpID = crossElideCount;
                                crossElideCount++;
                                matchOp->crossBlockID = matchOpID;
                            }
                            
                            mem_op->crossBlockID = matchOpID;
                            mem_op->isDep = true;
                            mem_op->isCrossPresv = true;
                            mem_op->depMemOpDelta = offset;
                            mem_op->inst->eraseFromParent();
                            
                            int bb_val = blockHash(bb);
                            auto costInfo = costPerBlock.find(bb_val);
                            
                            Instruction* sbbc = dyn_cast<Instruction>(costInfo->second.posValue);
                            
                            auto opCount = sbbc->getOperand(0);
                             
                            Instruction* posPathAdd = BinaryOperator::Create(Instruction::Add, 
                                                                             opCount, cNOne, "", sbbc);
                                
                            MarkInstAsContechInst(posPathAdd);
                            sbbc->setOperand(0, posPathAdd);
                            
                            foundDup = true;
                        }
                    }
                }
                
                if (foundDup == false)
                {
                    addrSet.push_back(mem_op->addr);
                }
                
                mem_op = mem_op->next;
            }
#endif
        }
        
        opsInBlock[bb] = addrSet;
        addrSet.clear();
        cfgInfoMap[bb]->crossOpCount = -1;
    }
    
    cfgInfoMap[&(F->getEntryBlock())]->crossOpCount = crossElideCount;
}

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
//  Determine if the global value (Constant*) has already been elided
//    If it has, return its id
//    If not, then add it to the elide constant function
//
int Contech::assignIdToGlobalElide(Constant* consGV, Module &M)
{
    GlobalValue* gv = dyn_cast<GlobalValue>(consGV);
    assert(NULL != gv);

#ifdef DISABLE_MEM
    return -1;
#endif
    
    auto id = elidedGlobalValues.find(consGV);
    if (id == elidedGlobalValues.end())
    {
        int nextId = lastAssignedElidedGVId + 1;
        
        // All Elide GV IDs must fit in 2 bytes.
        if (nextId > 0xffff) return -1;
        
        // Thread locals are not really global.  Their assembly is equivalent, but
        //   the fs: (or other approach) generates the unique address
        if (gv->isThreadLocal()) return -1;
        
        Function* f = dyn_cast<Function>(cct.writeElideGVEventsFunction.getCallee());
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
        //Function::ArgumentListType& argList = f->getArgumentList();
		auto argList = f->args();
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
    tMemOp->isCrossPresv = false;
    tMemOp->isTSC = false;
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
        
#ifndef VERIFY_LOOP_ELIDE
        if (tMemOp->isLoopElide == false)
#endif
        {
            Constant* cPos = ConstantInt::get(cct.int32Ty, memOpPos);
            Constant* cElide = ConstantInt::get(cct.int8Ty, elide);
            Constant* cPath = ConstantInt::get(cct.int8Ty, 0);
            Instruction* addrI = new BitCastInst(addr, cct.voidPtrTy, Twine("Cast as void"), li);
            MarkInstAsContechInst(addrI);

            Value* argsMO[] = {addrI, cPos, pos, cElide, cPath};
            debugLog("storeMemOpFunction @" << __LINE__);
            CallInst* smo = CallInst::Create(cct.storeMemOpFunction, ArrayRef<Value*>(argsMO, 5), "", li);
            MarkInstAsContechInst(smo);

            assert(smo != NULL);
            smo->getCalledFunction()->addFnAttr( ALWAYS_INLINE );
            tMemOp->inst = smo;
        }
    }

    return tMemOp;
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
    StructType *STy = gepit.getStructTypeOrNull();
    if (STy != NULL)
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

Value* Contech::findSimilarMemoryInstExt(Instruction* memI, Value* addr, int64_t* offset)
{
    auto p = memI->getParent();
    return findSimilarMemoryInstExtT<llvm::BasicBlock::iterator>(memI, addr, offset, p->begin(), p->end(), this);
}

Value* Contech::findSimilarMemoryInstExt(Instruction* memI, Value* addr, int64_t* offset, vector<Value*> *v)
{
    return findSimilarMemoryInstExtT<std::vector<Value*>::iterator>(memI, addr, offset, v->begin(), v->end(), this, true);
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

    //Function::ArgumentListType& argList = extFun->getArgumentList();
	auto argList = extFun->args();

    Instruction* addrI = new BitCastInst(dyn_cast<Value>(argList.begin()), argTy->getPointerTo(), Twine("Cast to Type"), soloBlock);
    MarkInstAsContechInst(addrI);

    // getElemPtr 0, 0 -> arg 0 of type*

    Value* args[2] = {ConstantInt::get(cct.int32Ty, 0), ConstantInt::get(cct.int32Ty, 0)};
    Instruction* ppid = GetElementPtrInst::Create(NULL, addrI, ArrayRef<Value*>(args, 2), "ParentIdPtr", soloBlock);
    MarkInstAsContechInst(ppid);

    Instruction* pid = new LoadInst(ppid->getType(), ppid, "ParentId", soloBlock);
    MarkInstAsContechInst(pid);

    // getElemPtr 0, 1 -> arg 1 of type*
    args[1] = ConstantInt::get(cct.int32Ty, 1);
    Instruction* parg = GetElementPtrInst::Create(NULL, addrI, ArrayRef<Value*>(args, 2), "ArgPtr", soloBlock);
    MarkInstAsContechInst(parg);

    Instruction* argP = new LoadInst(parg->getType(), parg, "Arg", soloBlock);
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

    //Function::ArgumentListType& argList = extFun->getArgumentList();
	auto argList = extFun->args();
    unsigned argListSize = extFun->arg_size();

    Value** cArgExt = new Value*[argListSize - 1];
    auto it = argList.begin();
	auto peit = it;
    for (unsigned i = 0; i < argListSize - 1; i ++)
    {
        cArgExt[i] = dyn_cast<Value>(it);
		peit = it;
        ++it;
    }

    Value* cArg[] = {dyn_cast<Value>(peit)};
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

    //Function::ArgumentListType& argList = extFun->getArgumentList();
	auto argList = extFun->args();
    unsigned argListSize = extFun->arg_size();

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
    //errs() << "F: " << *sourceValue << "\n";
    //errs() << "At: " << *insertBefore << "\n";
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

int Contech::getLineNum(Instruction* I)
{
    DILocation* dil = I->getDebugLoc();
    if (dil == NULL) 
    {
        auto SP = I->getParent()->getParent()->getSubprogram();
        if (SP != NULL) return SP->getLine();
        return 1;
    }
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
                Value* v = ci->getCalledOperand();
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
                Value* v = ci->getCalledOperand();
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

// REFACTOR: bbid is actually bb entry?
void Contech::addToLoopTrack(pllvm_loopiv_block llb, BasicBlock* bbid, Instruction* memOp, Value* addr, unsigned short* memOpPos, int64_t* memOpDelta, int* loopIVSize)
{
    int64_t multFactor = 1;
    vector<pair<Value*, int> > ac;
    auto ilte = loopInfoTrack.find(bbid);
    llvm_loop_track* llt = NULL;
    bool createEntry = false;
    
    /*if (llb->stepIV == 0)
    {
        errs() << "Step 0: " << *memOp << "\n";
        errs() << " in " << *bbid << "\n";
    }*/
    //errs() << *memOp << "\t" << llb->memIV << "\t" << *addr << "\n";
    
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
    else if (ilte->second->stepIV == 0)
    {
        ilte->second->stepIV = llb->stepIV;
        ilte->second->startIV = llb->startIV;
        ilte->second->stepBlock = llb->stepBlock;
        ilte->second->memIV = llb->memIV;
        ilte->second->exitBlocks = llb->exitBlocks;
        
        llt = ilte->second;
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
        if (ci == NULL ||
            ci->isLosslessCast() == false ||
            ci->getSrcTy()->isPointerTy() == false)
        {
            // This should only happen when the memop is a loop invariant
            *memOpPos = llt->baseAddr.size();
            llt->baseAddr.push_back(addr);
            return;
        }
        
        // errs() << "CAST: " << *ci << "\n";
        // errs() << *ci->getOperand(0) << "\n";
        gepAddr = dyn_cast<GetElementPtrInst>(ci->getOperand(0));
        
        if (gepAddr == NULL &&
            ci->getSrcTy()->isPointerTy() == true)
        {
            *memOpPos = llt->baseAddr.size();
            llt->baseAddr.push_back(addr);
            return;
        }
    }
    
    if (llb->loopInv == true)
    {
        int64_t offset = 0;
        Value* m = findSimilarMemoryInstExt(memOp, addr, &offset, &llt->baseAddr);
        
        if (m == NULL)
        {
            *memOpPos = llt->baseAddr.size();
            llt->baseAddr.push_back(addr);
        }
        else
        {
            *memOpDelta = offset;
            for (auto it = llt->baseAddr.begin(), et = llt->baseAddr.end(); it != et; ++it)
            {
                if (*it == m) break;
                *memOpPos = *memOpPos + 1;
            }
        }
        return;
    }
    
    if (gepAddr != NULL)
    {
        baseAddr = gepAddr->getPointerOperand();
        
        int64_t offset = 0;
        bool depIV = false;
        for (auto itG = gep_type_begin(gepAddr), etG = gep_type_end(gepAddr); itG != etG; ++itG)
        {
            Value* gepI = itG.getOperand();
            Instruction* nCI = dyn_cast<Instruction>(gepI);
            Value* nextIV = llb->memIV;
            multFactor = 1;
            
            //errs () << *gepI << "\t" << offset << "\t" << *itG.getIndexedType() << "\n";
            
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
                for (unsigned int i = 0; i < pn->getNumIncomingValues(); i++)
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
                    //errs() << "AS COMP\n";
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
                //errs() << "INV: " << llb->stepIV << "\n";
                tOffset += -1 * llb->stepIV;
            }
            
            // N.B. UpdateOffsetEx updates multFactor, while not using it.
            //   This order applies the factor to the IV
            *loopIVSize = updateOffsetEx(itG, multFactor, &multFactor);
            offset += updateOffsetEx(itG, tOffset, &multFactor);
            depIV = true;
        }
        
        *memOpDelta = offset;
        // Need to distinguish that the op could be inferrred and would need
        //   a non-zero size, versus the hoisted invariant addresses?
        if (depIV == false)
        {
            //errs() << "DEP " << "\t" << *memOp->getType() << "\n";
            *loopIVSize = 1;
        }
    }
    
    if (baseAddr == NULL)
    {
        errs() << "No base addr found: " << *addr << "\n";
        return;
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
            //   If the memop depends on the IV address, then 
            //   the step was a GEPI with a constant.
            PHINode* pn = dyn_cast<PHINode>(llb->memIV);
            if (pn != NULL)
            {
                for (unsigned int i = 0; i < pn->getNumIncomingValues(); i++)
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