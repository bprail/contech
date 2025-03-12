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
#include "llvm/Analysis/ScalarEvolution.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/Support/CommandLine.h"

#include "BufferCheckAnalysis.h"
#include "Contech.h"
#include "LoopIV.h"

using namespace llvm;
using namespace std;

// ContechState is required to reconstruct the basic block events from the event trace
cl::opt<string> ContechStateFilename("ContechState", cl::desc("File with current Contech state"), cl::value_desc("filename"));

uint64_t tailCount = 0;

namespace llvm {
    DebugLoc defLoc;
#define STORE_AND_LEN(x,y) {x, sizeof(x), y}
#define FUNCTIONS_INSTRUMENT_SIZE 67
// NB Order matters in this array.  Put the most specific function names first, then
//  the more general matches.
    llvm_function_map functionsInstrument[FUNCTIONS_INSTRUMENT_SIZE] = {
                                            // If main has OpenMP regions, the derived functions
                                            //    will begin with main or MAIN__
                                           STORE_AND_LEN("main\0", MAIN),
                                           STORE_AND_LEN("MAIN__\0", MAIN),
                                           STORE_AND_LEN("pthread_create", THREAD_CREATE),
                                           STORE_AND_LEN("pthread_join", THREAD_JOIN),
                                           STORE_AND_LEN("parsec_barrier_wait", BARRIER_WAIT),
                                           STORE_AND_LEN("pthread_barrier_wait",  BARRIER_WAIT),
                                           STORE_AND_LEN("parsec_barrier",  BARRIER),
                                           STORE_AND_LEN("pthread_barrier",  BARRIER),
                                           STORE_AND_LEN("malloc",  MALLOC),
                                           STORE_AND_LEN("xmalloc",  MALLOC),
                                           STORE_AND_LEN("valloc",  MALLOC),
                                           STORE_AND_LEN("memalign",  MALLOC2),
                                           STORE_AND_LEN("operator new",  MALLOC),
                                           STORE_AND_LEN("mmap",  MALLOC2),
                                           STORE_AND_LEN("realloc",  REALLOC),
                                           // Splash2.raytrace has a free_rayinfo, so \0 added
                                           STORE_AND_LEN("free\0",  FREE),
                                           STORE_AND_LEN("operator delete",  FREE),
                                           STORE_AND_LEN("munmap",  FREE),
                                           STORE_AND_LEN("exit",  EXIT),
                                           STORE_AND_LEN("pthread_mutex_init", SYNC_ACQUIRE),
                                           STORE_AND_LEN("pthread_mutex_lock",  SYNC_ACQUIRE),
                                           STORE_AND_LEN("pthread_mutex_trylock",  SYNC_ACQUIRE),
                                           STORE_AND_LEN("pthread_mutex_unlock",  SYNC_RELEASE),
                                           STORE_AND_LEN("pthread_spin_lock",  SYNC_ACQUIRE),
                                           STORE_AND_LEN("pthread_spin_trylock",  SYNC_ACQUIRE),
                                           STORE_AND_LEN("pthread_spin_unlock",  SYNC_RELEASE),
                                           STORE_AND_LEN("pthread_rwlock_rdlock",  SYNC_ACQUIRE),
                                           STORE_AND_LEN("pthread_rwlock_tryrdlock",  SYNC_ACQUIRE),
                                           STORE_AND_LEN("pthread_rwlock_wrlock",  SYNC_ACQUIRE),
                                           STORE_AND_LEN("pthread_rwlock_trywrlock",  SYNC_ACQUIRE),
                                           STORE_AND_LEN("pthread_rwlock_unlock",  SYNC_RELEASE),
                                           STORE_AND_LEN("sem_post",  SYNC_RELEASE),
                                           STORE_AND_LEN("sem_wait",  SYNC_ACQUIRE),
                                           STORE_AND_LEN("sem_trywait",  SYNC_ACQUIRE),
                                           STORE_AND_LEN("_mutex_lock_",  SYNC_ACQUIRE),
                                           STORE_AND_LEN("_mutex_unlock_",  SYNC_RELEASE),
                                           STORE_AND_LEN("pthread_cond_wait",  COND_WAIT),
                                           STORE_AND_LEN("pthread_cond_signal",  COND_SIGNAL),
                                           STORE_AND_LEN("pthread_cond_broadcast",  COND_SIGNAL),
                                           STORE_AND_LEN("GOMP_parallel_start",  OMP_CALL),
                                           STORE_AND_LEN("GOMP_parallel_end",  OMP_END),
                                           STORE_AND_LEN("GOMP_atomic_start",  GLOBAL_SYNC_ACQUIRE),
                                           STORE_AND_LEN("__kmpc_single",  GLOBAL_SYNC_ACQUIRE),
                                           STORE_AND_LEN("GOMP_atomic_end",  GLOBAL_SYNC_RELEASE),
                                           STORE_AND_LEN("__kmpc_end_single",  GLOBAL_SYNC_RELEASE),
                                           STORE_AND_LEN("__kmpc_fork_call",  OMP_FORK),
                                           STORE_AND_LEN("__kmpc_dispatch_next",  OMP_FOR_ITER),
                                           STORE_AND_LEN("__kmpc_barrier",  OMP_BARRIER),
                                           STORE_AND_LEN("__kmpc_cancel_barrier",  OMP_BARRIER),
                                           STORE_AND_LEN("GOMP_barrier",  OMP_BARRIER),
                                           STORE_AND_LEN("__kmpc_omp_task_with_deps",  OMP_TASK_CALL),
                                           STORE_AND_LEN("MPI_Send",  MPI_SEND_BLOCKING),
                                           STORE_AND_LEN("mpi_send_",  MPI_SEND_BLOCKING),
                                           STORE_AND_LEN("MPI_Recv",  MPI_RECV_BLOCKING),
                                           STORE_AND_LEN("mpi_recv_",  MPI_RECV_BLOCKING),
                                           STORE_AND_LEN("MPI_Isend",  MPI_SEND_NONBLOCKING),
                                           STORE_AND_LEN("mpi_isend_",  MPI_SEND_NONBLOCKING),
                                           STORE_AND_LEN("MPI_Irecv",  MPI_RECV_NONBLOCKING),
                                           STORE_AND_LEN("mpi_irecv_",  MPI_RECV_NONBLOCKING),
                                           STORE_AND_LEN("MPI_Bcast",  MPI_BROADCAST),
                                           STORE_AND_LEN("MPI_Barrier",  BARRIER_WAIT),
                                           STORE_AND_LEN("mpi_barrier_",  BARRIER_WAIT),
                                           STORE_AND_LEN("MPI_Wait",  MPI_TRANSFER_WAIT),
                                           STORE_AND_LEN("mpi_wait_",  MPI_TRANSFER_WAIT),
                                           STORE_AND_LEN("__cilkrts_leave_frame",  CILK_FRAME_DESTROY),
                                           STORE_AND_LEN("llvm.eh.sjlj.setjmp",  CILK_FRAME_CREATE),
                                           STORE_AND_LEN("__cilkrts_sync",  CILK_SYNC)};


    ModulePass* createContechPass() { return new Contech(); }
}

Type* Contech::getTypeFromStr(const char ty)
{
    switch(ty)
    {
        case 'v': return cct.voidTy;
        case 'b': return cct.int1Ty;
        case 'c': return cct.int8Ty;
        case 's': return cct.int16Ty;
        case 'i': return cct.int32Ty;
        case 'l': return cct.int64Ty;
        case 't': return cct.pthreadTy;
        case 'p': return cct.voidPtrTy;
        default:
            return NULL;
    }
}

FunctionCallee Contech::getFunction(Module &M, const char* fname, const char* fmt, bool isVarg)
{
    size_t argLen = strlen(fmt);
    assert(argLen > 0);
    Type* retTy = getTypeFromStr(fmt[0]);
    
    if (argLen > 1)
    {
        Type* argTy[argLen - 1];
        
        for (unsigned int i = 1; i < argLen; i++)
        {
            argTy[i - 1] = getTypeFromStr(fmt[i]);
        }
        
        FunctionType* funTy = FunctionType::get(retTy, ArrayRef<Type*>(argTy, (argLen - 1)), isVarg);
        return M.getOrInsertFunction(fname, funTy);
    }
    else
    {
        FunctionType* funVoidVoidTy = FunctionType::get(retTy, isVarg);
        return M.getOrInsertFunction(fname, funVoidVoidTy);
    }
    
    return NULL;
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
    // Get the different integer types required by Contech
    LLVMContext &ctx = M.getContext();
    currentDataLayout = &M.getDataLayout();
    cct.int1Ty = Type::getInt1Ty(ctx);
    cct.int8Ty = Type::getInt8Ty(ctx);
    cct.int16Ty = Type::getInt16Ty(ctx);
    cct.int32Ty = Type::getInt32Ty(ctx);
    cct.int64Ty = Type::getInt64Ty(ctx);
    cct.voidTy = Type::getVoidTy(ctx);
    cct.voidPtrTy = cct.int8Ty->getPointerTo();
    
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

    
    
    FunctionType* funVoidPtrVoidPtrTy;
    Type* funVoidPtrVoidTypes[] = {cct.voidPtrTy};
    funVoidPtrVoidPtrTy = FunctionType::get(cct.voidPtrTy, ArrayRef<Type*>(funVoidPtrVoidTypes, 1), false);

    // v - void, c - int8, s - int16, i - int32, l - int64, t - pthread, p - voidPtr
    //   First type is the return type.  Remaining are arguments.
    cct.getBufPosFunction = getFunction(M, "__ctGetBufferPos", "ip");
    cct.storeBasicBlockFunction = getFunction(M, "__ctStoreBasicBlock", "piipc");
    cct.storeBasicBlockCompFunction = getFunction(M, "__ctStoreBasicBlockComplete", "iiipccc");
    cct.extendPathInfoFunction = getFunction(M, "__ctExtendPathInfo", "ccb");
    cct.storePathInfoFunction = getFunction(M, "__ctStorePathInfo", "vpc");
    cct.storeMemOpFunction = getFunction(M, "__ctStoreMemOp", "vpipcc");
    cct.getBufFunction = getFunction(M, "__ctGetBuffer", "p");
    cct.getBufPtrFunction = getFunction(M, "__ctGetBufferPtr", "pip");
    cct.cilkInitFunction = getFunction(M, "__ctInitCilkSync", "p");
    cct.allocateBufferFunction = getFunction(M, "__ctAllocateLocalBuffer", "v");
    cct.ompPushParentFunction = getFunction(M, "__ctOMPPushParent", "v");
    cct.ompPopParentFunction = getFunction(M, "__ctOMPPopParent", "v");
    cct.ompProcessJoinFunction = getFunction(M, "__ctOMPProcessJoinStack", "v");
    cct.allocateCTidFunction = getFunction(M, "__ctAllocateCTid", "i");
    cct.getThreadNumFunction = getFunction(M, "__ctGetLocalNumber", "i");
    cct.getCurrentTickFunction = getFunction(M, "__ctGetCurrentTick", "l");
    cct.allocateTicketFunction = getFunction(M, "__ctAllocateTicket", "l");
    cct.ctPeekParentIdFunction = getFunction(M, "__ctPeekParent", "i");
    cct.ompGetNestLevelFunction = getFunction(M, "omp_get_level", "i");

    cct.storeSyncFunction = getFunction(M, "__ctStoreSync", "vpiill");
    cct.pthreadExitFunction = getFunction(M, "pthread_exit", "vi");
    cct.ompThreadCreateFunction = getFunction(M, "__ctOMPThreadCreate", "vi");
    cct.ompThreadJoinFunction = getFunction(M, "__ctOMPThreadJoin", "vi");
    cct.ompTaskCreateFunction = getFunction(M, "__ctOMPTaskCreate", "vi");
    cct.checkBufferFunction = getFunction(M, "__ctCheckBufferSize", "vi");
    cct.checkBufferLargeFunction = getFunction(M, "__ctCheckBufferBySize", "vi");
    cct.storeLoopExitFunction = getFunction(M, "__ctStoreLoopExit", "vi");
    cct.storeLoopShortFunction = getFunction(M, "__ctStoreLoopEntryShort", "vsp");
    
    cct.ompGetParentFunction = getFunction(M, "omp_get_ancestor_thread_num", "ii");

    cct.storeLoopEntryFunction = getFunction(M, "__ctStoreLoopEntry", "viiilsp");
    
    cct.queueBufferFunction = getFunction(M, "__ctQueueBuffer", "vc");
    cct.ompTaskJoinFunction = getFunction(M, "__ctOMPTaskJoin", "v");

    cct.storeBarrierFunction = getFunction(M, "__ctStoreBarrier", "vcpl");

    cct.storeThreadInfoFunction = getFunction(M, "__ctAddThreadInfo", "vpi");

    cct.storeMPITransferFunction = getFunction(M, "__ctStoreMPITransfer", "vcciiiiplp");
    cct.storeMPIAllOneFunction = getFunction(M, "__ctStoreMPIAllOne", "vciiipl");
    cct.storeMPIWaitFunction = getFunction(M, "__ctStoreMPIWait", "vpl");

    cct.cilkCreateFunction = getFunction(M, "__ctRecordCilkFrame", "vplii");
    cct.cilkSyncFunction = getFunction(M, "__ctRecordCilkSync", "vp");
    cct.cilkRestoreFunction = getFunction(M, "__ctRestoreCilkFrame", "vp");
    cct.cilkParentFunction = getFunction(M, "__ctCilkPromoteParent", "vp");
    
    cct.writeElideGVEventsFunction =  getFunction(M, "__ctWriteElideGVEvents", "vp");
    Function* f = dyn_cast<Function>(cct.writeElideGVEventsFunction.getCallee());
    if (f != NULL) 
    {
        Instruction* iPt;
        if (f->empty())
        {
            BasicBlock* bbEntry = BasicBlock::Create(M.getContext(), "", f, NULL);
            iPt = ReturnInst::Create(M.getContext(), bbEntry);
        }
    }

    cct.storeGVEventFunction = getFunction(M, "__ctStoreGVEvent", "vppi");
    
    cct.storeThreadCreateFunction = getFunction(M, "__ctStoreThreadCreate", "vill");
    cct.storeThreadJoinFunction = getFunction(M, "__ctStoreThreadJoin", "vtl");

    cct.storeMemoryEventFunction = getFunction(M, "__ctStoreMemoryEvent", "vctp");
    cct.storeBulkMemoryOpFunction = getFunction(M, "__ctStoreBulkMemoryEvent", "vtpp");

    cct.ompStoreInOutDepsFunction = getFunction(M, "__ctOMPStoreInOutDeps", "vptii");
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

void Contech::getAnalysisUsage(AnalysisUsage& AU) const {		
    AU.setPreservesAll();		
    AU.addRequired<ScalarEvolutionWrapperPass>();		
    AU.addRequired<LoopInfoWrapperPass>();		
    AU.addPreserved<LoopInfoWrapperPass>();
    AU.addRequired<DominatorTreeWrapperPass>();
    //AU.addRequired<LoopInfoWrapperPass>();  //in this order		
}		

LoopInfo* Contech::getAnalysisLoopInfo(Function& F)		
{		
    return &getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();		
}		
		
ScalarEvolution* Contech::getAnalysisSCEV(Function& F)		
{		
    return &getAnalysis<ScalarEvolutionWrapperPass>(F).getSE();		
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

    map<int, llvm_inst_block> fullCost;
    vector<Function*> instFuncs;
    for (Module::iterator F = M.begin(), FE = M.end(); F != FE; ++F) 
    {
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
            // This invalidates F->getName(), ie possibly fmn is invalid
            //F->setName(Twine("ct_orig_main"));
            inMain = true;
        }
        // Add other functions that Contech should not instrument here
        // NB Main is checked above and is special cased
        else if (classifyFunctionName(fmn) != NONE ||
                 __ctStrCmp(fmn, "__ct") == 0)
        {
            if (__ctStrCmp(fmn, "__ct") != 0)
            {
                errs() << "SKIP: " << fmn << "\n";
            }
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
        
        bool setDebugLocForF = true;
        // "Normalize" every basic block to have only one function call in it
        for (Function::iterator B = F->begin(), BE = F->end(); B != BE; ) 
        {
            BasicBlock &pB = *B;
            CallInst *ci;
            int status = 0;
            
            if (setDebugLocForF) 
            {    
                defLoc = pB.getFirstNonPHIOrDbgOrLifetime()->getDebugLoc();
                setDebugLocForF = false;
            }
            
            //
            // It would be possible to detect empty blocks and remove some;
            //   however, those blocks can be used for selecting values for
            //   the PHI nodes.  They are generally infrequent and only
            //   have minimal trace reduction.
            //
            
            if (internalSplitOnCall(pB, &ci, &status) == false)
            {
                ++B;
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
        
#ifndef DISABLE_MEM
        /*LoopIV* liv = new LoopIV(this);
        liv->runOnFunction(*F);
        vector<llvm_loopiv_block*> temp = liv->getLoopMemoryOps();
        loopMemOps.clear();
        for (unsigned int cnt = 0; cnt < temp.size(); cnt++) 
        {
            if (temp[cnt]->canElide)
            {
                loopMemOps[temp[cnt]->memOp] = cnt;
            }
        }
        
        LoopMemoryOps.clear();
        LoopMemoryOps.insert(LoopMemoryOps.end(), 
                             temp.begin(), temp.end());
        delete liv;*/
#endif
        
        
        // static analysis
        Function* pF = &*F;
        DT = &getAnalysis<DominatorTreeWrapperPass>(*pF).getDomTree();
        
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
        // Now instrument each basic block in the function
        for (Function::iterator B = F->begin(), BE = F->end(); B != BE; ++B) 
        {
            BasicBlock &pB = *B;
            
            internalRunOnBasicBlock(pB, M, bb_count, fmn, 
                                    costPerBlock, num_checks, origin_checks);
            bb_count++;
        }

        // run the check analysis
        BufferCheckAnalysis bufferCheckAnalysis{costPerBlock,
                                                loopExits,
                                                loopBelong,
                                                loopEntry,
                                                1024};

        // run analysis
        bufferCheckAnalysis.runAnalysis(pF);
        // see the analysis result
        map<int, bool> needCheckAtBlock{ bufferCheckAnalysis.getNeedCheckAtBlock() };
        
        hash<BasicBlock*> blockHash{};
        
        for (Function::iterator B = F->begin(), BE = F->end(); B != BE; ++B) 
        {
            int bb_val = blockHash(&*B);
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
            lib->second.insertPoint = callChk;
            num_checks++;
        }
        
        //errs() << F->getName().str() << "," << num_checks << "," << origin_checks << "\n" ;
        
        // Apply Loop entry / exits
        for (auto it = loopInfoTrack.begin(), et = loopInfoTrack.end(); it != et; ++it)
        {
            pllvm_loop_track llt = it->second;
            uint32_t bbid = cfgInfoMap[it->first]->id;
            //errs() << "L: " << bbid << " " << *(it->first) << "\n";
            Instruction* iPt = it->first->getTerminator();
            Constant* cbbid = ConstantInt::get(cct.int32Ty, bbid);
            Constant* cstep = ConstantInt::get(cct.int32Ty, llt->stepIV);
            Value* startValue = NULL;
            Constant* stepID = NULL;
            
            if (llt->startIV != NULL)
                startValue = castSupport(cct.int64Ty, llt->startIV, iPt);
            else
                startValue = ConstantInt::get(cct.int64Ty, 0);
            stepID = ConstantInt::get(cct.int32Ty, cfgInfoMap[llt->stepBlock]->id);
            
            cfgInfoMap[it->first]->stepIV = llt->stepIV;
            cfgInfoMap[it->first]->stepBlock = cfgInfoMap[llt->stepBlock]->id;
            cfgInfoMap[it->first]->isLoopEntry = true;
            
            // Insert a call per elided loop memop
            uint16_t i = 0;
            //errs() << "Loop at " << cfgInfoMap[it->first]->id << " with " << llt->baseAddr.size() << "\n";
            for (auto mit = llt->baseAddr.begin(), met = llt->baseAddr.end(); mit != met; ++mit)
            {
                Constant* opPos = ConstantInt::get(cct.int16Ty, i);
                Value* voidAddr = NULL;
                
                auto ac = llt->compMap.find(i);
                if (ac == llt->compMap.end())
                {
                    Value* v = *mit;
                    if (*mit == NULL) v = ConstantInt::get(cct.int64Ty, 0);
                    voidAddr = castSupport(cct.voidPtrTy, v, iPt);
                }
                else
                {
                    Value* bVal = castSupport(cct.int64Ty, *mit, iPt);
                    
                    for (auto acit = ac->second.begin(), acet = ac->second.end(); acit != acet; ++acit)
                    {
                        Value* val = acit->first;
                        int scale = acit->second;
                        Value* ival = castSupport(cct.int64Ty, val, iPt);
                        Constant* cscale = ConstantInt::get(cct.int64Ty, scale);
                        
                        Instruction* imul = BinaryOperator::Create(Instruction::Mul, ival, cscale, "", iPt);
                        
                        bVal = BinaryOperator::Create(Instruction::Add, bVal, imul, "", iPt);
                    }
                    
                    voidAddr = castSupport(cct.voidPtrTy, bVal, iPt);
                }
                
                if (i == 0)
                {
                    //errs() << "Entry - " << bbid << "\n";
                    Value* argsEntry[] = {cbbid, cstep, stepID, startValue, opPos, voidAddr};
                    debugLog("storeLoopEntryFunction @" << __LINE__);
                    Instruction* loopEntry = CallInst::Create(cct.storeLoopEntryFunction, ArrayRef<Value*>(argsEntry, 6), "", iPt);
                    MarkInstAsContechInst(loopEntry);
                }
                else
                {
                    Value* argsShort[] = {opPos, voidAddr};
                    debugLog("storeLoopShortFunction @" << __LINE__);
                    Instruction* loopEntry = CallInst::Create(cct.storeLoopShortFunction, ArrayRef<Value*>(argsShort, 2), "", iPt);
                    MarkInstAsContechInst(loopEntry);
                }
                i++;
            }
            
            // Insert one exit call per exit block
            for (auto eit = llt->exitBlocks.begin(), eet = llt->exitBlocks.end(); eit != eet; ++eit)
            {
                // This is actually set by exit edge, not block, so duplicates may exist.
                bool isDupBlock = false;
                for (auto skip = (eit + 1); skip != eet; ++skip)
                {
                    if (*skip == *eit) {isDupBlock = true; break;}
                }
                if (isDupBlock == true) continue;
                
                //errs() << "Exit - " << cfgInfoMap[*eit]->id << "\t" << i << "\n";
                
                Value* argsExit[] = {cbbid};
                //
                // When inserting at the start of a basic block, instrumentation has to come
                //   after several types of instructions, and unfortunately, there is no
                //   routine to also skip the landing pad instruction(s) from exception handling.
                //
                auto insertPoint = convertInstToIter((*eit)->getFirstNonPHIOrDbgOrLifetime());
                while (dyn_cast<LandingPadInst>(convertIterToInst(insertPoint)) != NULL)
                {
                    ++insertPoint;
                }
                
                Instruction* loopExit = CallInst::Create(cct.storeLoopExitFunction, 
                                                         ArrayRef<Value*>(argsExit, 1), 
                                                         "", 
                                                         convertIterToInst(insertPoint));
                MarkInstAsContechInst(loopExit);
            }
            
            delete llt;
        }
        loopInfoTrack.clear();
        
        fullCost.insert(costPerBlock.begin(), costPerBlock.end());
        instFuncs.push_back(&*F);
        
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

    int pathID = bb_count;
    // Delay the chainBufferCalls until all BasicBlocks are processed
    //   This way all path IDs follow the BBIDs.
    for (auto it = instFuncs.begin(), et = instFuncs.end(); it != et; ++it) 
    {
        Function* pF = *it;
        DT = &getAnalysis<DominatorTreeWrapperPass>(*pF).getDomTree();
        pathID = chainBufferCalls(*it, fullCost, pathID);
        crossBlockCalculation(*it, fullCost);
    }

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
        delete[] buffer;
    }
    //contechStateFile->seekp(0, ios_base::end);

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
            int32_t nId = bi->second->next_id;
            contechStateFile->write((char*)&nId, sizeof(int32_t));
            
            if (nId != -1)
            {
                // HACK
                nId = 0;
                contechStateFile->write((char*)&nId, sizeof(int32_t));
            }
            else
            {
                size_t tPaths = bi->second->path_ids.size();
                contechStateFile->write((char*)&tPaths, sizeof(int32_t));
                if (tPaths > 0)
                {
                    for (auto it = bi->second->path_ids.begin(), et = bi->second->path_ids.end();
                         it != et; ++it)
                    {
                        contechStateFile->write((char*)&(*it).first, sizeof(int32_t));
                        contechStateFile->write((char*)&(*it).second, sizeof(int32_t));
                    }
                }
            }
            
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

            bool loopEntry = bi->second->isLoopEntry;
            contechStateFile->write((char*)&loopEntry, sizeof(bool));
            if (loopEntry)
            {
                contechStateFile->write((char*)&bi->second->stepBlock, sizeof(unsigned int));
                contechStateFile->write((char*)&bi->second->stepIV, sizeof(int));
            }
            
            int presvCount = bi->second->crossOpCount;
            BasicBlock* bbEntry = bi->second->isFuncExit;
            int funcExit = -1;
            if (bbEntry != NULL)
            {
                funcExit = cfgInfoMap[bbEntry]->id;
            }
            contechStateFile->write((char*)&presvCount, sizeof(int));
            contechStateFile->write((char*)&funcExit, sizeof(int));
            
            // Number of memory operations
            contechStateFile->write((char*)&bi->second->len, sizeof(unsigned int));

            while (t != NULL)
            {
                pllvm_mem_op tn = t->next;
                char memFlags = (t->isDep)?BBI_FLAG_MEM_DUP:0x0;
                memFlags |= (t->isWrite)?0x1:0x0;
                memFlags |= (t->isCrossPresv)?BBI_FLAG_MEM_PRESV:0x0;
                if (t->isDep)
                {
                    memFlags |= (t->isGlobal)?BBI_FLAG_MEM_GV:0x0;
                    memFlags |= (t->isLoopElide)?BBI_FLAG_MEM_LOOP:0x0;
                }

                contechStateFile->write((char*)&memFlags, sizeof(char));
                contechStateFile->write((char*)&t->size, sizeof(char));
                // Add optional dep mem op info
                if (t->isDep)
                {
                    assert((memFlags & BBI_FLAG_MEM_DUP) == BBI_FLAG_MEM_DUP);
                    if (t->isLoopElide)
                    {
                        uint32_t loopHeaderId = cfgInfoMap[t->loopHeaderId]->id;
                        contechStateFile->write((char*)&t->loopIVSize, sizeof(int));
                        contechStateFile->write((char*)&loopHeaderId, sizeof(uint32_t));
                        contechStateFile->write((char*)&t->loopMemOp, sizeof(uint16_t));
                        contechStateFile->write((char*)&t->depMemOpDelta, sizeof(int64_t));
                    }
                    else if (t->isCrossPresv)
                    {
                        contechStateFile->write((char*)&t->crossBlockID, sizeof(uint16_t));
                        contechStateFile->write((char*)&t->depMemOpDelta, sizeof(int64_t));
                    }
                    else
                    {
                        contechStateFile->write((char*)&t->depMemOp, sizeof(uint16_t));
                        contechStateFile->write((char*)&t->depMemOpDelta, sizeof(int64_t));
                    }
                }
                else if (t->isCrossPresv)
                {
                    contechStateFile->write((char*)&t->crossBlockID, sizeof(uint16_t));
                }
                
                delete (t);
                t = tn;
            }
            wcount++;
            // Cannot delete bi->second here, as the ID may be referenced by a later
            //   block when computing loop info
            //delete bi->second;
        }

        evTy = ct_event_path_info;
        for (auto it = pathInfoMap.begin(), et = pathInfoMap.end(); it != et; ++it)
        {
            contechStateFile->write((char*)&evTy, sizeof(unsigned char));
            
            // PATHID, STARTID, DEPTH, IDs of Cond
            //   CONDID[0] is startID, also it->first is block*
            contechStateFile->write((char*) &it->second.id, sizeof(uint32_t));
            
            uint32_t startID = cfgInfoMap[it->first]->id;
            contechStateFile->write((char*)&startID, sizeof(uint32_t));
            
            int32_t pathCount = it->second.pathDepth;
            contechStateFile->write((char*)&pathCount, sizeof(int32_t));
        }        
    }
    //errs() << "Wrote: " << wcount << " basic blocks\n";
    cfgInfoMap.clear();
    contechStateFile->close();
    delete contechStateFile;

    errs() << "Tail Dup Count: " << tailCount << "\n";
    
    return true;
}

// is_loop_computable
// 
// Checks if address can be calculated as const base + f(i,j,..) 
// imports data from loop pass 
//
int Contech::is_loop_computable(Instruction* memI, int* offset)
{
    *offset = 0;
    auto elem = loopMemOps.find(memI);
    if (elem == loopMemOps.end()) return -1;
    
    return elem->second;
}

// returns size in bytes
unsigned int Contech::getSizeofType(Type* t)
{
    unsigned int r = t->getPrimitiveSizeInBits();
    if (r > 0) return (r + 7) / 8;  //Round up to the nearest byte
    else if (t->isPointerTy()) { return cct.pthreadSize;}
    else if (t->isPtrOrPtrVectorTy()) 
    { // TODO: Is there a vector subtype ?
        errs() << *t << " is pointer vector\n";
        return t->getStructNumElements() * cct.pthreadSize;
    }
    else if (t->isVectorTy()) { return t->getStructNumElements() * t->getScalarSizeInBits();}
    else if (t->isArrayTy()) { /*errs() << *t << " is array\n";*/}
    else if (t->isStructTy()) { /*errs() << *t << " is struct\n";*/}

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
                    // Contech did not insert this branch, but it should claim it, so the instruction
                    //   is not later removed.
                    if (bi->isUnconditional()) 
                    {
                        *st = 4;
                        MarkInstAsContechInst(B.getTerminator()); 
                        return false;
                    }
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
bool Contech::internalRunOnBasicBlock(BasicBlock &B,  Module &M, int bbid, const char* fnName, 
                                      map<int, llvm_inst_block>& costOfBlock, int& num_checks, int& origin_check)
{
    Instruction* iPt = B.getTerminator();
    vector<pllvm_mem_op> opsInBlock;
    unsigned int memOpCount = 0, memOpGVElide = 0;
    Instruction* aPhi ;//= convertIterToInst(B.begin());
    bool getNextI = false;
    bool containQueueBuf = false;
    bool hasUninstCall = true; // Any call is uninst
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
    map<Instruction*, int64_t> dupMemOpOff;
    map<Value*, unsigned short> dupMemOpPos;
    map<Instruction*, int> loopIVOp;


    //errs() << "BB: " << bbid << "\n";
    //errs() << *&B << "\n";
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
            int64_t addrOffset = 0;
            Value* addrSimilar = findSimilarMemoryInstExt(li, li->getPointerOperand(), &addrOffset);

            if (addrSimilar != NULL)
            {
                // errs() << *addrSimilar << " ?=? " << *li << "\t" << addrOffset << "\n";
                dupMemOps[li] = addrSimilar;
                dupMemOpOff[li] = addrOffset;
                dupMemOpPos[addrSimilar] = 0;
            }
            else if ((addrOffset = is_loop_computable(li, (int*)&addrOffset)) != -1)
            {
                loopIVOp[li] = addrOffset;
                memOpCount ++;
            }
            else
            {
                memOpCount ++;
            }
        }
        else if (StoreInst *si = dyn_cast<StoreInst>(&*I))
        {
            int64_t addrOffset = 0;
            Value* addrSimilar = findSimilarMemoryInstExt(si, si->getPointerOperand(), &addrOffset);

            if (addrSimilar != NULL)
            {
                // errs() << *addrSimilar << " ?=? " << *si << "\t" << addrOffset << "\n";
                dupMemOps[si] = addrSimilar;
                dupMemOpOff[si] = addrOffset;
                dupMemOpPos[addrSimilar] = 0;
            }
            else if ((addrOffset = is_loop_computable(si, (int*)&addrOffset)) != -1)
            {
                loopIVOp[si] = addrOffset; 
                memOpCount ++;                
            }
            else
            {
                memOpCount ++;
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

    llvm_basic_block* bi = new llvm_basic_block;
    if (bi == NULL)
    {
        errs() << "Cannot record CFG in Contech\n";
        return true;
    }
    
    //
    // Large blocks cannot have their IDs elided.
    // TODO: permanent value or different approach to checks
    //
    if (memOpCount < 160) 
    {
        elideBasicBlockId = checkAndApplyElideId(&B, bbid, costOfBlock);
    }

    bi->id = bbid;
    bi->next_id = -1;
    bi->path_ids.clear();
    bi->first_op = NULL;
    bi->containGlobalAccess = false;
    bi->containAtomic = false;
    bi->containCall = false;
    bi->isLoopEntry = false;
    bi->isFuncExit = NULL;
    bi->lineNum = lineNum;
    bi->numIROps = numIROps;
    bi->fnName.assign(fnName);
    bi->fileName = fileName;
    bi->fileNameSize = fileNameSize;
    //bi->fileName = B.getDebugLoc().getScope().getFilename();//M.getModuleIdentifier().data();
    bi->critPathLen = getCriticalPathLen(B);

    //errs() << "Basic Block - " << bbid << " -- " << memOpCount << "\n";
    //debugLog("checkBufferFunction @" << __LINE__);
    //CallInst::Create(checkBufferFunction, "", aPhi);
    Constant* llvm_bbid;
    Constant* llvm_nops = NULL;
    CallInst* sbb;
    CallInst* sbbc = NULL;
    unsigned int memOpPos = 0;

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
        Value* argsBB[] = {llvm_bbid, bufPos, baseBufValue, cElide};
        debugLog("storeBasicBlockFunction for BBID: " << bbid << " @" << __LINE__);
        sbb = CallInst::Create(cct.storeBasicBlockFunction,
                               ArrayRef<Value*>(argsBB, 4),
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
        tMemOp->isGlobal = false;
        tMemOp->isLoopElide = false;
        tMemOp->isCrossPresv = false;
        tMemOp->isTSC = true;
        tMemOp->depMemOp = 0;
        tMemOp->depMemOpDelta = 0;

        Constant* cPos = ConstantInt::get(cct.int32Ty, memOpPos);
        Constant* cPath = ConstantInt::get(cct.int8Ty, 0);
        Value* addrI = castSupport(cct.voidPtrTy, stTick, aPhi);
        Value* argsMO[] = {addrI, cPos, sbb, cElide, cPath};
        debugLog("storeMemOpFunction @" << __LINE__);
        CallInst* smo = CallInst::Create(cct.storeMemOpFunction, ArrayRef<Value*>(argsMO, 5), "", aPhi);
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
        Instruction* fenI = new FenceInst(M.getContext(), AtomicOrdering::Acquire, llvm::SyncScope::SingleThread, bufV);
        MarkInstAsContechInst(fenI);
    }


    bool hasInstAllMemOps = false;
    for (BasicBlock::iterator I = B.begin(), E = B.end(); I != E; ++I)
    {

        // After all of the known memOps have been instrumented, close out the basic
        //   block event based on the number of memOps
        if (hasInstAllMemOps == false && memOpPos == memOpCount)
        {
            Value* cElide = ConstantInt::get(cct.int8Ty, elideBasicBlockId);
            Value* skipStore = ConstantInt::get(cct.int8Ty, 0);
            llvm_nops = ConstantInt::get(cct.int32Ty, memOpCount);
            Constant* cPathInfo = ConstantInt::get(cct.int8Ty, 0);
            Value* argsBBc[] = {llvm_nops, basePosValue, baseBufValue, cElide, skipStore, cPathInfo};
            #ifdef TSC_IN_BB
            if (memOpCount == 1)
            #else
            if (memOpCount == 0)
            #endif
            {
                debugLog("storeBasicBlockCompFunction @" << __LINE__);
                sbbc = CallInst::Create(cct.storeBasicBlockCompFunction, ArrayRef<Value*>(argsBBc, 6), "", aPhi);
                MarkInstAsContechInst(sbbc);

                Instruction* fenI = new FenceInst(M.getContext(), AtomicOrdering::Release, llvm::SyncScope::SingleThread, aPhi);
                MarkInstAsContechInst(fenI);
                iPt = aPhi;
            }
            else
            {
                debugLog("storeBasicBlockCompFunction @" << __LINE__);
                sbbc = CallInst::Create(cct.storeBasicBlockCompFunction, ArrayRef<Value*>(argsBBc, 6), "", convertIterToInst(I));
                MarkInstAsContechInst(sbbc);

                Instruction* fenI = new FenceInst(M.getContext(), AtomicOrdering::Release, llvm::SyncScope::SingleThread, convertIterToInst(I));
                MarkInstAsContechInst(fenI);
                iPt = convertIterToInst(I);
            }
            sbbc->getCalledFunction()->addFnAttr( ALWAYS_INLINE);
            
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

        // <result> = load [volatile] <ty>* <pointer>[, align <alignment>][, !nontemporal !<index>][, !invariant.load !<index>]
        // Load and store are identical except the cIsWrite is set accordingly.
        //
        if (LoadInst *li = dyn_cast<LoadInst>(&*I))
        {
            pllvm_mem_op tMemOp = NULL;

            if (dupMemOps.find(li) != dupMemOps.end())
            {
                tMemOp = insertMemOp(NULL, li->getPointerOperand(), false, memOpPos, posValue, elideBasicBlockId, M, loopIVOp);
                
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
                tMemOp = insertMemOp(li, li->getPointerOperand(), false, memOpPos, posValue, elideBasicBlockId, M, loopIVOp);
                if (tMemOp->isGlobal && tMemOp->isDep)
                {
                    memOpCount--;
                    memOpGVElide++;
                }
                else if (tMemOp->isLoopElide && tMemOp->isDep)
                {
                    auto lis = LoopMemoryOps[loopIVOp[li]];
                    lis->wasElide = true;
                    tMemOp->loopHeaderId = lis->headerBlock;
                    
                    addToLoopTrack(lis, tMemOp->loopHeaderId, li, li->getPointerOperand(), 
                                   &tMemOp->loopMemOp, &tMemOp->depMemOpDelta, &tMemOp->loopIVSize);
                    
                    memOpGVElide++;
                    memOpCount--;
                    // memOpPos ++;
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

            tMemOp->addr = li->getPointerOperand();
            
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
                tMemOp = insertMemOp(NULL, si->getPointerOperand(), true, memOpPos, posValue, elideBasicBlockId, M, loopIVOp);
                
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
                tMemOp = insertMemOp(si, si->getPointerOperand(), true, memOpPos, posValue, elideBasicBlockId, M, loopIVOp);
                if (tMemOp->isGlobal && tMemOp->isDep)
                {
                    memOpCount--;
                    memOpGVElide++;
                }
                else if (tMemOp->isLoopElide)
                {
                    auto lis = LoopMemoryOps[loopIVOp[si]];
                    lis->wasElide = true;
                    tMemOp->loopHeaderId = lis->headerBlock;
                    
                    addToLoopTrack(lis, tMemOp->loopHeaderId, si, si->getPointerOperand(), 
                                   &tMemOp->loopMemOp, &tMemOp->depMemOpDelta, &tMemOp->loopIVSize);
                    
                    memOpGVElide++;
                    memOpCount--;
                    // memOpPos ++;
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

            tMemOp->addr = si->getPointerOperand();
            
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
                                                 I,
                                                 iPt,
                                                 bi,
                                                 &cct,
                                                 this,
                                                 M);
            if (huc == false) hasUninstCall = false;
        }
        else if (dyn_cast<ReturnInst>(&*I) != NULL ||
                 dyn_cast<CatchReturnInst>(&*I) != NULL ||
                 dyn_cast<CleanupReturnInst>(&*I) != NULL)
        {
            Function* f = B.getParent();
            BasicBlock* bbEntry = &f->getEntryBlock();
            bi->isFuncExit = bbEntry;//cfgInfoMap[(bbEntry)]->id;
        }
    }

    // There could be multiple call instructions in the block, due to intrinsics and
    //   debug "calls".  If any are real calls, then the block contains a call.
    //   If it has 
    bi->containCall = (bi->containCall)?true:(!hasUninstCall);
    bi->len = memOpCount + dupMemOps.size() + memOpGVElide;
    
    {
        hash<BasicBlock*> blockHash{};
        int bb_val = blockHash(&B);
        llvm_inst_block lib;
        lib.cost = memOpCount * 6 + ((elideBasicBlockId == true)? 0 : 3) 
                             + ((hasUninstCall == false) ? 64 : 0);
        lib.insertPoint = iPt;
        lib.posValue = sbbc;
        
        if (sbbc == NULL)
        {
            errs() << memOpCount << " > " << memOpPos << "\n";
            errs() << "In: " << bbid << "\n";
            errs() << B << "\n";
            assert(0);
        }
        
        lib.hasCheck = false;
        lib.hasElide = elideBasicBlockId;
        lib.preElide = false;
        lib.containQueueCall = containQueueBuf;
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

    // Finally record the information about this basic block
    //  into the CFG structure, so that targets can be matched up
    //  once all basic blocks have been parsed
    cfgInfoMap.insert(pair<BasicBlock*, llvm_basic_block*>(&B, bi));
    
    debugLog("Return from BBID: " << bbid);

    return true;
}

char Contech::ID = 0;
static RegisterPass<Contech> X("Contech", "Contech Pass", false, false);
