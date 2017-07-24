//===- Contech.cpp - Based on Example code from "Writing an LLVM Pass" ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source License.
//   And the license terms of Contech, see LICENSE.
//
//===----------------------------------------------------------------------===//

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

map<BasicBlock*, llvm_basic_block*> cfgInfoMap;

// ContechState is required to reconstruct the basic block events from the event trace
cl::opt<string> ContechStateFilename("ContechState", cl::desc("File with current Contech state"), cl::value_desc("filename"));

// MarkFrontEnd and Minimal cover variations of the instrumentation that are used in special cases
cl::opt<bool> ContechMarkFrontend("ContechMarkFE", cl::desc("Generate a minimal marked output"));
cl::opt<bool> ContechMinimal("ContechMinimal", cl::desc("Generate a minimally instrumented output"));

uint64_t tailCount = 0;

namespace llvm {
#define STORE_AND_LEN(x) x, sizeof(x)
#define FUNCTIONS_INSTRUMENT_SIZE 60
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
                                           {STORE_AND_LEN("sem_post"), SYNC_RELEASE},
                                           {STORE_AND_LEN("sem_wait"), SYNC_ACQUIRE},
                                           {STORE_AND_LEN("sem_trywait"), SYNC_ACQUIRE},
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
                                           {STORE_AND_LEN("GOMP_barrier"), OMP_BARRIER},
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
                                           {STORE_AND_LEN("mpi_wait_"), MPI_TRANSFER_WAIT},
                                           {STORE_AND_LEN("__cilkrts_leave_frame"), CILK_FRAME_DESTROY},
                                           {STORE_AND_LEN("llvm.eh.sjlj.setjmp"), CILK_FRAME_CREATE},
                                           {STORE_AND_LEN("__cilkrts_sync"), CILK_SYNC}};


    ModulePass* createContechPass() { return new Contech(); }
}

//
// Create any globals required for this module
//
//  These globals are predominantly setting up the constants for Contech's runtime functions.
//    Each constant is effectively the entry point to a function and can be called / invoked.
//    The routine also finds the appropriate type definitions and retains them for consistent use.
//
bool Contech::doInitialization(Module &M)
{
    // Function types are named fun(Return type)(arg1 ... argN)Ty
    FunctionType* funVoidPtrI32I32VoidPtrI8I8Ty;
    FunctionType* funVoidVoidPtrI32VoidPtrI8I8Ty;
    FunctionType* funVoidVoidPtrI32I32I64I64Ty;
    FunctionType* funVoidPtrVoidPtrTy;
    FunctionType* funVoidPtrVoidTy;
    FunctionType* funVoidVoidTy;
    FunctionType* funVoidVoidPtrTy;
    FunctionType* funVoidVoidPtrVoidPtrI32Ty;
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
    FunctionType* funI32I32I32VoidPtrI8I8Ty;
    FunctionType* funVoidVoidPtrI64Ty;
    FunctionType* funVoidVoidPtrI64I32I32Ty;
    FunctionType* funVoidI32I64I64Ty;

    // Get the different integer types required by Contech
    LLVMContext &ctx = M.getContext();
    currentDataLayout = &M.getDataLayout();
    cct.int8Ty = Type::getInt8Ty(ctx);
    cct.int32Ty = Type::getInt32Ty(ctx);
    cct.int64Ty = Type::getInt64Ty(ctx);
    cct.voidTy = Type::getVoidTy(ctx);
    cct.voidPtrTy = cct.int8Ty->getPointerTo();

    Type* funVoidPtrVoidTypes[] = {cct.voidPtrTy};
    funVoidPtrVoidPtrTy = FunctionType::get(cct.voidPtrTy, ArrayRef<Type*>(funVoidPtrVoidTypes, 1), false);

    funI32VoidPtrTy = FunctionType::get(cct.int32Ty, ArrayRef<Type*>(funVoidPtrVoidTypes, 1), false);
    cct.getBufPosFunction = M.getOrInsertFunction("__ctGetBufferPos",funI32VoidPtrTy);

    Type* argsBB[] = {cct.int32Ty, cct.int32Ty, cct.voidPtrTy,  cct.int8Ty,  cct.int8Ty};
    funVoidPtrI32I32VoidPtrI8I8Ty = FunctionType::get(cct.voidPtrTy, ArrayRef<Type*>(argsBB, 5), false);
    cct.storeBasicBlockFunction = M.getOrInsertFunction("__ctStoreBasicBlock", funVoidPtrI32I32VoidPtrI8I8Ty);

    funI32I32I32VoidPtrI8I8Ty = FunctionType::get(cct.int32Ty, ArrayRef<Type*>(argsBB, 5), false);
    cct.storeBasicBlockCompFunction = M.getOrInsertFunction("__ctStoreBasicBlockComplete", funI32I32I32VoidPtrI8I8Ty);

    Type* argsMO[] = {cct.voidPtrTy, cct.int32Ty, cct.voidPtrTy, cct.int8Ty, cct.int8Ty};
    funVoidVoidPtrI32VoidPtrI8I8Ty = FunctionType::get(cct.voidTy, ArrayRef<Type*>(argsMO, 5), false);
    cct.storeMemOpFunction = M.getOrInsertFunction("__ctStoreMemOp", funVoidVoidPtrI32VoidPtrI8I8Ty);

    funVoidPtrVoidTy = FunctionType::get(cct.voidPtrTy, false);
    cct.getBufFunction = M.getOrInsertFunction("__ctGetBuffer",funVoidPtrVoidTy);
    cct.cilkInitFunction = M.getOrInsertFunction("__ctInitCilkSync", funVoidPtrVoidTy);

    // void (void) functions:
    funVoidVoidTy = FunctionType::get(cct.voidTy, false);
    cct.allocateBufferFunction = M.getOrInsertFunction("__ctAllocateLocalBuffer", funVoidVoidTy);
    cct.storeMemReadMarkFunction = M.getOrInsertFunction("__ctStoreMemReadMark", funVoidVoidTy);
    cct.storeMemWriteMarkFunction = M.getOrInsertFunction("__ctStoreMemWriteMark", funVoidVoidTy);
    cct.ompPushParentFunction = M.getOrInsertFunction("__ctOMPPushParent", funVoidVoidTy);
    cct.ompPopParentFunction = M.getOrInsertFunction("__ctOMPPopParent", funVoidVoidTy);
    cct.ompProcessJoinFunction =  M.getOrInsertFunction("__ctOMPProcessJoinStack", funVoidVoidTy);

    // Void -> Int32 / 64
    cct.allocateCTidFunction = M.getOrInsertFunction("__ctAllocateCTid", FunctionType::get(cct.int32Ty, false));
    cct.getThreadNumFunction = M.getOrInsertFunction("__ctGetLocalNumber", FunctionType::get(cct.int32Ty, false));
    cct.getCurrentTickFunction = M.getOrInsertFunction("__ctGetCurrentTick", FunctionType::get(cct.int64Ty, false));
    cct.allocateTicketFunction =  M.getOrInsertFunction("__ctAllocateTicket", FunctionType::get(cct.int64Ty, false));

    cct.ctPeekParentIdFunction = M.getOrInsertFunction("__ctPeekParent", FunctionType::get(cct.int32Ty, false));
    cct.ompGetNestLevelFunction = M.getOrInsertFunction("omp_get_level", FunctionType::get(cct.int32Ty, false));


    Type* argsSSync[] = {cct.voidPtrTy, cct.int32Ty/*type*/, cct.int32Ty/*retVal*/, cct.int64Ty /*ct_tsc_t*/, cct.int64Ty};
    funVoidVoidPtrI32I32I64I64Ty = FunctionType::get(cct.voidTy, ArrayRef<Type*>(argsSSync, 5), false);
    cct.storeSyncFunction = M.getOrInsertFunction("__ctStoreSync", funVoidVoidPtrI32I32I64I64Ty);

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

    Type* argsCFC[] = {cct.voidPtrTy, cct.int64Ty, cct.int32Ty, cct.int32Ty};
    funVoidVoidPtrI64I32I32Ty = FunctionType::get(cct.voidTy, ArrayRef<Type*>(argsCFC, 4), false);
    cct.cilkCreateFunction = M.getOrInsertFunction("__ctRecordCilkFrame", funVoidVoidPtrI64I32I32Ty);

    Type* argsInit[] = {cct.voidPtrTy};
    funVoidVoidPtrTy = FunctionType::get(cct.voidTy, ArrayRef<Type*>(argsInit, 1), false);
    cct.cilkSyncFunction = M.getOrInsertFunction("__ctRecordCilkSync", funVoidVoidPtrTy);
    cct.cilkRestoreFunction = M.getOrInsertFunction("__ctRestoreCilkFrame", funVoidVoidPtrTy);
    cct.cilkParentFunction = M.getOrInsertFunction("__ctCilkPromoteParent", funVoidVoidPtrTy);
    cct.writeElideGVEventsFunction =  M.getOrInsertFunction("__ctWriteElideGVEvents", funVoidVoidPtrTy);
    
    Function* f = dyn_cast<Function>(cct.writeElideGVEventsFunction);
    if (f != NULL) 
    {
        Instruction* iPt;
        if (f->empty())
        {
            BasicBlock* bbEntry = BasicBlock::Create(M.getContext(), "", f, NULL);
            iPt = ReturnInst::Create(M.getContext(), bbEntry);
        }
    }

    Type* argsSGV[] = {cct.voidPtrTy, cct.voidPtrTy, cct.int32Ty};
    funVoidVoidPtrVoidPtrI32Ty = FunctionType::get(cct.voidTy, ArrayRef<Type*>(argsSGV, 3), false);
    cct.storeGVEventFunction = M.getOrInsertFunction("__ctStoreGVEvent", funVoidVoidPtrVoidPtrI32Ty);
    
    Type* argsCTCreate[] = {cct.int32Ty, cct.int64Ty, cct.int64Ty};
    funVoidI32I64I64Ty = FunctionType::get(cct.voidTy, ArrayRef<Type*>(argsCTCreate, 3), false);
    cct.storeThreadCreateFunction = M.getOrInsertFunction("__ctStoreThreadCreate", funVoidI32I64I64Ty);

    if (currentDataLayout->getPointerSizeInBits() == 64)
    {
        cct.pthreadTy = cct.int64Ty;
        cct.pthreadSize = 8;
    }
    else
    {
        cct.pthreadTy = cct.int32Ty;
        cct.pthreadSize = 4;
    }

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
pllvm_mem_op Contech::insertMemOp(Instruction* li, Value* addr, bool isWrite, unsigned int memOpPos, Value* pos, bool elide, Module &M, Value* varBBID)
{
    pllvm_mem_op tMemOp = new llvm_mem_op;

    tMemOp->addr = NULL;
    tMemOp->next = NULL;
    tMemOp->isWrite = isWrite;
    tMemOp->isDep = false;
    tMemOp->isGlobal = false;
    tMemOp->depMemOp = 0;
    tMemOp->depMemOpDelta = 0;
    tMemOp->size = getSimpleLog(getSizeofType(addr->getType()->getPointerElementType()));

    if (tMemOp->size > 4)
    {
        errs() << "MemOp of size: " << tMemOp->size << "\n";
    }

    if (/*GlobalValue* gv = */NULL != dyn_cast<GlobalValue>(addr) &&
        NULL == dyn_cast<GetElementPtrInst>(addr))
    {
        tMemOp->isGlobal = true;
        tMemOp->addr = addr;
        //errs() << "Is global - " << *addr << "\n";
        
        if (li != NULL)
        {
            int elideGVId = assignIdToGlobalElide(dyn_cast<Constant>(tMemOp->addr), M);
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
                tMemOp->addr = gepAddr->getPointerOperand();
                
                APInt* constOffset = new APInt(64, 0, true);
                gepAddr->accumulateConstantOffset(*currentDataLayout, *constOffset);
                if (!constOffset->isSignedIntN(32)) return tMemOp;
                int64_t offset = constOffset->getSExtValue();
                errs() << offset << "\n";
                int elideGVId = assignIdToGlobalElide(dyn_cast<Constant>(tMemOp->addr), M);
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
        Constant* cPos = ConstantInt::get(cct.int32Ty, memOpPos);
        Constant* cElide = ConstantInt::get(cct.int8Ty, elide);
        Instruction* addrI = new BitCastInst(addr, cct.voidPtrTy, Twine("Cast as void"), li);
        MarkInstAsContechInst(addrI);

        Value* argsMO[] = {addrI, cPos, pos, cElide, varBBID};
        debugLog("storeMemOpFunction @" << __LINE__);
        CallInst* smo = CallInst::Create(cct.storeMemOpFunction, ArrayRef<Value*>(argsMO, 5), "", li);
        MarkInstAsContechInst(smo);

        assert(smo != NULL);
        smo->getCalledFunction()->addFnAttr( ALWAYS_INLINE );
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
    errs() << *pred;
    tailCount++;
    
    return true;
}

//
// findSimilarMemoryInst
//
//   A key performance feature, this function will determine whether a given memory operation
//     is statically offset from another memory operation within that same basic block.  If so,
//     then the later operation's address can be omitted and computed after execution.
//   If such a pair is found, then this function will compute the static offset between the two
//     memory accesses.
//
Value* Contech::findSimilarMemoryInst(Instruction* memI, Value* addr, int* offset)
{
    vector<Value*> addrComponents;
    Instruction* addrI = dyn_cast<Instruction>(addr);
    int tOffset = 0, baseOffset = 0;

    *offset = 0;

    //return NULL; // memdup toggle

    if (addrI == NULL)
    {
        // No instruction generates the address, it is probably a global value
        for (auto it = memI->getParent()->begin(), et = memI->getParent()->end(); it != et; ++it)
        {
            // Stop iterating at self
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
        // If the search has reached the current memory operation, then no match exists
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
        bool constMode = false, finConst = false, isMatch = true;
        if (gepAddrT->getPointerOperand() != gepAddr->getPointerOperand()) continue;
        tOffset = 0;
        for (auto itG = gep_type_begin(gepAddrT), etG = gep_type_end(gepAddrT); itG != etG; i++, ++itG)
        {
            Value* gepI = itG.getOperand();

            // If the index of GEP is a Constant, then it can vary between mem ops
            if (ConstantInt* gConst = dyn_cast<ConstantInt>(gepI))
            {
                if (i == addrComponents.size())
                {
                    finConst = true;
                    // Last field was not a constant, but that can be alright
                    if (constMode == false)
                    {
                        
                    }
                }
                ConstantInt* aConst = NULL;
                if (finConst == true ||
                    (aConst = dyn_cast<ConstantInt>(addrComponents[i])))
                {
                    if (!finConst &&
                        aConst->getSExtValue() != gConst->getSExtValue()) constMode = true;
                    isMatch = true;
                    
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
                    isMatch = false;
                    break;
                }
            }
            else if (constMode == true)
            {
                // Only can handle cases where a varying constant is at the end of the calculation
                //   or has non-varying constants around it.
                isMatch = false;
                break;
            }

            // temp offset remains 0 until a final sequence of constant int offsets
            tOffset = 0;
            if (i >= addrComponents.size() || 
                gepI != addrComponents[i])
            {
                isMatch = false;
                break;
            }
        }

        if (isMatch ||
            (i == addrComponents.size() && (gepAddrT->getNumIndices() != gepAddr->getNumIndices())))
        {
            //errs() << *gepAddrT << " ?=? " << *gepAddr << "\t" << baseOffset << "\t" << tOffset << "\n";
            *offset = baseOffset - tOffset;
            return &*it;
        }
    }

    return NULL;
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
        else if (classifyFunctionName(fmn) != NONE ||
                 __ctStrCmp(fmn, "__ct") == 0)
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
        
        bool changed = false;
        do {
            changed = false;
            for (Function::iterator B = F->begin(), BE = F->end(); B != BE; ++B)
            {
                if (attemptTailDuplicate(&*B))
                {
                    changed = true;
                    break;
                }
            }
        } while (changed);
        
        // static analysis
        Function* pF = &*F;
        
        // the loop information
        LoopInfo* LI = &getAnalysis<LoopInfoWrapperPass>(*pF).getLoopInfo();
        
        // state of loop exits
        map<int, Loop*> loopExits;
        collectLoopExits(pF, loopExits, LI);
        
        // state of loop and basic block
        map<int, Loop*> loopBelong;
        collectLoopBelong(pF, loopBelong, LI);
        
        // whether is a loop entry
        unordered_map<Loop*, int> loopEntry{ collectLoopEntry(pF, LI) };

        map<int, llvm_inst_block> costPerBlock;
        int num_checks = 0;
        int origin_checks = 0;
        unsigned int bb_count_base = bb_count;
        
        // Now instrument each basic block in the function
        for (Function::iterator B = F->begin(), BE = F->end(); B != BE; ++B) 
        {
            BasicBlock &pB = *B;
            internalRunOnBasicBlock(pB, M, bb_count, ContechMarkFrontend, fmn, 
                                    costPerBlock, num_checks, origin_checks);
            bb_count++;
            assert(bb_count - bb_count_base < 64);
        }
        
        // Switch the "bb_count" to the next function
        errs() << bb_count << "\t";
        bb_count += (62);
        bb_count &= (~0x3f);
        errs() << bb_count << "\n";

        // run the check analysis
        BufferCheckAnalysis bufferCheckAnalysis{
            costPerBlock,
            loopExits,
            loopBelong,
            loopEntry,
            1024
        };
        
        // run analysis
        bufferCheckAnalysis.runAnalysis(pF);
        // see the analysis result
        map<int, bool> needCheckAtBlock{ bufferCheckAnalysis.getNeedCheckAtBlock() };
        map<int, bool> needLongBBID { bufferCheckAnalysis.getNeedLongBBID() };
        
        hash<BasicBlock*> blockHash{};
        
        for (Function::iterator B = F->begin(), BE = F->end(); B != BE; ++B) 
        {
            int bb_val = blockHash(&*B);
            
            auto isLongBBID = needLongBBID.find(bb_val);
            if (isLongBBID->second == true)
            {
                auto lib = costPerBlock.find(bb_val);
                if (lib == costPerBlock.end())
                {
                    errs() << "Failed to look up known block in cost table.\n" << *B;
                    assert("Missing Block" && 0);
                }
                
                Value* true_val = ConstantInt::get(cct.int8Ty, 1);
                
                CallInst* sbbc = dyn_cast<CallInst>(lib->second.posValue);
                assert(sbbc != NULL && "llvm_inst_block posValue is not sbbc CallInst");
                
                sbbc->setArgOperand(4, true_val);
                CallInst* basePosValue = dyn_cast<CallInst>(sbbc->getArgOperand(1));
                assert(basePosValue != NULL && "arg1 of sbbc is not basePosValue, a CallInst");
                
                CallInst* sbb = NULL;
                for (auto it = basePosValue->user_begin(), et = basePosValue->user_end();
                     it != et; ++it)
                {
                    sbb = dyn_cast<CallInst>(*it);
                    assert(sbb != NULL);
                    if (sbb != sbbc) break;
                }
                assert(sbb != NULL);
                
                sbb->setArgOperand(4, true_val);
                for (auto it = sbb->user_begin(), et = sbb->user_end(); it != et; ++it)
                {
                    CallInst* memOp = dyn_cast<CallInst>(*it);
                    assert(memOp != NULL);
                    memOp->setArgOperand(4, true_val);
                }
            }
            
            auto isReq = needCheckAtBlock.find(bb_val);
            if (isReq->second == false) {continue;}
            
            auto lib = costPerBlock.find(bb_val);
            if (lib == costPerBlock.end())
            {
                errs() << "Failed to look up known block in cost table.\n" << *B;
                assert("Missing Block" && 0);
            }
            
            // HACK!  Skip blocks that are the pre elides.
            //if(lib->second.preElide == true) {continue;}
            assert(lib->second.preElide == false);
            
            Value* sbbc = lib->second.posValue;
            Value* argsCheck[] = {sbbc};
            Instruction* iPt = lib->second.insertPoint;
            
            debugLog("checkBufferFunction@" << __LINE__);
            Instruction* callChk = CallInst::Create(cct.checkBufferFunction, ArrayRef<Value*>(argsCheck, 1), "", iPt);
            MarkInstAsContechInst(callChk);
            num_checks++;
        }
        
        errs() << F->getName().str() << "," << num_checks 
               << "," << origin_checks << "\n" ;

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
    
    bb_count++;
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
            contechStateFile->write((char*)&bi->second->next_id, sizeof(int32_t));
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

            strLen = bi->second->fileNameSize;
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
                if (t->isDep)
                {
                    memFlags |= (t->isGlobal)?BBI_FLAG_MEM_GV:0x0;
                }

                contechStateFile->write((char*)&memFlags, sizeof(char));
                contechStateFile->write((char*)&t->size, sizeof(char));
                // Add optional dep mem op info
                if (t->isDep)
                {
                    assert((memFlags & BBI_FLAG_MEM_DUP) == BBI_FLAG_MEM_DUP);
                    contechStateFile->write((char*)&t->depMemOp, sizeof(uint16_t));
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

    errs() << "Tail Dup Count: " << tailCount << "\n";
    
    return true;
}

// returns size in bytes
unsigned int Contech::getSizeofType(Type* t)
{
    unsigned int r = t->getPrimitiveSizeInBits();
    if (r > 0) return (r + 7) / 8;  //Round up to the nearest byte
    else if (t->isPointerTy()) { return cct.pthreadSize;}
    else if (t->isPtrOrPtrVectorTy()) 
    { 
        errs() << *t << " is pointer vector\n";
        return t->getVectorNumElements() * cct.pthreadSize;
    }
    else if (t->isVectorTy()) { return t->getVectorNumElements() * t->getScalarSizeInBits();}
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
                    // Contech did not insert this branch, but is should claim it, so the instruction
                    //   is not later removed.
                    if (bi->isUnconditional()) {*st = 4; MarkInstAsContechInst(B.getTerminator()); return false;}
                }
                else if (/* ReturnInst *ri = */ dyn_cast<ReturnInst>(&*I))
                {
                    *st = 5;
                    return false;
                }
            }
            B.splitBasicBlock(I, "");
            MarkInstAsContechInst(B.getTerminator());
            return true;
        }
    }

    return false;
}

//
// For each basic block
//
bool Contech::internalRunOnBasicBlock(BasicBlock &B,  Module &M, int bbid, const bool markOnly, const char* fnName, 
                                      map<int, llvm_inst_block>& costOfBlock, int& num_checks, int& origin_check)
{
    Instruction* iPt = B.getTerminator();
    vector<pllvm_mem_op> opsInBlock;
    unsigned int memOpCount = 0, memOpGVElide = 0;
    Instruction* aPhi ;//= convertIterToInst(B.begin());
    bool getNextI = false;
    bool containQueueBuf = false;
    bool hasUninstCall = true; // Any call is uninst
    bool containKeyCall = false;
    bool elideBasicBlockId = false;
    Value* posValue = NULL;
    Value* basePosValue = NULL;
    Value* baseBufValue = NULL;
    unsigned int lineNum = 0, numIROps = B.size();
    unsigned int fileNameSize = 0;
    const char* fileName;

    auto abadf = B.begin();
    aPhi = convertIterToInst(abadf);

    vector<Instruction*> delayedAtomicInsts;
    map<Instruction*, Value*> dupMemOps;
    map<Instruction*, int> dupMemOpOff;
    map<Value*, unsigned short> dupMemOpPos;

    Value* varBBID = ConstantInt::get(cct.int8Ty, 0);

    //errs() << "BB: " << bbid << "\n";
    debugLog("Enter BBID: " << bbid);

    if (lineNum == 0)
    {
        Instruction* gf = B.getFirstNonPHIOrDbgOrLifetime();
        lineNum = getLineNum(gf);
        DILocation* dis = (gf)->getDebugLoc();//.getScope();
        if (dis != NULL)
        {
            fileName = dis->getFilename().str().c_str();
            fileNameSize = (fileName != NULL)?strlen(fileName):0;
        }
        //dyn_cast<DIScope>(dis)->getFilename().str().c_str();
    }
    
    for (BasicBlock::iterator I = B.begin(), E = B.end(); I != E; ++I)
    {
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
            aPhi = convertIterToInst(I);
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
    
    //
    // Large blocks cannot have their IDs elided.
    // TODO: permament value or different approach to checks
    //
    if (memOpCount < 160) {
        elideBasicBlockId = checkAndApplyElideId(&B, bbid, costOfBlock);
    }

    bi->id = bbid;
    bi->next_id = -1;
    bi->first_op = NULL;
    bi->containGlobalAccess = false;
    bi->containAtomic = false;
    bi->lineNum = lineNum;
    bi->numIROps = numIROps;
    bi->fnName.assign(fnName);
    bi->fileName = fileName;
    bi->fileNameSize = fileNameSize;
    //bi->fileName = B.getDebugLoc().getScope().getFilename();//M.getModuleIdentifier().data();
    bi->critPathLen = getCriticalPathLen(B);
    bi->containCall = false;

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
    else
    {
        Instruction* bufV = CallInst::Create(cct.getBufFunction, "bufPos", aPhi);
        MarkInstAsContechInst(bufV);
        baseBufValue = bufV;

        Value* argsGBF[] = {baseBufValue};
        Instruction* bufPos = CallInst::Create(cct.getBufPosFunction, ArrayRef<Value*>(argsGBF,1), "bufPos", aPhi);
        MarkInstAsContechInst(bufPos);
        basePosValue = bufPos;

        Value* cElide = ConstantInt::get(cct.int8Ty, elideBasicBlockId);
        llvm_bbid = ConstantInt::get(cct.int32Ty, bbid);
        Value* argsBB[] = {llvm_bbid, basePosValue, baseBufValue, cElide, varBBID};
        debugLog("storeBasicBlockFunction for BBID: " << bbid << " @" << __LINE__);
        sbb = CallInst::Create(cct.storeBasicBlockFunction,
                               ArrayRef<Value*>(argsBB, 5),
                               string("storeBlock") + to_string(bbid),
                               aPhi);
        MarkInstAsContechInst(sbb);

        sbb->getCalledFunction()->addFnAttr( ALWAYS_INLINE);
        posValue = sbb;

        // TSC_IN_BB - an optional research feature that adds a timestamp to every basic block
        //   Including the timestamp slows overall execution and is at such a fine granularity
        //   that many measurements can be meaningless.
//#define TSC_IN_BB
#ifdef TSC_IN_BB
        Instruction* stTick = CallInst::Create(cct.getCurrentTickFunction, "tick", aPhi);
        MarkInstAsContechInst(stTick);

        //pllvm_mem_op tMemOp = insertMemOp(aPhi, stTick, true, memOpPos, posValue);
        pllvm_mem_op tMemOp = new llvm_mem_op;

        tMemOp->isWrite = true;
        tMemOp->size = 7;
        tMemOp->isDep = false;
        tMemOp->depMemOp = 0;
        tMemOp->depMemOpDelta = 0;

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
        if (bi->first_op == NULL)
        {
            bi->first_op = tMemOp;
        }
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
        Instruction* fenI = new FenceInst(M.getContext(), AtomicOrdering::Acquire, SingleThread, bufV);
        MarkInstAsContechInst(fenI);
    }


    bool hasInstAllMemOps = false;
    for (BasicBlock::iterator I = B.begin(), E = B.end(); I != E; ++I)
    {

        // After all of the known memOps have been instrumented, close out the basic
        //   block event based on the number of memOps
        if (hasInstAllMemOps == false && memOpPos == memOpCount && markOnly == false)
        {
            Value* cElide = ConstantInt::get(cct.int8Ty, elideBasicBlockId);
            llvm_nops = ConstantInt::get(cct.int32Ty, memOpCount);
            Value* argsBBc[] = {llvm_nops, basePosValue, baseBufValue, cElide, varBBID};
            #ifdef TSC_IN_BB
            if (memOpCount == 1)
            #else
            if (memOpCount == 0)
            #endif
            {
                debugLog("storeBasicBlockCompFunction @" << __LINE__);
                sbbc = CallInst::Create(cct.storeBasicBlockCompFunction, ArrayRef<Value*>(argsBBc, 5), "", aPhi);
                MarkInstAsContechInst(sbbc);

                Instruction* fenI = new FenceInst(M.getContext(), AtomicOrdering::Release, SingleThread, aPhi);
                MarkInstAsContechInst(fenI);
                iPt = aPhi;
            }
            else
            {
                debugLog("storeBasicBlockCompFunction @" << __LINE__);
                sbbc = CallInst::Create(cct.storeBasicBlockCompFunction, ArrayRef<Value*>(argsBBc, 5), "", convertIterToInst(I));
                MarkInstAsContechInst(sbbc);

                Instruction* fenI = new FenceInst(M.getContext(), AtomicOrdering::Release, SingleThread, convertIterToInst(I));
                MarkInstAsContechInst(fenI);
                iPt = convertIterToInst(I);
            }
            sbbc->getCalledFunction()->addFnAttr( ALWAYS_INLINE);
            bi->len = memOpCount + dupMemOps.size() + memOpGVElide;
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
                Value* nTicket = ConstantInt::get(cct.int64Ty, 0);
                Value* cArg[] = {cinst,
                                 synType,
                                 retV,
                                 nGetTick,
                                 nTicket};
                debugLog("storeSyncFunction @" << __LINE__);
                CallInst* nStoreSync = CallInst::Create(cct.storeSyncFunction, ArrayRef<Value*>(cArg,5),
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
                tMemOp = insertMemOp(NULL, li->getPointerOperand(), false, memOpPos, posValue, elideBasicBlockId, M, varBBID);
                tMemOp->isDep = true;
                tMemOp->depMemOp = dupMemOpPos[dupMemOps.find(li)->second];
                tMemOp->depMemOpDelta = dupMemOpOff[li];
                
                if (tMemOp->isGlobal)
                {
                    bi->containGlobalAccess = true;
                    tMemOp->isGlobal = false;
                }
            }
            else
            {
                assert(memOpPos < memOpCount);
                tMemOp = insertMemOp(li, li->getPointerOperand(), false, memOpPos, posValue, elideBasicBlockId, M, varBBID);
                if (tMemOp->isGlobal && tMemOp->isDep)
                {
                    memOpCount--;
                    memOpGVElide++;
                }
                else
                {
                    memOpPos ++;
                }
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
                tMemOp = insertMemOp(NULL, si->getPointerOperand(), true, memOpPos, posValue, elideBasicBlockId, M, varBBID);
                tMemOp->isDep = true;
                tMemOp->depMemOp = dupMemOpPos[dupMemOps.find(si)->second];
                tMemOp->depMemOpDelta = dupMemOpOff[si];
                
                if (tMemOp->isGlobal)
                {
                    bi->containGlobalAccess = true;
                    tMemOp->isGlobal = false;
                }
            }
            else
            {
                assert(memOpPos < memOpCount);
                // Skip globals for testing
                //if (NULL != dyn_cast<GlobalValue>(si->getPointerOperand())) {memOpCount--;continue;}
                tMemOp = insertMemOp(si, si->getPointerOperand(), true, memOpPos, posValue, elideBasicBlockId, M, varBBID);
                if (tMemOp->isGlobal && tMemOp->isDep)
                {
                    memOpCount--;
                    memOpGVElide++;
                }
                else
                {
                    memOpPos ++;
                }
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
            bi->containAtomic = true;
            if (hasInstAllMemOps == true)
            {
                debugLog("getCurrentTickFunction @" << __LINE__);
                CallInst* nGetTick = CallInst::Create(cct.getCurrentTickFunction, "tick", convertIterToInst(I));
                MarkInstAsContechInst(nGetTick);

                Value* synType = ConstantInt::get(cct.int32Ty, 4); // HACK - user-defined sync type
                 // If sync_acquire returns int, pass it, else pass 0 - success
                Value* retV = ConstantInt::get(cct.int32Ty, 0);
                // ++I moves the insertion point to after the xchg inst
                Value* cinst = castSupport(cct.voidPtrTy, xchgI->getPointerOperand(), convertIterToInst(++I));
                Value* nTicket = ConstantInt::get(cct.int64Ty, 0);
                Value* cArg[] = {cinst,
                                 synType,
                                 retV,
                                 nGetTick,
                                 nTicket};
                debugLog("storeSyncFunction @" << __LINE__);
                CallInst* nStoreSync = CallInst::Create(cct.storeSyncFunction, ArrayRef<Value*>(cArg,5),
                                                    "", convertIterToInst(I)); // Insert after xchg inst
                MarkInstAsContechInst(nStoreSync);

                I = convertInstToIter(nStoreSync);
                iPt = nStoreSync;
            }
            else
            {
                delayedAtomicInsts.push_back(xchgI);
            }
        }
        else if (AtomicRMWInst *armw = dyn_cast<AtomicRMWInst>(&*I))
        {
            bi->containAtomic = true;
            if (hasInstAllMemOps == true)
            {
                debugLog("getCurrentTickFunction @" << __LINE__);
                CallInst* nGetTick = CallInst::Create(cct.getCurrentTickFunction, "tick", convertIterToInst(I));
                MarkInstAsContechInst(nGetTick);

                Value* synType = ConstantInt::get(cct.int32Ty, 4); // HACK - user-defined sync type
                 // If sync_acquire returns int, pass it, else pass 0 - success
                Value* retV = ConstantInt::get(cct.int32Ty, 0);
                // ++I moves the insertion point to after the armw inst
                Value* cinst = castSupport(cct.voidPtrTy, armw->getPointerOperand(), convertIterToInst(++I));
                Value* nTicket = ConstantInt::get(cct.int64Ty, 0);
                Value* cArg[] = {cinst,
                                 synType,
                                 retV,
                                 nGetTick,
                                 nTicket};
                debugLog("storeSyncFunction @" << __LINE__);
                CallInst* nStoreSync = CallInst::Create(cct.storeSyncFunction, ArrayRef<Value*>(cArg,5),
                                                    "", convertIterToInst(I)); // Insert after armw inst
                MarkInstAsContechInst(nStoreSync);

                I = convertInstToIter(nStoreSync);
                iPt = nStoreSync;
            }
            else
            {
                delayedAtomicInsts.push_back(armw);
            }
        }
        else if (CallInst *ci = dyn_cast<CallInst>(&*I))
        {
            bool huc = hasUninstCall;
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
            if (huc == false) hasUninstCall = false;
        }
        else if (InvokeInst *ci = dyn_cast<InvokeInst>(&*I))
        {
            bool huc = hasUninstCall;
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
            if (huc == false) hasUninstCall = false;
        }
    }

    // There could be multiple call instructions in the block, due to intrinsics and
    //   debug "calls".  If any are real calls, then the block contains a call.
    //   If it just has uinst calls, then these are calls that don't count such as llvm.dbg
    //   or _ctStoreBasicBlock().
    bi->containCall = (bi->containCall)?true:(!hasUninstCall);
    errs() << bbid << "\t" << bi->containCall << "\t" << hasUninstCall << "\t" << containQueueBuf << "\n";
    {
        hash<BasicBlock*> blockHash{};
        int bb_val = blockHash(&B);
        llvm_inst_block lib;
        lib.cost = memOpCount * 6 + ((elideBasicBlockId == true)? 0 : 3) 
                             + ((hasUninstCall == false) ? 64 : 0);
        lib.insertPoint = iPt;
        lib.posValue = sbbc;
        lib.hasCheck = false;
        lib.hasElide = elideBasicBlockId;
        lib.preElide = false;
        lib.containQueueCall = containQueueBuf;
        lib.containCall = bi->containCall;
        // If there are more than 170 memops, then "prealloc" space
        if (memOpCount > ((1024 - 4) / 6))
        {
            // TODO: Function not defined in ct_runtime
            Value* argsCheck[] = {llvm_nops};
            debugLog("checkBufferLargeFunction @" << __LINE__);
            origin_check++;
            num_checks++;
            
            Instruction* callChk = CallInst::Create(cct.checkBufferLargeFunction, ArrayRef<Value*>(argsCheck, 1), "", sbb);
            MarkInstAsContechInst(callChk);
            
            lib.hasCheck = true;
        }
        costOfBlock[bb_val] = lib;
    }
    #if 0
    //
    // Being conservative, if another function was called, then
    // the instrumentation needs to check that the buffer isn't full
    //
    // Being really conservative every block has a check, this also
    //   requires disabling the dominator tree traversal in the runOnModule routine
    //
    //if (/*containCall == true && */containQueueBuf == false && markOnly == false)
    else if ((B.getTerminator()->getNumSuccessors() != 1 && markOnly == false) ||
             (&B == &(B.getParent()->getEntryBlock())))
    {
        // Since calls terminate basic blocks
        //   These blocks would have only 1 successor
        Value* argsCheck[] = {sbbc};
        debugLog("checkBufferFunction @" << __LINE__);
        
        // calculate the original check counts
        origin_check++;

        hash<BasicBlock*> blockHash{};
        int bb_val = blockHash(&B);
        // the terminator
        Instruction* last = &*B.end();
        if (needCheckAtBlock.find(bb_val) == needCheckAtBlock.end()) {
          // we do not actually need the check
          // by the result of analysis
          if ((&B != &(B.getParent()->getEntryBlock())) &&
            (B.getTerminator()->getNumSuccessors() != 0) &&
            loopExits.find(bb_val) == loopExits.end() &&
            !isa<CallInst>(last)) {
            // no need to check
            // we always add checks on 
            // (1) function entry and exit
            // (2) loop exit
          }
          else {
            Instruction* callChk = CallInst::Create(cct.checkBufferFunction, ArrayRef<Value*>(argsCheck, 1), "", iPt);
            MarkInstAsContechInst(callChk);
            num_checks++;
          }
        }
        else {
          // we need to add check according to the analysis result
          needCheckAtBlock.erase(bb_val);
          Instruction* callChk = CallInst::Create(cct.checkBufferFunction, ArrayRef<Value*>(argsCheck, 1), "", iPt);
          MarkInstAsContechInst(callChk);
          num_checks++;
         }
    }
    else {
        // straight line code
        // need to see whether we need to add check 
         Value* argsCheck[] = {sbbc};
        hash<BasicBlock*> blockHash{};
        int bb_val = blockHash(&B);

        if (needCheckAtBlock.find(bb_val) != needCheckAtBlock.end()) {
          // straight line code and need check according to analysis
          Instruction* callChk = CallInst::Create(cct.checkBufferFunction, ArrayRef<Value*>(argsCheck, 1), "", iPt);
          MarkInstAsContechInst(callChk);
          num_checks++;
        }

    }
    #endif

    // Finally record the information about this basic block
    //  into the CFG structure, so that targets can be matched up
    //  once all basic blocks have been parsed
    cfgInfoMap.insert(pair<BasicBlock*, llvm_basic_block*>(&B, bi));
    if (elideBasicBlockId)
    {
        
    }

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


char Contech::ID = 0;
static RegisterPass<Contech> X("Contech", "Contech Pass", false, false);

