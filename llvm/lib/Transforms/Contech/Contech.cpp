//===- Contech.cpp - Based on Example code from "Writing an LLVM Pass" ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "Contech"

#include "llvm/Config/config.h"
#if LLVM_VERSION_MAJOR==2
#error LLVM Version 3.2 or greater required
#else
#if LLVM_VERSION_MINOR>=3
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
#if LLVM_VERSION_MINOR>=5
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/DebugLoc.h"
#else
#include "llvm/DebugInfo.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Support/GetElementPtrTypeIterator.h"
#include "llvm/Support/DebugLoc.h"
#endif
#define ALWAYS_INLINE (Attribute::AttrKind::AlwaysInline)
#else
#include "llvm/Constants.h"
#include "llvm/DataLayout.h"
#include "llvm/Instructions.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Type.h"
#include "llvm/Module.h"
#include "llvm/LLVMContext.h"
#include "llvm/Metadata.h"
#include "llvm/Function.h"
#include "llvm/Attributes.h"
#include "llvm/Analysis/DebugInfo.h"
#define ALWAYS_INLINE (Attributes::AttrVal::AlwaysInline)
#include "llvm/Support/InstIterator.h"
#include "llvm/Support/GetElementPtrTypeIterator.h"
#include "llvm/Support/DebugLoc.h"
#endif
#endif
#include "llvm/Support/raw_ostream.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Instrumentation.h"

#include <map>
#include <set>
#include <vector>
#include <iostream>
#include <fstream>
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/CommandLine.h"
#include <cxxabi.h>

#include "Contech.h"
using namespace llvm;
using namespace std;

map<BasicBlock*, llvm_basic_block*> cfgInfoMap;
cl::opt<string> ContechStateFilename("ContechState", cl::desc("File with current Contech state"), cl::value_desc("filename"));
cl::opt<bool> ContechMarkFrontend("ContechMarkFE", cl::desc("Generate a minimal marked output"));
cl::opt<bool> ContechMinimal("ContechMinimal", cl::desc("Generate a minimally instrumented output"));

namespace llvm {
#define STORE_AND_LEN(x) x, sizeof(x)
#define FUNCTIONS_INSTRUMENT_SIZE 53
// NB Order matters in this array.  Put the most specific function names first, then 
//  the more general matches.
    llvm_function_map functionsInstrument[FUNCTIONS_INSTRUMENT_SIZE] = {
                                            // If main has OpenMP regions, the derived functions
                                            //    will begin with main or MAIN__
                                           {STORE_AND_LEN("main\0"), MAIN},
                                           {STORE_AND_LEN("MAIN__\0"), MAIN},
                                           {STORE_AND_LEN("pthread_create"), THREAD_CREATE},
                                           {STORE_AND_LEN("pthread_join"), THREAD_JOIN},
                                           {STORE_AND_LEN("parsec_barrier_wait"), BARRIER_WAIT},
                                           {STORE_AND_LEN("pthread_barrier_wait"), BARRIER_WAIT},
                                           {STORE_AND_LEN("parsec_barrier"), BARRIER},
                                           {STORE_AND_LEN("pthread_barrier"), BARRIER},
                                           {STORE_AND_LEN("malloc"), MALLOC},
                                           {STORE_AND_LEN("xmalloc"), MALLOC},
                                           {STORE_AND_LEN("valloc"), MALLOC},
                                           {STORE_AND_LEN("memalign"), MALLOC2},
                                           {STORE_AND_LEN("operator new"), MALLOC},
                                           {STORE_AND_LEN("mmap"), MALLOC2},
                                           {STORE_AND_LEN("realloc"), REALLOC},
                                           // Splash2.raytrace has a free_rayinfo, so \0 added
                                           {STORE_AND_LEN("free\0"), FREE},
                                           {STORE_AND_LEN("operator delete"), FREE},
                                           {STORE_AND_LEN("munmap"), FREE},
                                           {STORE_AND_LEN("exit"), EXIT},
                                           {STORE_AND_LEN("pthread_mutex_lock"), SYNC_ACQUIRE},
                                           {STORE_AND_LEN("pthread_mutex_trylock"), SYNC_ACQUIRE},
                                           {STORE_AND_LEN("pthread_mutex_unlock"), SYNC_RELEASE},
                                           {STORE_AND_LEN("pthread_spin_lock"), SYNC_ACQUIRE},
                                           {STORE_AND_LEN("pthread_spin_trylock"), SYNC_ACQUIRE},
                                           {STORE_AND_LEN("pthread_spin_unlock"), SYNC_RELEASE},
                                           {STORE_AND_LEN("_mutex_lock_"), SYNC_ACQUIRE},
                                           {STORE_AND_LEN("_mutex_unlock_"), SYNC_RELEASE},
                                           {STORE_AND_LEN("pthread_cond_wait"), COND_WAIT},
                                           {STORE_AND_LEN("pthread_cond_signal"), COND_SIGNAL},
                                           {STORE_AND_LEN("pthread_cond_broadcast"), COND_SIGNAL},
                                           {STORE_AND_LEN("GOMP_parallel_start"), OMP_CALL},
                                           {STORE_AND_LEN("GOMP_parallel_end"), OMP_END},
                                           {STORE_AND_LEN("GOMP_atomic_start"), GLOBAL_SYNC_ACQUIRE},
                                           {STORE_AND_LEN("__kmpc_single"), GLOBAL_SYNC_ACQUIRE},
                                           {STORE_AND_LEN("GOMP_atomic_end"), GLOBAL_SYNC_RELEASE},
                                           {STORE_AND_LEN("__kmpc_end_single"), GLOBAL_SYNC_RELEASE},
                                           {STORE_AND_LEN("__kmpc_fork_call"), OMP_FORK},
                                           {STORE_AND_LEN("__kmpc_dispatch_next"), OMP_FOR_ITER},
                                           {STORE_AND_LEN("__kmpc_barrier"), OMP_BARRIER},
                                           {STORE_AND_LEN("__kmpc_cancel_barrier"), OMP_BARRIER},
                                           {STORE_AND_LEN("__kmpc_omp_task_with_deps"), OMP_TASK_CALL},
                                           {STORE_AND_LEN("MPI_Send"), MPI_SEND_BLOCKING},
                                           {STORE_AND_LEN("mpi_send_"), MPI_SEND_BLOCKING},
                                           {STORE_AND_LEN("MPI_Recv"), MPI_RECV_BLOCKING},
                                           {STORE_AND_LEN("mpi_recv_"), MPI_RECV_BLOCKING},
                                           {STORE_AND_LEN("MPI_Isend"), MPI_SEND_NONBLOCKING},
                                           {STORE_AND_LEN("mpi_isend_"), MPI_SEND_NONBLOCKING},
                                           {STORE_AND_LEN("MPI_Irecv"), MPI_RECV_NONBLOCKING},
                                           {STORE_AND_LEN("mpi_irecv_"), MPI_RECV_NONBLOCKING},
                                           {STORE_AND_LEN("MPI_Barrier"), BARRIER_WAIT},
                                           {STORE_AND_LEN("mpi_barrier_"), BARRIER_WAIT},
                                           {STORE_AND_LEN("MPI_Wait"), MPI_TRANSFER_WAIT},
                                           {STORE_AND_LEN("mpi_wait_"), MPI_TRANSFER_WAIT}};

    
    ModulePass* createContechPass() { return new Contech(); }
}    
    //
    // Create any globals required for this module
    //
    bool Contech::doInitialization(Module &M)
    {
        // Function types are named fun(Return type)(arg1 ... argN)Ty
        FunctionType* funVoidPtrI32I32VoidPtrTy;
        FunctionType* funVoidVoidPtrI32VoidPtrTy;
        FunctionType* funVoidVoidPtrI32I32I64Ty;
        FunctionType* funVoidPtrVoidPtrTy;
        FunctionType* funVoidPtrVoidTy;
        FunctionType* funVoidVoidTy;
        FunctionType* funVoidI8I64VoidPtrTy;
        FunctionType* funVoidI64VoidPtrVoidPtrTy;
        FunctionType* funVoidI8Ty;
        FunctionType* funVoidI32Ty;
        FunctionType* funVoidI8VoidPtrI64Ty;
        FunctionType* funVoidVoidPtrI32Ty;
        FunctionType* funVoidI64I64Ty;
        FunctionType* funVoidI8I8I32I32I32I32VoidPtrI64VoidPtrTy;
        FunctionType* funI32I32Ty;
        FunctionType* funI32VoidPtrTy;
        FunctionType* funI32I32I32VoidPtrTy;
        FunctionType* funVoidVoidPtrI64Ty;

        LLVMContext &ctx = M.getContext();
        #if LLVM_VERSION_MINOR>=5
        currentDataLayout = M.getDataLayout();
        #else
        currentDataLayout = new DataLayout(&M);
        #endif
        cct.int8Ty = Type::getInt8Ty(ctx);
        cct.int32Ty = Type::getInt32Ty(ctx);
        cct.int64Ty = Type::getInt64Ty(ctx);
        cct.voidTy = Type::getVoidTy(ctx);
        cct.voidPtrTy = cct.int8Ty->getPointerTo();

        Type* funVoidPtrVoidTypes[] = {cct.voidPtrTy};
        funVoidPtrVoidPtrTy = FunctionType::get(cct.voidPtrTy, ArrayRef<Type*>(funVoidPtrVoidTypes, 1), false);
        cct.threadInitFunction = M.getOrInsertFunction("__ctInitThread", funVoidPtrVoidPtrTy);
        
        funI32VoidPtrTy = FunctionType::get(cct.int32Ty, ArrayRef<Type*>(funVoidPtrVoidTypes, 1), false);
        cct.getBufPosFunction = M.getOrInsertFunction("__ctGetBufferPos",funI32VoidPtrTy);
        
        Type* argsBB[] = {cct.int32Ty, cct.int32Ty, cct.voidPtrTy};
        funVoidPtrI32I32VoidPtrTy = FunctionType::get(cct.voidPtrTy, ArrayRef<Type*>(argsBB, 3), false);
        cct.storeBasicBlockFunction = M.getOrInsertFunction("__ctStoreBasicBlock", funVoidPtrI32I32VoidPtrTy);
        
        funI32I32I32VoidPtrTy = FunctionType::get(cct.int32Ty, ArrayRef<Type*>(argsBB, 3), false);
        cct.storeBasicBlockCompFunction = M.getOrInsertFunction("__ctStoreBasicBlockComplete", funI32I32I32VoidPtrTy);
        
        Type* argsMO[] = {cct.voidPtrTy, cct.int32Ty, cct.voidPtrTy};
        funVoidVoidPtrI32VoidPtrTy = FunctionType::get(cct.voidTy, ArrayRef<Type*>(argsMO, 3), false);
        cct.storeMemOpFunction = M.getOrInsertFunction("__ctStoreMemOp", funVoidVoidPtrI32VoidPtrTy);
        
        funVoidPtrVoidTy = FunctionType::get(cct.voidPtrTy, false);
        cct.getBufFunction = M.getOrInsertFunction("__ctGetBuffer",funVoidPtrVoidTy);
        
        // void (void) functions:
        funVoidVoidTy = FunctionType::get(cct.voidTy, false);
        cct.allocateBufferFunction = M.getOrInsertFunction("__ctAllocateLocalBuffer", funVoidVoidTy);
        cct.storeMemReadMarkFunction = M.getOrInsertFunction("__ctStoreMemReadMark", funVoidVoidTy);
        cct.storeMemWriteMarkFunction = M.getOrInsertFunction("__ctStoreMemWriteMark", funVoidVoidTy);
        cct.ompPushParentFunction = M.getOrInsertFunction("__ctOMPPushParent", funVoidVoidTy);
        cct.ompPopParentFunction = M.getOrInsertFunction("__ctOMPPopParent", funVoidVoidTy);
        cct.ompProcessJoinFunction =  M.getOrInsertFunction("__ctOMPProcessJoinStack", funVoidVoidTy);
        
        cct.allocateCTidFunction = M.getOrInsertFunction("__ctAllocateCTid", FunctionType::get(cct.int32Ty, false));
        cct.getThreadNumFunction = M.getOrInsertFunction("__ctGetLocalNumber", FunctionType::get(cct.int32Ty, false));
        cct.getCurrentTickFunction = M.getOrInsertFunction("__ctGetCurrentTick", FunctionType::get(cct.int64Ty, false));
        
        cct.ctPeekParentIdFunction = M.getOrInsertFunction("__ctPeekParent", FunctionType::get(cct.int32Ty, false));
        cct.ompGetNestLevelFunction = M.getOrInsertFunction("omp_get_level", FunctionType::get(cct.int32Ty, false));
        
        Type* argsSSync[] = {cct.voidPtrTy, cct.int32Ty/*type*/, cct.int32Ty/*retVal*/, cct.int64Ty /*ct_tsc_t*/};
        funVoidVoidPtrI32I32I64Ty = FunctionType::get(cct.voidTy, ArrayRef<Type*>(argsSSync, 4), false);
        cct.storeSyncFunction = M.getOrInsertFunction("__ctStoreSync", funVoidVoidPtrI32I32I64Ty);
        
        Type* argsTC[] = {cct.int32Ty};
        
        // TODO: See how one might flag a function as having the attribute of "does not return", for exit()
        funVoidI32Ty = FunctionType::get(cct.voidTy, ArrayRef<Type*>(argsTC, 1), false);
        cct.storeBasicBlockMarkFunction = M.getOrInsertFunction("__ctStoreBasicBlockMark", funVoidI32Ty);
        cct.pthreadExitFunction = M.getOrInsertFunction("pthread_exit", funVoidI32Ty);
        cct.ompThreadCreateFunction = M.getOrInsertFunction("__ctOMPThreadCreate", funVoidI32Ty);
        cct.ompThreadJoinFunction = M.getOrInsertFunction("__ctOMPThreadJoin", funVoidI32Ty);
        cct.ompTaskCreateFunction = M.getOrInsertFunction("__ctOMPTaskCreate", funVoidI32Ty);
        cct.checkBufferFunction = M.getOrInsertFunction("__ctCheckBufferSize", funVoidI32Ty);
        cct.checkBufferLargeFunction = M.getOrInsertFunction("__ctCheckBufferBySize", funVoidI32Ty);
        
        funI32I32Ty = FunctionType::get(cct.int32Ty, ArrayRef<Type*>(argsTC, 1), false);
        cct.ompGetParentFunction = M.getOrInsertFunction("omp_get_ancestor_thread_num", funI32I32Ty);
       
        Type* argsQB[] = {cct.int8Ty};
        funVoidI8Ty = FunctionType::get(cct.voidTy, ArrayRef<Type*>(argsQB, 1), false);
        cct.queueBufferFunction = M.getOrInsertFunction("__ctQueueBuffer", funVoidI8Ty);
        cct.ompTaskJoinFunction = M.getOrInsertFunction("__ctOMPTaskJoin", funVoidVoidTy);
        
        Type* argsSB[] = {cct.int8Ty, cct.voidPtrTy, cct.int64Ty};
        funVoidI8VoidPtrI64Ty = FunctionType::get(cct.voidTy, ArrayRef<Type*>(argsSB, 3), false);
        cct.storeBarrierFunction = M.getOrInsertFunction("__ctStoreBarrier", funVoidI8VoidPtrI64Ty);
        
        Type* argsATI[] = {cct.voidPtrTy, cct.int32Ty};
        funVoidVoidPtrI32Ty = FunctionType::get(cct.voidTy, ArrayRef<Type*>(argsATI, 2), false);
        cct.storeThreadInfoFunction = M.getOrInsertFunction("__ctAddThreadInfo", funVoidVoidPtrI32Ty);
        
        Type* argsSMPIXF[] = {cct.int8Ty, cct.int8Ty, cct.int32Ty, cct.int32Ty, cct.int32Ty, cct.int32Ty, cct.voidPtrTy, cct.int64Ty, cct.voidPtrTy};
        funVoidI8I8I32I32I32I32VoidPtrI64VoidPtrTy = FunctionType::get(cct.voidTy, ArrayRef<Type*>(argsSMPIXF, 9), false);
        cct.storeMPITransferFunction = M.getOrInsertFunction("__ctStoreMPITransfer", funVoidI8I8I32I32I32I32VoidPtrI64VoidPtrTy);
        
        Type* argsMPIW[] = {cct.voidPtrTy, cct.int64Ty};
        funVoidVoidPtrI64Ty = FunctionType::get(cct.voidTy, ArrayRef<Type*>(argsMPIW, 2), false);
        cct.storeMPIWaitFunction = M.getOrInsertFunction("__ctStoreMPIWait", funVoidVoidPtrI64Ty);
        
        // This needs to be machine type here
        //
        #if LLVM_VERSION_MINOR>=5
        // LLVM 3.5 removed module::getPointerSize() -> DataLayout::getPointerSize()
        //   Let's find malloc instead, which takes size_t and size_t is the size we need
        //auto mFunc = M.getFunction("malloc");
        //FunctionType* mFuncTy = mFunc->getFunctionType();
        //pthreadTy = mFuncTy->getParamType(0);
        if (currentDataLayout->getPointerSize() == llvm::Module::Pointer64)
        {
            cct.pthreadTy = cct.int64Ty;
            cct.pthreadSize = 8;
        }
        else
        {
            cct.pthreadTy = cct.int32Ty;
            cct.pthreadSize = 4;
        }
        #else
        if (M.getPointerSize() == llvm::Module::Pointer64)
        {
            cct.pthreadTy = cct.int64Ty;
            cct.pthreadSize = 8;
        }
        else
        {
            cct.pthreadTy = cct.int32Ty;
            cct.pthreadSize = 4;
        }
        #endif
        
        Type* argsSTJ[] = {cct.pthreadTy, cct.int64Ty};
        funVoidI64I64Ty = FunctionType::get(cct.voidTy, ArrayRef<Type*>(argsSTJ, 2), false);
        cct.storeThreadJoinFunction = M.getOrInsertFunction("__ctStoreThreadJoin", funVoidI64I64Ty);
        
        Type* argsME[] = {cct.int8Ty, cct.pthreadTy, cct.voidPtrTy};
        funVoidI8I64VoidPtrTy = FunctionType::get(cct.voidTy, ArrayRef<Type*>(argsME, 3), false);
        cct.storeMemoryEventFunction = M.getOrInsertFunction("__ctStoreMemoryEvent", funVoidI8I64VoidPtrTy);
        Type* argsBulkMem[] = {cct.pthreadTy, cct.voidPtrTy, cct.voidPtrTy};
        funVoidI64VoidPtrVoidPtrTy = FunctionType::get(cct.voidTy, ArrayRef<Type*>(argsBulkMem, 3), false);
        cct.storeBulkMemoryOpFunction = M.getOrInsertFunction("__ctStoreBulkMemoryEvent", funVoidI64VoidPtrVoidPtrTy);
        
        Type* argsOMPSD[] = {cct.voidPtrTy, cct.pthreadTy, cct.int32Ty, cct.int32Ty};
        FunctionType* funVoidVoidPtrI64I32I32 = FunctionType::get(cct.voidTy, ArrayRef<Type*>(argsOMPSD, 4), false);
        cct.ompStoreInOutDepsFunction = M.getOrInsertFunction("__ctOMPStoreInOutDeps", funVoidVoidPtrI64I32I32);
        cct.ompPrepareTaskFunction = NULL;
        
        Type* pthreadTyPtr = cct.pthreadTy->getPointerTo();
        Type* argsCTA[] = {pthreadTyPtr, 
                           cct.voidPtrTy, 
                           static_cast<Type *>(funVoidPtrVoidPtrTy)->getPointerTo(),
                           cct.voidPtrTy};
        FunctionType* funIntPthreadPtrVoidPtrVoidPtrFunVoidPtr = FunctionType::get(cct.int32Ty, ArrayRef<Type*>(argsCTA,4), false);
        cct.createThreadActualFunction = M.getOrInsertFunction("__ctThreadCreateActual", funIntPthreadPtrVoidPtrVoidPtrFunVoidPtr);
		
        cct.ContechMDID = ctx.getMDKindID("ContechInst");
        
        return true;
    }
    
    _CONTECH_FUNCTION_TYPE Contech::classifyFunctionName(const char* fn)
    {
        for (unsigned int i = 0; i < FUNCTIONS_INSTRUMENT_SIZE; i++)
        {
            if (strncmp(fn, functionsInstrument[i].func_name, functionsInstrument[i].str_len - 1) == 0)
            {
                return functionsInstrument[i].typeID;
            }
        }
    
        return NONE;
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
                CallInst::Create(cct.checkBufferFunction, "", I);
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
    //  Wrapper call that appropriately adds the operations to record the memory operation
    //
    pllvm_mem_op Contech::insertMemOp(Instruction* li, Value* addr, bool isWrite, unsigned int memOpPos, Value* pos)
    {
        pllvm_mem_op tMemOp = new llvm_mem_op;
        
        tMemOp->addr = NULL;
        tMemOp->next = NULL;
        tMemOp->isWrite = isWrite;
        tMemOp->isDep = false;
        tMemOp->size = getSimpleLog(getSizeofType(addr->getType()->getPointerElementType()));
        
        if (tMemOp->size > 4)
        {
            errs() << "MemOp of size: " << tMemOp->size << "\n";
        }
        
        if (/*GlobalValue* gv = */NULL != dyn_cast<GlobalValue>(addr))
        {
            tMemOp->isGlobal = true;
            //errs() << "Is global - " << *addr << "\n";
            //return tMemOp; // HACK!
        }
        else
        {
            tMemOp->isGlobal = false;
            //errs() << "Is not global - " << *addr << "\n";
        }
        
        if (li != NULL)
        {
            Constant* cPos = ConstantInt::get(cct.int32Ty, memOpPos);
            Instruction* addrI = new BitCastInst(addr, cct.voidPtrTy, Twine("Cast as void"), li);
            MarkInstAsContechInst(addrI);
            
            Value* argsMO[] = {addrI, cPos, pos};
            debugLog("storeMemOpFunction @" << __LINE__);
            CallInst* smo = CallInst::Create(cct.storeMemOpFunction, ArrayRef<Value*>(argsMO, 3), "", li);
            MarkInstAsContechInst(smo);
            
            assert(smo != NULL);
            smo->getCalledFunction()->addFnAttr( ALWAYS_INLINE );
        }
        
        return tMemOp;
    }
    
    Value* Contech::findSimilarMemoryInst(Instruction* memI, Value* addr, int* offset)
    {
        vector<Value*> addrComponents;
        Instruction* addrI = dyn_cast<Instruction>(addr);
        int tOffset = 0, baseOffset = 0;
        
        *offset = 0;
        
        //return NULL;
        
        if (addrI == NULL)
        {
            // No instruction generates the address, it is probably a global value
            for (auto it = memI->getParent()->begin(), et = memI->getParent()->end(); it != et; ++it)
            {
                if (memI == dyn_cast<Instruction>(&*it)) break;
                if (LoadInst *li = dyn_cast<LoadInst>(&*it))
                {
                    Value* addrT = li->getPointerOperand();
                    
                    if (addrT == addr) return li;
                }
                else if (StoreInst *si = dyn_cast<StoreInst>(&*it))
                {
                    Value* addrT = si->getPointerOperand();
                    
                    if (addrT == addr) return si;
                }
            }

            return NULL;
        }
        else if (addrI->getParent() != memI->getParent())
        {
            // Address is computed in a different basic block
            return NULL;
        }
        else if (CastInst* bci = dyn_cast<CastInst>(addr))
        {
            for (auto it = memI->getParent()->begin(), et = memI->getParent()->end(); it != et; ++it)
            {
                if (memI == dyn_cast<Instruction>(&*it)) break;
                if (LoadInst *li = dyn_cast<LoadInst>(&*it))
                {
                    Value* addrT = li->getPointerOperand();
                    
                    if (addrT == addr) return li;
                }
                else if (StoreInst *si = dyn_cast<StoreInst>(&*it))
                {
                    Value* addrT = si->getPointerOperand();
                    
                    if (addrT == addr) return si;
                }
            }

            return NULL;
        }
        
        // Given addr, find the values that it depends on
        GetElementPtrInst* gepAddr = dyn_cast<GetElementPtrInst>(addr);
        if (gepAddr == NULL)
        {
            //errs() << *addr << "\n";
            //errs() << "Mem instruction did not come from GEP\n";
            return NULL;
        }
        
        for (auto itG = gep_type_begin(gepAddr), etG = gep_type_end(gepAddr); itG != etG; ++itG)
        {
            Value* gepI = itG.getOperand();
            
            // If the index of GEP is a Constant, then it can vary between mem ops
            if (ConstantInt* aConst = dyn_cast<ConstantInt>(gepI))
            {
                if (StructType *STy = dyn_cast<StructType>(*itG)) 
                {
                    unsigned ElementIdx = aConst->getZExtValue();
                    const StructLayout *SL = currentDataLayout->getStructLayout(STy);
                    baseOffset += SL->getElementOffset(ElementIdx);
                }
                else
                {
                    baseOffset += aConst->getZExtValue() * currentDataLayout->getTypeAllocSize(itG.getIndexedType());
                }
            }
            else
            {
                // Reset to 0 if there is a variable offset
                baseOffset = 0;
            }
            
            // TODO: if the value is a constant global, then it matters
            addrComponents.push_back(gepI);
        }
        
        for (auto it = memI->getParent()->begin(), et = memI->getParent()->end(); it != et; ++it)
        {
            GetElementPtrInst* gepAddrT = NULL;
            if (memI == dyn_cast<Instruction>(&*it)) break;
            
            if (LoadInst *li = dyn_cast<LoadInst>(&*it))
            {
                Value* addrT = li->getPointerOperand();
                
                gepAddrT = dyn_cast<GetElementPtrInst>(addrT);
            }
            else if (StoreInst *si = dyn_cast<StoreInst>(&*it))
            {
                Value* addrT = si->getPointerOperand();
                
                gepAddrT = dyn_cast<GetElementPtrInst>(addrT);
            }
            
            if (gepAddrT == NULL) continue;
            if (gepAddrT == gepAddr)
            {
                *offset = 0;
                return &*it;
            }
            
            unsigned int i = 0;
            bool constMode = false;
            if (gepAddrT->getNumIndices() != gepAddr->getNumIndices()) continue;
            if (gepAddrT->getPointerOperand() != gepAddr->getPointerOperand()) continue;
            tOffset = 0;
            for (auto itG = gep_type_begin(gepAddrT), etG = gep_type_end(gepAddrT); itG != etG; i++, ++itG)
            {
                Value* gepI = itG.getOperand();
                
                // If the index of GEP is a Constant, then it can vary between mem ops
                if (ConstantInt* gConst = dyn_cast<ConstantInt>(gepI)) 
                {
                    if (ConstantInt* aConst = dyn_cast<ConstantInt>(addrComponents[i])) 
                    {
                        if (aConst->getSExtValue() != gConst->getSExtValue()) constMode = true;
                        
                        //
                        // GetElementPtr supports computing a constant offset; however, it requires
                        //   all fields to be constant.  The code is reproduced here, in order to compute
                        //   a partial constant offset along with the delta from the other GEP inst.
                        //
                        if (StructType *STy = dyn_cast<StructType>(*itG)) 
                        {
                            unsigned ElementIdx = gConst->getZExtValue();
                            const StructLayout *SL = currentDataLayout->getStructLayout(STy);
                            tOffset += SL->getElementOffset(ElementIdx);
                            continue;
                        }
                        
                        tOffset += gConst->getZExtValue() * currentDataLayout->getTypeAllocSize(itG.getIndexedType());
                        
                        continue;
                    }
                    else
                    {
                        break;
                    }
                }
                else if (constMode == true)
                {
                    // Only can handle cases where a varying constant is at the end of the calculation
                    //   or has non-varying constants around it.
                    break;
                }
                
                // temp offset remains 0 until a final sequence of constant int offsets
                tOffset = 0;
                if (gepI != addrComponents[i]) break;
            }
            
            if (i == addrComponents.size()) 
            {
                //errs() << *gepAddrT << " ?=? " << *gepAddr << "\t" << baseOffset << "\t" << tOffset << "\n";
                *offset = baseOffset - tOffset;
                return &*it;
            }
        }
        
        return NULL;
    }
    
    void Contech::internalAddAllocate(BasicBlock& B)
    {
        debugLog("allocateBufferFunction @" << __LINE__);
        CallInst::Create(cct.allocateBufferFunction, "", B.begin());
    }
    
    //
    // Go through the module to get the basic blocks
    //
    bool Contech::runOnModule(Module &M)
    {
        unsigned int bb_count = 0;
        int length = 0;
        char* buffer = NULL;
        doInitialization(M);
        
        ifstream* icontechStateFile = new ifstream(ContechStateFilename.c_str(), ios_base::in | ios_base::binary);
        if (icontechStateFile != NULL && icontechStateFile->good())
        {
            icontechStateFile->seekg(0, icontechStateFile->end);
            length = icontechStateFile->tellg();
            icontechStateFile->seekg(0, icontechStateFile->beg);
            buffer = new char[length];
            icontechStateFile->read(buffer, length);
            bb_count = *(unsigned int*)buffer;
        }
        else
        {
            //errs() << contechStateFile->rdstate() << "\t" << contechStateFile->fail() << "\n";
            //errs() << ContechStateFilename.c_str() << "\n";
        }
        
        // for unknown reasons, a file that does not exist needs to clear all bits and not
        // just eof for writing
        icontechStateFile->close();
        delete icontechStateFile;
        
        for (Module::iterator F = M.begin(), FE = M.end(); F != FE; ++F) {
            int status;
            const char* fmn = F->getName().data();
            char* fn = abi::__cxa_demangle(fmn, 0, 0, &status);
            bool inMain = false;

            // If status is 0, then we demangled the name
            if (status != 0)
            {
                // fmn is original name string
            }
            else
            {
                fmn = fn;
            }
            
            // Replace Main with a different main, if Contech is inserting the runtime
            //   and associated instrumentation
            if (__ctStrCmp(fmn, "main\0") == 0)
            {
                // Only rename main if this is not the marker front end
                if (ContechMarkFrontend == false)
                {
                    // This invalidates F->getName(), ie possibly fmn is invalid
                    //F->setName(Twine("ct_orig_main"));
                    inMain = true;
                }
            }
            // Add other functions that Contech should not instrument here
            // NB Main is checked above and is special cased
            else if (classifyFunctionName(fmn) != NONE)
            {
                errs() << "SKIP: " << fmn << "\n";
                if (fmn == fn)
                {
                    free(fn);
                }
                continue;
            }
            // If this function is one Contech adds when instrumenting for OpenMP
            // then it can be skipped
            else if (contechAddedFunctions.find(&*F) != contechAddedFunctions.end())
            {
                errs() << "SKIP: " << fmn << "\n";
                if (fmn == fn)
                {
                    free(fn);
                }
                continue;
            }
            
            if (F->size() == 0)
            {
                if (fmn == fn)
                {
                    free(fn);
                }
                continue;
            }
            errs() << fmn << "\n";
        
            // "Normalize" every basic block to have only one function call in it
            for (Function::iterator B = F->begin(), BE = F->end(); B != BE; ) {
                BasicBlock &pB = *B;
                CallInst *ci;
                int status = 0;

                if (internalSplitOnCall(pB, &ci, &status) == false)
                {
                    B++;
                    #ifdef SPLIT_DEBUG
                    if (ci != NULL)
                        errs() << status << "\t" << *ci << "\n";
                    #endif
                }
                else {
                    
                }
            }
        
            // Now instrument each basic block in the function
            for (Function::iterator B = F->begin(), BE = F->end(); B != BE; ++B) {
                BasicBlock &pB = *B;
                
                internalRunOnBasicBlock(pB, M, bb_count, ContechMarkFrontend, fmn);
                bb_count++;
            }
            
            // If fmn is fn, then it was allocated by the demangle routine and we are required to free
            if (fmn == fn)
            {
                free(fn);
            }
            
            // Renaming invalidates the current name of the function
            if (inMain == true)
            {
                F->setName(Twine("ct_orig_main"));
            }
        }
        
        if (ContechMarkFrontend == true) goto cleanup;
        
cleanup:     
        ofstream* contechStateFile = new ofstream(ContechStateFilename.c_str(), ios_base::out | ios_base::binary);
        //contechStateFile->seekp(0, ios_base::beg);
        if (buffer == NULL)
        {
            // New state file starts with the basic block count
            contechStateFile->write((char*)&bb_count, sizeof(unsigned int));
        }
        else
        {
            // Write the existing data back out
            //   First, put a new basic block count at the start of the existing data
            *(unsigned int*) buffer = bb_count;
            contechStateFile->write(buffer, length);
        }
        //contechStateFile->seekp(0, ios_base::end);
        
        if (ContechMarkFrontend == false && ContechMinimal == false)
        {
            int wcount = 0;
            unsigned char evTy = ct_event_basic_block_info;
            for (map<BasicBlock*, llvm_basic_block*>::iterator bi = cfgInfoMap.begin(), bie = cfgInfoMap.end(); bi != bie; ++bi)
            {
                pllvm_mem_op t = bi->second->first_op;
                
                // Write out basic block info events
                //   Then runtime can directly pass the events to the event list
                contechStateFile->write((char*)&evTy, sizeof(unsigned char));
                contechStateFile->write((char*)&bi->second->id, sizeof(unsigned int));
                // This is the flags field, which is currently 0 or 1 for containing a call
                unsigned int flags = ((unsigned int)bi->second->containCall) | 
                                     ((unsigned int)bi->second->containGlobalAccess << 1);
                contechStateFile->write((char*)&flags, sizeof(unsigned int));
                contechStateFile->write((char*)&bi->second->lineNum, sizeof(unsigned int));
                contechStateFile->write((char*)&bi->second->numIROps, sizeof(unsigned int));
                contechStateFile->write((char*)&bi->second->critPathLen, sizeof(unsigned int));
                
                int strLen = bi->second->fnName.length();//(bi->second->fnName != NULL)?strlen(bi->second->fnName):0;
                contechStateFile->write((char*)&strLen, sizeof(int));
                *contechStateFile << bi->second->fnName;
                //contechStateFile->write(bi->second->fnName, strLen * sizeof(char));
                
                
                strLen = (bi->second->fileName != NULL)?strlen(bi->second->fileName):0;
                contechStateFile->write((char*)&strLen, sizeof(int));
                contechStateFile->write(bi->second->fileName, strLen * sizeof(char));
                
                strLen = bi->second->callFnName.length();
                contechStateFile->write((char*)&strLen, sizeof(int));
                *contechStateFile << bi->second->callFnName;
                
                // Number of memory operations
                contechStateFile->write((char*)&bi->second->len, sizeof(unsigned int));
                
                while (t != NULL)
                {
                    pllvm_mem_op tn = t->next;
                    char memFlags = (t->isDep)?BBI_FLAG_MEM_DUP:0x0;
                    memFlags |= (t->isWrite)?0x1:0x0;
                    
                    contechStateFile->write((char*)&memFlags, sizeof(char));
                    contechStateFile->write((char*)&t->size, sizeof(char));
                    // Add optional dep mem op info
                    if (t->isDep )
                    {
                        assert((memFlags & BBI_FLAG_MEM_DUP) == BBI_FLAG_MEM_DUP);
                        contechStateFile->write((char*)&t->depMemOp, sizeof(unsigned short));
                        contechStateFile->write((char*)&t->depMemOpDelta, sizeof(int));
                    }
                    delete (t);
                    t = tn;
                }
                wcount++;
                free(bi->second);
            }
        }
        //errs() << "Wrote: " << wcount << " basic blocks\n";
        cfgInfoMap.clear();
        contechStateFile->close();
        delete contechStateFile;
        
        return true;
    }
    
    // returns size in bytes
    unsigned int Contech::getSizeofType(Type* t)
    {
        unsigned int r = t->getPrimitiveSizeInBits();
        if (r > 0) return r / 8;
        else if (t->isPointerTy()) { return 8;}
        else if (t->isVectorTy()) { return t->getVectorNumElements() * t->getScalarSizeInBits();}
        else if (t->isPtrOrPtrVectorTy()) { errs() << *t << " is pointer vector\n";}
        else if (t->isArrayTy()) { errs() << *t << " is array\n";}
        else if (t->isStructTy()) { errs() << *t << " is struct\n";}
        
        // DataLayout::getStructLayout(StructType*)->getSizeInBytes()
        StructType* st = dyn_cast<StructType>(t);
        if (st == NULL)
        {
            errs() << "Failed get size - " << *t << "\n";
            return 0;
        }
        auto stLayout = currentDataLayout->getStructLayout(st);
        if (stLayout == NULL)
        {
            errs() << "Failed get size - " << *t << "\n";
        }
        else
        {
            return stLayout->getSizeInBytes();
        }
        
        return 0;
    }    
    
    // base 2 log of a value
    unsigned int Contech::getSimpleLog(unsigned int i)
    {
        if (i > 128) {return 8;}
        if (i > 64) { return 7;}
        if (i > 32) { return 6;}
        if (i > 16) { return 5;}
        if (i > 8) { return 4;}
        if (i > 4) { return 3;}
        if (i > 2) { return 2;}
        if (i > 1) { return 1;}
        return 0;
    }

    bool Contech::internalSplitOnCall(BasicBlock &B, CallInst** tci, int* st)
    {
        *tci = NULL;
        for (BasicBlock::iterator I = B.begin(), E = B.end(); I != E; ++I)
        {
            // As InvokeInst are already terminator instructions, we do not have to find them here
            if (CallInst *ci = dyn_cast<CallInst>(&*I)) 
            {
                *tci = ci;
                if (ci->isTerminator()) {*st = 1; return false;}
                if (ci->doesNotReturn()) {*st = 2; return false;}
                Function* f = ci->getCalledFunction();
                
                //
                // If F == NULL, then f is indirect
                //   O.w. this function may just be an annotation and can be ignored
                //
                if (f != NULL)
                {
                    const char* fn = f->getName().data();
                    if (0 == __ctStrCmp(fn, "llvm.dbg") ||
                        0 == __ctStrCmp(fn, "llvm.lifetime")) 
                    {
                        *st = 3;
                        continue;
                    }
                
                }
                
                I++;
                
                // At this point, the call instruction returns and was not the last in the block
                //   If the next instruction is a terminating, unconditional branch then splitting
                //   is redundant.  (as splits create a terminating, unconditional branch)
                if (I->isTerminator())
                {
                	if (BranchInst *bi = dyn_cast<BranchInst>(&*I))
                	{
                		if (bi->isUnconditional()) {*st = 4; return false;}
                	}
                    else if (/* ReturnInst *ri = */ dyn_cast<ReturnInst>(&*I))
                    {
                        *st = 5;
                        return false;
                    }
                }
                B.splitBasicBlock(I, "");
                return true;
            }
        }
    
        return false;
    }
    
    //
    // For each basic block
    //
    bool Contech::internalRunOnBasicBlock(BasicBlock &B,  Module &M, int bbid, const bool markOnly, const char* fnName)
    {
        Instruction* iPt = B.getTerminator();
        std::vector<pllvm_mem_op> opsInBlock;
        unsigned int memOpCount = 0;
        Instruction* aPhi = B.begin();
        bool getNextI = false;
        bool containQueueBuf = false;
        bool hasUninstCall = false;
        bool containKeyCall = false;
        Value* posValue = NULL;
        Value* basePosValue = NULL;
        Value* baseBufValue = NULL;
        unsigned int lineNum = 0, numIROps = B.size();
        
        vector<Instruction*> delayedAtomicInsts;
        map<Instruction*, Value*> dupMemOps;
        map<Instruction*, int> dupMemOpOff;
        map<Value*, unsigned short> dupMemOpPos;
        
        //errs() << "BB: " << bbid << "\n";
        debugLog("Enter BBID: " << bbid);
        
        for (BasicBlock::iterator I = B.begin(), E = B.end(); I != E; ++I)
        {
            MDNode *N;
            if (lineNum == 0 && (N = I->getMetadata("dbg"))) 
            {
                DILocation Loc(N);
                
                lineNum = Loc.getLineNumber();
            }
        
            // TODO: Use BasicBlock->getFirstNonPHIOrDbgOrLifetime as insertion point
            //   compare with getFirstInsertionPt
            if (/*PHINode *pn = */dyn_cast<PHINode>(&*I)) 
            {
                getNextI = true;
                numIROps --;
                continue;
            }
            else if (/*LandingPadInst *lpi = */dyn_cast<LandingPadInst>(&*I))
            {
                getNextI = true;
                numIROps --;
                continue;
            }
            else if (I->getMetadata(cct.ContechMDID) )
            {
                // This instruction was already added by the instrumentation skip!
                getNextI = true;
                numIROps --;
                continue;
            }
            else if (LoadInst *li = dyn_cast<LoadInst>(&*I))
            {
                int addrOffset = 0;
                Value* addrSimilar = findSimilarMemoryInst(li, li->getPointerOperand(), &addrOffset);
                
                if (addrSimilar != NULL)
                {
                    //errs() << *addrSimilar << " ?=? " << *li << "\t" << addrOffset << "\n";
                    dupMemOps[li] = addrSimilar;
                    dupMemOpOff[li] = addrOffset;
                    dupMemOpPos[addrSimilar] = 0;
                }
                else
                {
                    memOpCount ++;
                }
            }
            else if (StoreInst *si = dyn_cast<StoreInst>(&*I)) 
            {
                int addrOffset = 0;
                Value* addrSimilar = findSimilarMemoryInst(si, si->getPointerOperand(), &addrOffset);
                
                if (addrSimilar != NULL)
                {
                    //errs() << *addrSimilar << " ?=? " << *si << "\t" << addrOffset << "\n";
                    dupMemOps[si] = addrSimilar;
                    dupMemOpOff[si] = addrOffset;
                    dupMemOpPos[addrSimilar] = 0;
                }
                else
                {
                    memOpCount ++;
                }
            }
            else if (ContechMinimal == true)
            {
                if (CallInst* ci = dyn_cast<CallInst>(&*I)) 
                {
                    Function *f = ci->getCalledFunction();
                    
                    // call is indirect
                    // TODO: add dynamic check on function called
                    if (f == NULL) { continue; }

                    int status;
                    const char* fmn = f->getName().data();
                    char* fdn = abi::__cxa_demangle(fmn, 0, 0, &status);
                    const char* fn = fdn;
                    if (status != 0) 
                    {
                        fn = fmn;
                    }
                    
                    CONTECH_FUNCTION_TYPE tID = classifyFunctionName(fn);
                    if (tID == EXIT || // We need to replace exit otherwise the trace is corrupt
                        tID == SYNC_ACQUIRE ||
                        tID == SYNC_RELEASE ||
                        tID == BARRIER_WAIT ||
                        tID == THREAD_CREATE ||
                        tID == THREAD_JOIN ||
                        tID == COND_WAIT ||
                        tID == COND_SIGNAL)
                    {
                        containKeyCall = true;
                    }
                    
                    if (status == 0)
                    {
                        free(fdn);
                    }
                }
            
            }
            
            // LLVM won't do insertAfter, so we have to get the instruction after the instruction
            // to insert before it
            if (getNextI == true)
            {
                aPhi = I;
                getNextI = false;
            }
        }
        
        if (ContechMinimal == true && containKeyCall == false)
        {
            return false;
        }
        
        llvm_basic_block* bi = new llvm_basic_block;
        if (bi == NULL)
        {
            errs() << "Cannot record CFG in Contech\n";
            return true;
        }
        bi->id = bbid;
        bi->first_op = NULL;
        bi->containGlobalAccess = false;
        bi->lineNum = lineNum;
        bi->numIROps = numIROps;
        bi->fnName.assign(fnName);
        bi->fileName = M.getModuleIdentifier().data();
        bi->critPathLen = getCriticalPathLen(B);
        
        //errs() << "Basic Block - " << bbid << " -- " << memOpCount << "\n";
        //debugLog("checkBufferFunction @" << __LINE__);
        //CallInst::Create(checkBufferFunction, "", aPhi);
        Constant* llvm_bbid;
        Constant* llvm_nops = NULL;
        CallInst* sbb;
        CallInst* sbbc = NULL;
        unsigned int memOpPos = 0;
        
        if (markOnly == true)
        {
            llvm_bbid = ConstantInt::get(cct.int32Ty, bbid);
            Value* argsBB[] = {llvm_bbid};
            debugLog("storeBasicBlockMarkFunction @" << __LINE__);
            sbb = CallInst::Create(cct.storeBasicBlockMarkFunction, ArrayRef<Value*>(argsBB, 1), "", aPhi);
            MarkInstAsContechInst(sbb);
        }
        else {
            Instruction* bufV = CallInst::Create(cct.getBufFunction, "bufPos", aPhi);
            MarkInstAsContechInst(bufV);
            baseBufValue = bufV;
            
            Value* argsGBF[] = {baseBufValue};
            Instruction* bufPos = CallInst::Create(cct.getBufPosFunction, ArrayRef<Value*>(argsGBF,1), "bufPos", aPhi);
            MarkInstAsContechInst(bufPos);
            basePosValue = bufPos;
            
            llvm_bbid = ConstantInt::get(cct.int32Ty, bbid);
            Value* argsBB[] = {llvm_bbid, bufPos, baseBufValue};
            debugLog("storeBasicBlockFunction for BBID: " << bbid << " @" << __LINE__);
            sbb = CallInst::Create(cct.storeBasicBlockFunction, 
                                   ArrayRef<Value*>(argsBB, 3), 
                                   string("storeBlock") + to_string(bbid), 
                                   aPhi);
            MarkInstAsContechInst(sbb);
            
            sbb->getCalledFunction()->addFnAttr( ALWAYS_INLINE);
            posValue = sbb;

//#define TSC_IN_BB
#ifdef TSC_IN_BB
            Value* stTick = CallInst::Create(cct.getCurrentTickFunction, "tick", aPhi);
            MarkInstAsContechInst(stTick);
            
            //pllvm_mem_op tMemOp = insertMemOp(aPhi, stTick, true, memOpPos, posValue);
            pllvm_mem_op tMemOp = new llvm_mem_op;
        
            tMemOp->isWrite = true;
            tMemOp->size = 7;
            
            Constant* cPos = ConstantInt::get(cct.int32Ty, memOpPos);
            Value* addrI = castSupport(cct.voidPtrTy, stTick, aPhi);
            Value* argsMO[] = {addrI, cPos, sbb};
            debugLog("storeMemOpFunction @" << __LINE__);
            CallInst* smo = CallInst::Create(cct.storeMemOpFunction, ArrayRef<Value*>(argsMO, 3), "", aPhi);
            MarkInstAsContechInst(smo);
            
            assert(smo != NULL);
            smo->getCalledFunction()->addFnAttr( ALWAYS_INLINE );
            
            tMemOp->addr = NULL;
            tMemOp->next = NULL;
            
            memOpPos ++;
            memOpCount++;
            if (bi->first_op == NULL) bi->first_op = tMemOp;
            else
            {
                pllvm_mem_op t = bi->first_op;
                while (t->next != NULL)
                {
                    t = t->next;
                }
                t->next = tMemOp;
            }
#endif
            
            // In LLVM 3.3+, switch to Monotonic and not Acquire
            Instruction* fenI = new FenceInst(M.getContext(), Acquire, SingleThread, sbb);
            MarkInstAsContechInst(fenI);
        }

        
        bool hasInstAllMemOps = false;
        for (BasicBlock::iterator I = B.begin(), E = B.end(); I != E; ++I)
        {
        
            // After all of the known memOps have been instrumented, close out the basic
            //   block event based on the number of memOps
            if (hasInstAllMemOps == false && memOpPos == memOpCount && markOnly == false)
            {
                llvm_nops = ConstantInt::get(cct.int32Ty, memOpCount);
                Value* argsBBc[] = {llvm_nops, basePosValue, baseBufValue};
                #ifdef TSC_IN_BB
                if (memOpCount == 1)
                #else
                if (memOpCount == 0)
                #endif
                {
                    debugLog("storeBasicBlockCompFunction @" << __LINE__);
                    sbbc = CallInst::Create(cct.storeBasicBlockCompFunction, ArrayRef<Value*>(argsBBc, 3), "", aPhi);
                    MarkInstAsContechInst(sbbc);
                    
                    Instruction* fenI = new FenceInst(M.getContext(), Release, SingleThread, aPhi);
                    MarkInstAsContechInst(fenI);
                    iPt = aPhi;
                }
                else
                {
                    debugLog("storeBasicBlockCompFunction @" << __LINE__);
                    sbbc = CallInst::Create(cct.storeBasicBlockCompFunction, ArrayRef<Value*>(argsBBc, 3), "", I);
                    MarkInstAsContechInst(sbbc);
                    
                    Instruction* fenI = new FenceInst(M.getContext(), Release, SingleThread, I);
                    MarkInstAsContechInst(fenI);
                    iPt = I;
                }
                sbbc->getCalledFunction()->addFnAttr( ALWAYS_INLINE);
                bi->len = memOpCount + dupMemOps.size();
                hasInstAllMemOps = true;
                
                // Handle the delayed atomics now
                for (auto it = delayedAtomicInsts.begin(), et = delayedAtomicInsts.end(); it != et; ++it)
                {
                    Instruction* atomI = *it;
                    Value* cinst = NULL;
                    if (AtomicRMWInst *armw = dyn_cast<AtomicRMWInst>(atomI))
                    {
                        cinst = castSupport(cct.voidPtrTy, armw->getPointerOperand(), atomI);
                    }
                    else if (AtomicCmpXchgInst *xchgI = dyn_cast<AtomicCmpXchgInst>(atomI))
                    {
                        cinst = castSupport(cct.voidPtrTy, xchgI->getPointerOperand(), atomI);
                    }
                    else
                    {
                        assert(cinst != NULL);
                    }
                    debugLog("getCurrentTickFunction @" << __LINE__);
                    CallInst* nGetTick = CallInst::Create(cct.getCurrentTickFunction, "tick", atomI);
                    MarkInstAsContechInst(nGetTick);
                    
                    Value* synType = ConstantInt::get(cct.int32Ty, 4); // HACK - user-defined sync type
                     // If sync_acquire returns int, pass it, else pass 0 - success
                    Value* retV = ConstantInt::get(cct.int32Ty, 0);
                    Value* cArg[] = {cinst,
                                     synType, 
                                     retV,
                                     nGetTick};
                    debugLog("storeSyncFunction @" << __LINE__);
                    CallInst* nStoreSync = CallInst::Create(cct.storeSyncFunction, ArrayRef<Value*>(cArg,4),
                                                                "", iPt);
                    MarkInstAsContechInst(nStoreSync);
                }
            }

            // If this block is only being marked, then only memops are needed
            if (markOnly == true)
            {
                // Don't bother maintaining a list of memory ops for the basic block
                //   at this time
                bi->len = 0;
                if (LoadInst *li = dyn_cast<LoadInst>(&*I))
                {
                    debugLog("storeMemReadMarkFunction @" << __LINE__);
                    CallInst::Create(cct.storeMemReadMarkFunction, "", li);
                }
                else if (StoreInst *si = dyn_cast<StoreInst>(&*I)) 
                {
                    debugLog("storeMemWriteMarkFunction @" << __LINE__);
                    CallInst::Create(cct.storeMemWriteMarkFunction, "", si);
                }
                if (CallInst *ci = dyn_cast<CallInst>(&*I)) 
                {
                    if (ci->doesNotReturn())
                    {
                        iPt = ci;
                    }
                }
                continue;
            }
            
            // <result> = load [volatile] <ty>* <pointer>[, align <alignment>][, !nontemporal !<index>][, !invariant.load !<index>]
            // Load and store are identical except the cIsWrite is set accordingly.
            // 
            if (LoadInst *li = dyn_cast<LoadInst>(&*I)) 
            {
                pllvm_mem_op tMemOp = NULL;
                
                if (dupMemOps.find(li) != dupMemOps.end()) 
                {
                    tMemOp = insertMemOp(NULL, li->getPointerOperand(), false, memOpPos, posValue);
                    tMemOp->isDep = true;
                    tMemOp->depMemOp = dupMemOpPos[dupMemOps.find(li)->second];
                    tMemOp->depMemOpDelta = dupMemOpOff[li];
                }
                else
                {
                    assert(memOpPos < memOpCount);
                    // Skip globals for testing
                    //if (NULL != dyn_cast<GlobalValue>(li->getPointerOperand())) {memOpCount--;continue;}
                    tMemOp = insertMemOp(li, li->getPointerOperand(), false, memOpPos, posValue);
                    memOpPos ++;
                }
                
                if (tMemOp->isGlobal)
                {
                    bi->containGlobalAccess = true;
                }
                
                unsigned short pos = 0;
                if (bi->first_op == NULL) bi->first_op = tMemOp;
                else
                {
                    pllvm_mem_op t = bi->first_op;
                    while (t->next != NULL)
                    {
                        pos++;
                        t = t->next;
                    }
                    if (dupMemOpPos.find(li) != dupMemOpPos.end()) {dupMemOpPos[li] = pos + 1;}
                    t->next = tMemOp;
                }
            }
            //  store [volatile] <ty> <value>, <ty>* <pointer>[, align <alignment>][, !nontemporal !<index>]
            else if (StoreInst *si = dyn_cast<StoreInst>(&*I)) 
            {
                pllvm_mem_op tMemOp = NULL;
                
                if (dupMemOps.find(si) != dupMemOps.end()) 
                {
                    tMemOp = insertMemOp(NULL, si->getPointerOperand(), true, memOpPos, posValue);
                    tMemOp->isDep = true;
                    tMemOp->depMemOp = dupMemOpPos[dupMemOps.find(si)->second];
                    tMemOp->depMemOpDelta = dupMemOpOff[si];
                }
                else
                {
                    assert(memOpPos < memOpCount);
                    // Skip globals for testing
                    //if (NULL != dyn_cast<GlobalValue>(si->getPointerOperand())) {memOpCount--;continue;}
                    tMemOp = insertMemOp(si, si->getPointerOperand(), true, memOpPos, posValue);
                    memOpPos ++;
                }
                
                if (tMemOp->isGlobal)
                {
                    bi->containGlobalAccess = true;
                }
                
                unsigned short pos = 0;
                if (bi->first_op == NULL) bi->first_op = tMemOp;
                else
                {
                    pllvm_mem_op t = bi->first_op;
                    while (t->next != NULL)
                    {
                        pos ++;
                        t = t->next;
                    }
                    if (dupMemOpPos.find(si) != dupMemOpPos.end()) {dupMemOpPos[si] = pos + 1;}
                    t->next = tMemOp;
                }
            }
            else if (AtomicCmpXchgInst *xchgI = dyn_cast<AtomicCmpXchgInst>(&*I))
            {
                if (hasInstAllMemOps == true)
                {
                    debugLog("getCurrentTickFunction @" << __LINE__);
                    CallInst* nGetTick = CallInst::Create(cct.getCurrentTickFunction, "tick", I);
                    MarkInstAsContechInst(nGetTick);
                    
                    Value* synType = ConstantInt::get(cct.int32Ty, 4); // HACK - user-defined sync type
                     // If sync_acquire returns int, pass it, else pass 0 - success
                    Value* retV = ConstantInt::get(cct.int32Ty, 0);
                    // ++I moves the insertion point to after the xchg inst 
                    Value* cinst = castSupport(cct.voidPtrTy, xchgI->getPointerOperand(), ++I);
                    Value* cArg[] = {cinst,
                                     synType, 
                                     retV,
                                     nGetTick};
                    debugLog("storeSyncFunction @" << __LINE__);
                    CallInst* nStoreSync = CallInst::Create(cct.storeSyncFunction, ArrayRef<Value*>(cArg,4),
                                                        "", I); // Insert after xchg inst
                    MarkInstAsContechInst(nStoreSync);
                    
                    I = nStoreSync;
                    iPt = nStoreSync;
                }
                else
                {
                    delayedAtomicInsts.push_back(xchgI);
                }
            }
            else if (AtomicRMWInst *armw = dyn_cast<AtomicRMWInst>(&*I))
            {
                if (hasInstAllMemOps == true)
                {
                    debugLog("getCurrentTickFunction @" << __LINE__);
                    CallInst* nGetTick = CallInst::Create(cct.getCurrentTickFunction, "tick", I);
                    MarkInstAsContechInst(nGetTick);
                    
                    Value* synType = ConstantInt::get(cct.int32Ty, 4); // HACK - user-defined sync type
                     // If sync_acquire returns int, pass it, else pass 0 - success
                    Value* retV = ConstantInt::get(cct.int32Ty, 0);
                    // ++I moves the insertion point to after the armw inst 
                    Value* cinst = castSupport(cct.voidPtrTy, armw->getPointerOperand(), ++I);
                    Value* cArg[] = {cinst,
                                     synType, 
                                     retV,
                                     nGetTick};
                    debugLog("storeSyncFunction @" << __LINE__);
                    CallInst* nStoreSync = CallInst::Create(cct.storeSyncFunction, ArrayRef<Value*>(cArg,4),
                                                        "", I); // Insert after armw inst
                    MarkInstAsContechInst(nStoreSync);
                    
                    I = nStoreSync;
                    iPt = nStoreSync;
                }
                else
                {
                    delayedAtomicInsts.push_back(armw);
                }
            }
            else if (CallInst *ci = dyn_cast<CallInst>(&*I)) 
            {
                I = InstrumentFunctionCall<CallInst>(ci,
                                                     hasUninstCall, 
                                                     containQueueBuf,
                                                     hasInstAllMemOps, 
                                                     ContechMinimal,
                                                     I, 
                                                     iPt, 
                                                     bi, 
                                                     &cct, 
                                                     this,
                                                     M);
            }
            else if (InvokeInst *ci = dyn_cast<InvokeInst>(&*I)) 
            {
                I = InstrumentFunctionCall<InvokeInst>(ci,
                                                     hasUninstCall, 
                                                     containQueueBuf,
                                                     hasInstAllMemOps, 
                                                     ContechMinimal,
                                                     I, 
                                                     iPt, 
                                                     bi, 
                                                     &cct, 
                                                     this,
                                                     M);
            }
        }
        
        bi->containCall = hasUninstCall;
        
        
        
        // If there are more than 170 memops, then "prealloc" space
        if (memOpCount > ((1024 - 4) / 6))
        {
            // TODO: Function not defined in ct_runtime
            Value* argsCheck[] = {llvm_nops};
            debugLog("checkBufferLargeFunction @" << __LINE__);
            Instruction* callChk = CallInst::Create(cct.checkBufferLargeFunction, ArrayRef<Value*>(argsCheck, 1), "", sbb);
            MarkInstAsContechInst(callChk);
        }
        //
        // Being conservative, if another function was called, then
        // the instrumentation needs to check that the buffer isn't full
        //
        // Being really conservative every block has a check, this also
        //   requires disabling the dominator tree traversal in the runOnModule routine
        //
        //if (/*containCall == true && */containQueueBuf == false && markOnly == false)
        else if (B.getTerminator()->getNumSuccessors() != 1 && markOnly == false)
        {
            // Since calls terminate basic blocks
            //   These blocks would have only 1 successor
            Value* argsCheck[] = {sbbc};
            debugLog("checkBufferFunction @" << __LINE__);
            Instruction* callChk = CallInst::Create(cct.checkBufferFunction, ArrayRef<Value*>(argsCheck, 1), "", iPt);
            MarkInstAsContechInst(callChk);
        }
        
        // Finally record the information about this basic block
        //  into the CFG structure, so that targets can be matched up
        //  once all basic blocks have been parsed
        cfgInfoMap.insert(pair<BasicBlock*, llvm_basic_block*>(&B, bi));
        
        debugLog("Return from BBID: " << bbid);
        
        return true;
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
    
    Instruction* addrI = new BitCastInst(argList.begin(), argTy->getPointerTo(), Twine("Cast to Type"), soloBlock);
    MarkInstAsContechInst(addrI);
    
    // getElemPtr 0, 0 -> arg 0 of type*
    
    Value* args[2] = {ConstantInt::get(cct.int32Ty, 0), ConstantInt::get(cct.int32Ty, 0)};
    Instruction* ppid = GetElementPtrInst::Create(addrI, ArrayRef<Value*>(args, 2), "ParentIdPtr", soloBlock);
    MarkInstAsContechInst(ppid);
    
    Instruction* pid = new LoadInst(ppid, "ParentId", soloBlock);
    MarkInstAsContechInst(pid);
    
    // getElemPtr 0, 1 -> arg 1 of type*
    args[1] = ConstantInt::get(cct.int32Ty, 1);
    Instruction* parg = GetElementPtrInst::Create(addrI, ArrayRef<Value*>(args, 2), "ArgPtr", soloBlock);
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
        cArgExt[i] = it;
        ++it;
    }
    
    Value* cArg[] = {--(argList.end())};
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
        cArgExt[i] = it;
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
    auto castOp = CastInst::getCastOpcode (sourceValue, false, castType, false);
    debugLog("CastInst @" << __LINE__);
    Instruction* ret = CastInst::Create(castOp, sourceValue, castType, "Cast to Support Type", insertBefore);
    MarkInstAsContechInst(ret);
    return ret;
}
    
char Contech::ID = 0;
static RegisterPass<Contech> X("Contech", "Contech Pass", false, false);

