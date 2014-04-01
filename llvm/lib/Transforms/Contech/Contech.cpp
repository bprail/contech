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
#include "llvm/IR/Instructions.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Attributes.h"
#define ALWAYS_INLINE (Attribute::AttrKind::AlwaysInline)
#else
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Type.h"
#include "llvm/Module.h"
#include "llvm/LLVMContext.h"
#include "llvm/Metadata.h"
#include "llvm/Function.h"
#include "llvm/Attributes.h"
#define ALWAYS_INLINE (Attributes::AttrVal::AlwaysInline)
#endif
#endif
#include "llvm/Support/InstIterator.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Pass.h"
#include "llvm/Support/GetElementPtrTypeIterator.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/Dominators.h"
#include <map>
#include <set>
#include <vector>
#include <iostream>
#include <fstream>
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/CommandLine.h"
#include <cxxabi.h>


#include <ct_event_st.h>
#include "Contech.h"
using namespace llvm;
using namespace std;

#define __ctStrCmp(x, y) strncmp(x, y, sizeof(y) - 1)

//#define DEBUG_PRINT_CALLINST
#ifdef DEBUG_PRINT_CALLINST
    #define debugLog(s) errs() << s << "\n"
#else
    #define debugLog(s)
#endif
//#define SPLIT_DEBUG

map<BasicBlock*, llvm_basic_block*> cfgInfoMap;
cl::opt<string> ContechCFGFilename("ContechCFG", cl::desc("File to write Contech's CFG"), cl::value_desc("filename"));
cl::opt<string> ContechStateFilename("ContechState", cl::desc("File with current Contech state"), cl::value_desc("filename"));
cl::opt<bool> ContechMarkFrontend("ContechMarkFE", cl::desc("Generate a minimal marked output"));
cl::opt<bool> ContechMinimal("ContechMinimal", cl::desc("Generate a minimally instrumented output"));

namespace llvm {
#define STORE_AND_LEN(x) x, sizeof(x)
#define FUNCTIONS_INSTRUMENT_SIZE 23
// NB Order matters in this array.  Put the most specific function names first, then 
//  the more general matches.
    llvm_function_map functionsInstrument[FUNCTIONS_INSTRUMENT_SIZE] = {
                                           {STORE_AND_LEN("main"), MAIN},
                                           {STORE_AND_LEN("pthread_create"), THREAD_CREATE},
                                           {STORE_AND_LEN("pthread_join"), THREAD_JOIN},
                                           {STORE_AND_LEN("parsec_barrier_wait"), BARRIER_WAIT},
                                           {STORE_AND_LEN("pthread_barrier_wait"), BARRIER_WAIT},
                                           {STORE_AND_LEN("parsec_barrier"), BARRIER},
                                           {STORE_AND_LEN("pthread_barrier"), BARRIER},
                                           {STORE_AND_LEN("malloc"), MALLOC},
                                           {STORE_AND_LEN("operator new"), MALLOC},
                                           // Splash2.raytrace has a free_rayinfo, so \0 added
                                           {STORE_AND_LEN("free\0"), FREE},
                                           {STORE_AND_LEN("operator delete"),FREE},
                                           {STORE_AND_LEN("exit"), EXIT},
                                           {STORE_AND_LEN("pthread_mutex_lock"), SYNC_ACQUIRE},
                                           {STORE_AND_LEN("pthread_mutex_trylock"), SYNC_ACQUIRE},
                                           {STORE_AND_LEN("pthread_mutex_unlock"), SYNC_RELEASE},
                                           {STORE_AND_LEN("_mutex_lock_"), SYNC_ACQUIRE},
                                           {STORE_AND_LEN("_mutex_unlock_"), SYNC_RELEASE},
                                           {STORE_AND_LEN("pthread_cond_wait"), COND_WAIT},
                                           {STORE_AND_LEN("pthread_cond_signal"), COND_SIGNAL},
                                           {STORE_AND_LEN("pthread_cond_broadcast"), COND_SIGNAL},
                                           {STORE_AND_LEN("__kmpc_fork_call"), OMP_FORK},
                                           {STORE_AND_LEN("__kmpc_dispatch_next"), OMP_FOR_ITER},
                                           {STORE_AND_LEN("__kmpc_barrier"), OMP_BARRIER}};

    //
    // Contech - First record every load or store in a program
    //
    class Contech : public ModulePass {
    public:
        static char ID; // Pass identification, replacement for typeid
        Constant* storeBasicBlockFunction;
        Constant* storeBasicBlockCompFunction;
        Constant* storeMemOpFunction;
        Constant* threadInitFunction;
        Constant* allocateBufferFunction;
        Constant* checkBufferFunction;
        Constant* storeThreadCreateFunction;
        Constant* storeSyncFunction;
        Constant* storeMemoryEventFunction;
        Constant* queueBufferFunction;
        Constant* storeBarrierFunction;
        Constant* allocateCTidFunction;
        Constant* getThreadNumFunction;
        Constant* storeThreadJoinFunction;
        Constant* storeThreadInfoFunction;
        Constant* storeBulkMemoryOpFunction;
        Constant* getCurrentTickFunction;
        Constant* createThreadActualFunction;
        
        Constant* storeBasicBlockMarkFunction;
        Constant* storeMemReadMarkFunction;
        Constant* storeMemWriteMarkFunction;

        Constant* ompThreadCreateFunction;
        Constant* ompThreadJoinFunction;
        Constant* ompTaskCreateFunction;
        Constant* ompTaskJoinFunction;
        Constant* ompPushParentFunction;
        Constant* ompPopParentFunction;
        
        Constant* ompGetParentFunction;
        
        Constant* pthreadExitFunction;
        GlobalVariable* threadLocalNumber;
        Type* int8Ty;
        Type* int32Ty;
        Type* voidTy;
        Type* voidPtrTy;
        Type* int64Ty;
        Type* pthreadTy;
        Type* threadArgsTy;  // needed when wrapping pthread_create
        ofstream*     contechCFGFile;
        //fstream*      contechStateFile;
        std::set<Function*> contechAddedFunctions;
        std::set<Function*> ompMicroTaskFunctions;

        Contech() : ModulePass(ID) {
        }
        
        virtual bool doInitialization(Module &M);
        virtual bool runOnModule(Module &M);
        virtual bool internalRunOnBasicBlock(BasicBlock &B,  Module &M, int bbid, bool markOnly);
        virtual bool internalSplitOnCall(BasicBlock &B, CallInst**, int*);
        void addCheckAfterPhi(BasicBlock* B);
        void internalAddAllocate(BasicBlock& B);
        pllvm_mem_op insertMemOp(Instruction* li, Value* addr, bool isWrite, unsigned int memOpPos, Value*);
        unsigned int getSizeofType(Type*);
        unsigned int getSimpleLog(unsigned int);
        Function* createMicroTaskWrap(Function* ompMicroTask, Module &M);
        
        virtual void getAnalysisUsage(AnalysisUsage &AU) const {
            AU.addRequired<DominatorTree>();
            AU.addRequired<PostDominatorTree>();
        }
    };
    ModulePass* createContechPass() { return new Contech(); }
}    
    //
    // Create any globals requied for this module
    //
    bool Contech::doInitialization(Module &M)
    {
        // Function types are named fun(Return type)(arg1 ... argN)Ty
        FunctionType* funVoidPtrI32Ty;
        FunctionType* funVoidVoidPtrI32VoidPtrTy;
        FunctionType* funVoidVoidPtrI32I32I64Ty;
        FunctionType* funVoidPtrVoidPtrTy;
        FunctionType* funVoidPtrVoidTy;
        FunctionType* funVoidVoidTy;
        FunctionType* funVoidI8I64VoidPtrTy;
        FunctionType* funVoidI8Ty;
        FunctionType* funVoidI32Ty;
        FunctionType* funVoidI8VoidPtrI64Ty;
        FunctionType* funVoidVoidPtrI32Ty;
        FunctionType* funVoidI64I64Ty;
        FunctionType* funI8I32Ty;
        FunctionType* funI32I32Ty;

        LLVMContext &ctx = M.getContext();
        int8Ty = Type::getInt8Ty(ctx);
        int32Ty = Type::getInt32Ty(ctx);
        int64Ty = Type::getInt64Ty(ctx);
        voidTy = Type::getVoidTy(ctx);
        voidPtrTy = int8Ty->getPointerTo();

        Type* funVoidPtrVoidTypes[] = {voidPtrTy};
        funVoidPtrVoidTy = FunctionType::get(voidPtrTy, ArrayRef<Type*>(funVoidPtrVoidTypes, 1), false);

        Type* threadStructTypes[] = {static_cast<Type *>(funVoidPtrVoidTy)->getPointerTo(), voidPtrTy, int32Ty, int32Ty};
        threadArgsTy = StructType::create(ArrayRef<Type*>(threadStructTypes, 4), "contech_thread_create", false);
        
        Type* argsBB[] = {int32Ty};
        funVoidPtrI32Ty = FunctionType::get(voidPtrTy, ArrayRef<Type*>(argsBB, 1), false);
        storeBasicBlockFunction = M.getOrInsertFunction("__ctStoreBasicBlock", funVoidPtrI32Ty);
        Type* argsMO[] = {voidPtrTy, int32Ty, voidPtrTy};
        funVoidVoidPtrI32VoidPtrTy = FunctionType::get(voidTy, ArrayRef<Type*>(argsMO, 3), false);
        storeMemOpFunction = M.getOrInsertFunction("__ctStoreMemOp", funVoidVoidPtrI32VoidPtrTy);
        Type* argsInit[] = {voidPtrTy};//threadArgsTy->getPointerTo()};
        funVoidPtrVoidPtrTy = FunctionType::get(voidPtrTy, ArrayRef<Type*>(argsInit, 1), false);
        threadInitFunction = M.getOrInsertFunction("__ctInitThread", funVoidPtrVoidPtrTy);

        // void (void) functions:
        funVoidVoidTy = FunctionType::get(voidTy, false);
        allocateBufferFunction = M.getOrInsertFunction("__ctAllocateLocalBuffer", funVoidVoidTy);
        storeMemReadMarkFunction = M.getOrInsertFunction("__ctStoreMemReadMark", funVoidVoidTy);
        storeMemWriteMarkFunction = M.getOrInsertFunction("__ctStoreMemWriteMark", funVoidVoidTy);
        ompPushParentFunction = M.getOrInsertFunction("__ctOMPPushParent", funVoidVoidTy);
        ompPopParentFunction = M.getOrInsertFunction("__ctOMPPopParent", funVoidVoidTy);
        
        allocateCTidFunction = M.getOrInsertFunction("__ctAllocateCTid", FunctionType::get(int32Ty, false));
        getThreadNumFunction = M.getOrInsertFunction("__ctGetLocalNumber", FunctionType::get(int32Ty, false));
        getCurrentTickFunction = M.getOrInsertFunction("__ctGetCurrentTick", FunctionType::get(int64Ty, false));
        ompGetParentFunction = M.getOrInsertFunction("__kmpc_get_parent_taskid", FunctionType::get(int64Ty, false));
        
        Type* argsSSync[] = {voidPtrTy, int32Ty/*type*/, int32Ty/*retVal*/, int64Ty /*ct_tsc_t*/};
        funVoidVoidPtrI32I32I64Ty = FunctionType::get(voidTy, ArrayRef<Type*>(argsSSync, 4), false);
        storeSyncFunction = M.getOrInsertFunction("__ctStoreSync", funVoidVoidPtrI32I32I64Ty);
        
        Type* argsTC[] = {int32Ty};
        
        // TODO: See how one might flag a function as having the attribute of "does not return", for exit()
        funVoidI32Ty = FunctionType::get(voidTy, ArrayRef<Type*>(argsTC, 1), false);
        storeBasicBlockMarkFunction = M.getOrInsertFunction("__ctStoreBasicBlockMark", funVoidI32Ty);
        pthreadExitFunction = M.getOrInsertFunction("pthread_exit", funVoidI32Ty);
        ompThreadCreateFunction = M.getOrInsertFunction("__ctOMPThreadCreate", funVoidI32Ty);
        ompThreadJoinFunction = M.getOrInsertFunction("__ctOMPThreadJoin", funVoidI32Ty);
        ompTaskCreateFunction = M.getOrInsertFunction("__ctOMPTaskCreate", funVoidI32Ty);
        checkBufferFunction = M.getOrInsertFunction("__ctCheckBufferSize", funVoidI32Ty);
        
        funI32I32Ty = FunctionType::get(int32Ty, ArrayRef<Type*>(argsTC, 1), false);
        storeBasicBlockCompFunction = M.getOrInsertFunction("__ctStoreBasicBlockComplete", funI32I32Ty);
        
        Type* argsME[] = {int8Ty, int64Ty, voidPtrTy};
        funVoidI8I64VoidPtrTy = FunctionType::get(voidTy, ArrayRef<Type*>(argsME, 3), false);
        storeMemoryEventFunction = M.getOrInsertFunction("__ctStoreMemoryEvent", funVoidI8I64VoidPtrTy);
        storeBulkMemoryOpFunction = M.getOrInsertFunction("__ctStoreBulkMemoryEvent", funVoidI8I64VoidPtrTy);
        
        Type* argsQB[] = {int8Ty};
        funVoidI8Ty = FunctionType::get(voidTy, ArrayRef<Type*>(argsQB, 1), false);
        queueBufferFunction = M.getOrInsertFunction("__ctQueueBuffer", funVoidI8Ty);
        ompTaskJoinFunction = M.getOrInsertFunction("__ctOMPTaskJoin", funVoidVoidTy);
        
        Type* argsSB[] = {int8Ty, voidPtrTy, int64Ty};
        funVoidI8VoidPtrI64Ty = FunctionType::get(voidTy, ArrayRef<Type*>(argsSB, 3), false);
        storeBarrierFunction = M.getOrInsertFunction("__ctStoreBarrier", funVoidI8VoidPtrI64Ty);
        
        Type* argsATI[] = {voidPtrTy, int32Ty};
        funVoidVoidPtrI32Ty = FunctionType::get(voidTy, ArrayRef<Type*>(argsATI, 2), false);
        storeThreadInfoFunction = M.getOrInsertFunction("__ctAddThreadInfo", funVoidVoidPtrI32Ty);
        
        // This needs to be machine type here
        //
        
        if (M.getPointerSize() == llvm::Module::Pointer64)
        {
            Type* argsSTJ[] = {int64Ty, int64Ty};
            pthreadTy = int64Ty;
            funVoidI64I64Ty = FunctionType::get(voidTy, ArrayRef<Type*>(argsSTJ, 2), false);
        }
        else
        {
            Type* argsSTJ[] = {int32Ty, int64Ty};
            pthreadTy = int32Ty;
            funVoidI64I64Ty = FunctionType::get(voidTy, ArrayRef<Type*>(argsSTJ, 2), false);
        }
        
        storeThreadJoinFunction = M.getOrInsertFunction("__ctStoreThreadJoin", funVoidI64I64Ty);
        Type* argsCTA[] = {pthreadTy->getPointerTo(), 
                           voidPtrTy, 
                           static_cast<Type *>(funVoidPtrVoidTy)->getPointerTo(),
                           voidPtrTy};
        FunctionType* funIntPthreadPtrVoidPtrVoidPtrFunVoidPtr = FunctionType::get(int32Ty, ArrayRef<Type*>(argsCTA,4), false);
        createThreadActualFunction = M.getOrInsertFunction("__ctThreadCreateActual", funIntPthreadPtrVoidPtrVoidPtrFunVoidPtr);
		
        contechCFGFile = new ofstream(ContechCFGFilename.c_str(), ios_base::out | ios_base::binary | ios_base::app);
        if (contechCFGFile != NULL && contechCFGFile->good())
        {
            //errs() << "File success!\n";
        }
        else
        {
            //contechCFGFile = errs;
        }
        
        return true;
    }
    
    _CONTECH_FUNCTION_TYPE classifyFunctionName(const char* fn)
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
                CallInst::Create(checkBufferFunction, "", I);
                return;
            }
        }
    }
    
    //
    //  Wrapper call that appropriately adds the operations to record the memory operation
    //
    pllvm_mem_op Contech::insertMemOp(Instruction* li, Value* addr, bool isWrite, unsigned int memOpPos, Value* pos)
    {
        pllvm_mem_op tMemOp = new llvm_mem_op;
        
        tMemOp->isWrite = isWrite;
        tMemOp->size = getSimpleLog(getSizeofType(addr->getType()->getPointerElementType()));
        
        //Constant* cIsWrite = ConstantInt::get(int8Ty, isWrite);
        //Constant* cSize = ConstantInt::get(int8Ty, tMemOp->size);
        Constant* cPos = ConstantInt::get(int32Ty, memOpPos);
        Value* addrI = new BitCastInst(addr, voidPtrTy, Twine("Cast as void"), li);
        Value* argsMO[] = {addrI, cPos, pos};
        debugLog("storeMemOpFunction @" << __LINE__);
        CallInst* smo = CallInst::Create(storeMemOpFunction, ArrayRef<Value*>(argsMO, 3), "", li);
        assert(smo != NULL);
        smo->getCalledFunction()->addFnAttr( ALWAYS_INLINE );
        
        tMemOp->addr = NULL;
        tMemOp->next = NULL;
        
        return tMemOp;
    }
    
    void Contech::internalAddAllocate(BasicBlock& B)
    {
        debugLog("allocateBufferFunction @" << __LINE__);
        CallInst::Create(allocateBufferFunction, "", B.begin());
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
        

        
        //icontechStateFile->read((char*)&bb_count, sizeof(unsigned int));
        // *contechStateFile >> bb_count;
        // for unknown reasons, a file that does not exist needs to clear all bits and not
        // just eof for writing
        icontechStateFile->close();
        delete icontechStateFile;
        //contechStateFile->clear(contechStateFile->rdstate() & ~(ios_base::eofbit));
        
        // errs() << "Start BB: " << bb_count << "\n";
        
        for (Module::iterator F = M.begin(), FE = M.end(); F != FE; ++F) {
            int status;
            const char* fmn = F->getName().data();
            char* fn = abi::__cxa_demangle(fmn, 0, 0, &status);

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
                    F->setName(Twine("ct_orig_main"));
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
            // If this function is one Contech adds for OpenMP
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
        
            // Now insturment each basic block in the function
            for (Function::iterator B = F->begin(), BE = F->end(); B != BE; ++B) {
                BasicBlock &pB = *B;
                
                internalRunOnBasicBlock(pB, M, bb_count, ContechMarkFrontend);
                bb_count++;
            }
            
            // If fmn is fn, then it was allocated by the demangle routine and we are required to free
            if (fmn == fn)
            {
                free(fn);
            }
        }
        
        if (ContechMarkFrontend == true) goto cleanup;
        
        DominatorTree* dTree;
        // iterate over cfgInfoMap
        for (map<BasicBlock*, llvm_basic_block*>::iterator bi = cfgInfoMap.begin(), bie = cfgInfoMap.end(), t; bi != bie; ++bi)
        {
            *contechCFGFile << bi->second->id <<","<<bi->second->ev<<",";
            
            // TODO: replace with a wrapper that operates on each tgts
            // For each tgt, record its event type and the basic block number(s) that follow
            // Also, check if the target has already been reached and that it dominates this block.
            // If this block transitions to a block that dominates it, then there is a loop and the
            // dominator is a good place to check the buffer size
            t = bie;
            if (bi->second->tgts[0] != NULL)
            {
                t = cfgInfoMap.find(bi->second->tgts[0]);
            }
            if (t == bie)
            {
                *contechCFGFile << "-1,";
            }
            else
            {
                dTree = &getAnalysis<DominatorTree>(*(bi->first->getParent()));
                *contechCFGFile << t->second->id <<",";
                
                // T is the map entry for tgts[0]
                if (t->second->hasCheckBuffer == 0)
                {
                    t->second->hasCheckBuffer = 1;
                }
                else if (t->second->hasCheckBuffer == 1 && dTree->dominates(bi->second->tgts[0], bi->first))
                {
                    //addCheckAfterPhi(bi->second->tgts[0]);
                    t->second->hasCheckBuffer = 2;
                }
            }
            
            t = bie;
            if (bi->second->tgts[1] != NULL)
            {
                t = cfgInfoMap.find(bi->second->tgts[1]);
            }
            if (t == bie)
            {
                *contechCFGFile << "-1\n";
            }
            else
            {
                *contechCFGFile << t->second->id <<"\n";
                
                dTree = &getAnalysis<DominatorTree>(*(bi->first->getParent()));
                // t is the map entry for tgts[1]
                if (t->second->hasCheckBuffer == 0)
                {
                    t->second->hasCheckBuffer = 1;
                }
                else if (t->second->hasCheckBuffer == 1 && dTree->dominates(bi->second->tgts[1], bi->first))
                {
                    //addCheckAfterPhi(bi->second->tgts[1]);
                    t->second->hasCheckBuffer = 2;
                }
            }
        }
        
cleanup:     
        ofstream* contechStateFile = new ofstream(ContechStateFilename.c_str(), ios_base::out | ios_base::binary);
        //contechStateFile->seekp(0, ios_base::beg);
        if (buffer == NULL)
        {
            contechStateFile->write((char*)&bb_count, sizeof(unsigned int));
        }
        else
        {
            *(unsigned int*) buffer = bb_count;
            contechStateFile->write(buffer, length);
        }
        //contechStateFile->seekp(0, ios_base::end);
        
        int wcount = 0;
        for (map<BasicBlock*, llvm_basic_block*>::iterator bi = cfgInfoMap.begin(), bie = cfgInfoMap.end(); bi != bie; ++bi)
        {
            pllvm_mem_op t = bi->second->first_op;
            contechStateFile->write((char*)&bi->second->id, sizeof(unsigned int));
            contechStateFile->write((char*)&bi->second->len, sizeof(unsigned int));
            //errs() << "BB: " << bi->second->id << " Len: " << bi->second->len << "\n";
            
            while (t != NULL)
            {
                pllvm_mem_op tn = t->next;
                contechStateFile->write((char*)&t->isWrite, sizeof(bool));
                contechStateFile->write((char*)&t->size, sizeof(char));
                delete (t);
                t = tn;
            }
            wcount++;
            free(bi->second);
        }
        //errs() << "Wrote: " << wcount << " basic blocks\n";
        cfgInfoMap.clear();
        contechCFGFile->close();
        contechStateFile->close();
        delete contechStateFile;
        delete contechCFGFile;
        
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
        errs() << "Failed get size - " << *t << "\n";
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
        for (BasicBlock::iterator I = B.begin(), E = B.end(); I != E; ++I){
            if (CallInst *ci = dyn_cast<CallInst>(&*I)) {
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
    bool Contech::internalRunOnBasicBlock(BasicBlock &B,  Module &M, int bbid, const bool markOnly)
    {
        Instruction* iPt = B.getTerminator();
        std::vector<pllvm_mem_op> opsInBlock;
        ct_event_id containingEvent = ct_event_basic_block;
        unsigned int memOpCount = 0;
        Instruction* aPhi = B.begin();
        bool getNextI = false;
        bool containCall = false, containQueueBuf = false;
        bool containKeyCall = false;
        Value* posValue = NULL;
        
        for (BasicBlock::iterator I = B.begin(), E = B.end(); I != E; ++I){
            // TODO: Use BasicBlock->getFirstNonPHIOrDbgOrLifetime as insertion point
            //   compare with getFirstInsertionPt
            if (/*PHINode *pn = */dyn_cast<PHINode>(&*I)) {
                getNextI = true;
                continue;
            }
            else if (/*LandingPadInst *lpi = */dyn_cast<LandingPadInst>(&*I)){
                getNextI = true;
                continue;
            }
            else if (/*LoadInst *li = */dyn_cast<LoadInst>(&*I)){
                memOpCount ++;
            }
            else if (/*StoreInst *si = */dyn_cast<StoreInst>(&*I)) {
                memOpCount ++;
            }
            else if (ContechMinimal == true)
            {
                if (CallInst* ci = dyn_cast<CallInst>(&*I)) {
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
        
        //errs() << "Basic Block - " << bbid << " -- " << memOpCount << "\n";
        //debugLog("checkBufferFunction @" << __LINE__);
        //CallInst::Create(checkBufferFunction, "", aPhi);
        Constant* llvm_bbid;
        Constant* llvm_nops = NULL;
        CallInst* sbb;
        CallInst* sbbc = NULL;
        if (markOnly == true)
        {
            llvm_bbid = ConstantInt::get(int32Ty, bbid);
            Value* argsBB[] = {llvm_bbid};
            debugLog("storeBasicBlockMarkFunction @" << __LINE__);
            sbb = CallInst::Create(storeBasicBlockMarkFunction, ArrayRef<Value*>(argsBB, 1), "", aPhi);
        }
        else {
            llvm_bbid = ConstantInt::get(int32Ty, bbid);
            Value* argsBB[] = {llvm_bbid};
            debugLog("storeBasicBlockFunction @" << __LINE__);
            sbb = CallInst::Create(storeBasicBlockFunction, 
                                   ArrayRef<Value*>(argsBB, 1), 
                                   string("storeBlock") + to_string(bbid), 
                                   aPhi);
            sbb->getCalledFunction()->addFnAttr( ALWAYS_INLINE);
            posValue = sbb;
            
            // In LLVM 3.3+, switch to Monotonic and not Acquire
            new FenceInst(M.getContext(), Acquire, SingleThread, sbb);
        }

        unsigned int memOpPos = 0;
        bool hasInstAllMemOps = false;
        for (BasicBlock::iterator I = B.begin(), E = B.end(); I != E; ++I){
        
            // After all of the known memOps have been insturmented, close out the basic
            //   block event based on the number of memOps
            if (hasInstAllMemOps == false && memOpPos == memOpCount && markOnly == false)
            {
                llvm_nops = ConstantInt::get(int32Ty, memOpCount);
                Value* argsBBc[] = {llvm_nops};
                if (memOpCount == 0)
                {
                    debugLog("storeBasicBlockCompFunction @" << __LINE__);
                    sbbc = CallInst::Create(storeBasicBlockCompFunction, ArrayRef<Value*>(argsBBc, 1), "", aPhi);
                    
                    new FenceInst(M.getContext(), Release, SingleThread, aPhi);
                }
                else
                {
                    debugLog("storeBasicBlockCompFunction @" << __LINE__);
                    sbbc = CallInst::Create(storeBasicBlockCompFunction, ArrayRef<Value*>(argsBBc, 1), "", I);
                    
                    new FenceInst(M.getContext(), Release, SingleThread, I);
                }
                sbbc->getCalledFunction()->addFnAttr( ALWAYS_INLINE);
                bi->len = memOpCount;
                hasInstAllMemOps = true;
            }

            // If this block is only being marked, then only memops are needed
            if (markOnly == true)
            {
                // Don't bother maintaining a list of memory ops for the basic block
                //   at this time
                bi->len = 0;
                if (LoadInst *li = dyn_cast<LoadInst>(&*I)){
                    debugLog("storeMemReadMarkFunction @" << __LINE__);
                    CallInst::Create(storeMemReadMarkFunction, "", li);
                }
                else if (StoreInst *si = dyn_cast<StoreInst>(&*I)) {
                    debugLog("storeMemWriteMarkFunction @" << __LINE__);
                    CallInst::Create(storeMemWriteMarkFunction, "", si);
                }
                if (CallInst *ci = dyn_cast<CallInst>(&*I)) {
                
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
            if (LoadInst *li = dyn_cast<LoadInst>(&*I)){
                pllvm_mem_op tMemOp = insertMemOp(li, li->getPointerOperand(), false, memOpPos, posValue);
                memOpPos ++;
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
            }
            //  store [volatile] <ty> <value>, <ty>* <pointer>[, align <alignment>][, !nontemporal !<index>]
            else if (StoreInst *si = dyn_cast<StoreInst>(&*I)) {
                pllvm_mem_op tMemOp = insertMemOp(si, si->getPointerOperand(), true, memOpPos, posValue);
                memOpPos ++;
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
            }
            else if (CallInst *ci = dyn_cast<CallInst>(&*I)) {
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
                if (ci->doesNotReturn())
                {
                    iPt = ci;
                }
                
                CONTECH_FUNCTION_TYPE funTy = classifyFunctionName(fn);
                //errs() << funTy << "\n";
                switch(funTy)
                {
                
                // Check for call to exit(n), replace with pthread_exit(n)
                //  Splash benchmarks like to exit on us which pthread_cleanup doesn't catch
                //  Also check that this "...exit..." is at least a do not return function
                case(EXIT):
                    if (ci->getCalledFunction()->doesNotReturn())
                    {
                        ci->setCalledFunction(pthreadExitFunction);
                    }
                    break;
                case(MALLOC):
                    if (!(ci->getCalledFunction()->getReturnType()->isVoidTy()))
                    {
                        Value* cArg[] = {ConstantInt::get(int8Ty, 1), ci->getArgOperand(0), ci};
                        debugLog("storeMemoryEventFunction @" << __LINE__);
                        CallInst* nStoreME = CallInst::Create(storeMemoryEventFunction, ArrayRef<Value*>(cArg, 3),
                                                            "", ++I);
                        I = nStoreME;
                    }
                    break;
                case (FREE):
                {
                    Value* cz = ConstantInt::get(int8Ty, 0);
                    Value* cz32 = ConstantInt::get(int64Ty, 0);
                    Value* cArg[] = {cz, cz32, ci->getArgOperand(0)};
                    debugLog("storeMemoryEventFunction @" << __LINE__);
                    CallInst* nStoreME = CallInst::Create(storeMemoryEventFunction, ArrayRef<Value*>(cArg, 3),
                                                        "", ++I);
                    I = nStoreME;
                }
                break;
                case (SYNC_ACQUIRE):
                {
                    debugLog("getCurrentTickFunction @" << __LINE__);
                    CallInst* nGetTick = CallInst::Create(getCurrentTickFunction, "tick", ci);
                    Value* con1 = ConstantInt::get(int32Ty, 1);
                     // If sync_acquire returns int, pass it, else pass 0 - success
                    Value* retV;
                    if (ci->getType() == int32Ty)
                        retV = ci;
                    else 
                        retV = ConstantInt::get(int32Ty, 0);
                    Value* cArg[] = {new BitCastInst(ci->getArgOperand(0), voidPtrTy, "locktovoid", ci),
                                     con1, 
                                     retV,
                                     nGetTick};
                    debugLog("storeSyncFunction @" << __LINE__);
                    CallInst* nStoreSync = CallInst::Create(storeSyncFunction, ArrayRef<Value*>(cArg,4),
                                                        "", ++I);
                    I = nStoreSync;
                    if (false /*ContechMinimal == false*/)
                    {
                        cArg[0] = ConstantInt::get(int8Ty, 1);
                        debugLog("queueBufferFunction @" << __LINE__);
                        CallInst* nQueueBuf = CallInst::Create(queueBufferFunction, ArrayRef<Value*>(cArg, 1),
                                                            "", ++I);
                        I = nQueueBuf;
                        containQueueBuf = true;
                    }
                    containingEvent = ct_event_sync;
                    iPt = nStoreSync;
                }
                break;
                case (SYNC_RELEASE):
                {
                    debugLog("getCurrentTickFunction @" << __LINE__);
                    CallInst* nGetTick = CallInst::Create(getCurrentTickFunction, "tick", ci);
                    BitCastInst* bci = new BitCastInst(ci->getArgOperand(0), voidPtrTy, "locktovoid", ++I);
                    Value* cArg[] = {bci, ConstantInt::get(int32Ty, 0), ConstantInt::get(int32Ty, 0), nGetTick};
                    debugLog("storeSyncFunction @" << __LINE__);
                    CallInst* nStoreSync = CallInst::Create(storeSyncFunction, ArrayRef<Value*>(cArg,4),
                                                        "", I);
                    I = nStoreSync;
                    if (hasInstAllMemOps == false)
                    {
                        errs() << "Failed to close storeBasicBlock before storeSync\n";
                    }
                    if (false /*ContechMinimal == false*/)
                    {
                        cArg[0] = ConstantInt::get(int8Ty, 1);
                        debugLog("queueBufferFunction @" << __LINE__);
                        CallInst* nQueueBuf = CallInst::Create(queueBufferFunction, ArrayRef<Value*>(cArg, 1),
                                                            "", ++I);
                        I = nQueueBuf;
                        containQueueBuf = true;
                    }
                    containingEvent = ct_event_sync;
                    iPt = ++I;
                    I = nStoreSync;
                }
                break;
                case (COND_WAIT):
                {
                    debugLog("getCurrentTickFunction @" << __LINE__);
                    CallInst* nGetTick = CallInst::Create(getCurrentTickFunction, "tick", ci);
                    BitCastInst* bciCV = new BitCastInst(ci->getArgOperand(0), voidPtrTy, "locktovoid", ++I);
                    BitCastInst* bciMut = new BitCastInst(ci->getArgOperand(1), voidPtrTy, "locktovoid", ci);
                    Value* cArg[] = {bciMut, ConstantInt::get(int32Ty, 0), ConstantInt::get(int32Ty, 0), nGetTick};
                    debugLog("storeSyncFunction @" << __LINE__);
                    CallInst::Create(storeSyncFunction, ArrayRef<Value*>(cArg,4), "", ci);
                    if (ContechMinimal == false)
                    {
                        cArg[0] = ConstantInt::get(int8Ty, 1);
                        debugLog("queueBufferFunction @" << __LINE__);
                        CallInst::Create(queueBufferFunction, ArrayRef<Value*>(cArg, 1),
                                                            "", ci);
                        containQueueBuf = true;
                    }
                    
                    debugLog("getCurrentTickFunction @" << __LINE__);
                    CallInst* nGetTick2 = CallInst::Create(getCurrentTickFunction, "tick2", I); 
                    Value* retV;
                    if (ci->getType() == int32Ty)
                        retV = ci;
                    else 
                        retV = ConstantInt::get(int32Ty, 0);                    
                    Value* cArgCV[] = {bciCV, ConstantInt::get(int32Ty, 2), retV, nGetTick2};
                    debugLog("storeSyncFunction @" << __LINE__);
                    CallInst* nStoreCV = CallInst::Create(storeSyncFunction, ArrayRef<Value*>(cArgCV, 4), "", I);
                    I = nStoreCV;
                    CallInst* nGetTick3 = CallInst::Create(getCurrentTickFunction, "tick3", ++I);                                    
                    Value* cArgMut[] = {bciMut, ConstantInt::get(int32Ty, 1), ConstantInt::get(int32Ty, 0), nGetTick3};
                    debugLog("storeSyncFunction @" << __LINE__);
                    CallInst* nStoreMut = CallInst::Create(storeSyncFunction, ArrayRef<Value*>(cArgMut, 4), "", I);
                    I = nStoreMut;
                    containingEvent = ct_event_sync;
                    iPt = ++I;
                    I = nStoreMut;
                }
                break;
                case (COND_SIGNAL):
                {
                    debugLog("getCurrentTickFunction @" << __LINE__);
                    CallInst* nGetTick = CallInst::Create(getCurrentTickFunction, "tick", ci);
                    BitCastInst* bciCV = new BitCastInst(ci->getArgOperand(0), voidPtrTy, "locktovoid", ++I);
                    Value* retV;
                    if (ci->getType() == int32Ty)
                        retV = ci;
                    else 
                        retV = ConstantInt::get(int32Ty, 0);
                    Value* cArgCV[] = {bciCV, ConstantInt::get(int32Ty, 3), retV, nGetTick};
                    debugLog("storeSyncFunction @" << __LINE__);
                    CallInst* nStoreCV = CallInst::Create(storeSyncFunction, ArrayRef<Value*>(cArgCV, 4), "", I);
                    I = nStoreCV;
                    containingEvent = ct_event_sync;
                    iPt = ++I;
                    I = nStoreCV;
                }
                break;
                case (BARRIER_WAIT):
                {
                    debugLog("getCurrentTickFunction @" << __LINE__);
                    CallInst* nGetTick = CallInst::Create(getCurrentTickFunction, "tick", ci);
                    BitCastInst* bci = new BitCastInst(ci->getArgOperand(0), voidPtrTy, "locktovoid", I);
                    Value* c1 = ConstantInt::get(int8Ty, 1);
                    Value* cArgs[] = {c1, bci, nGetTick};
                    // Record the barrier entry
                    debugLog("storeBarrierFunction @" << __LINE__);
                    CallInst* nStoreBarEn = CallInst::Create(storeBarrierFunction, ArrayRef<Value*>(cArgs,3),
                                                        "", I);
                    Value* cArg[] = {c1};
                    debugLog("queueBufferFunction @" << __LINE__);
                    /*CallInst* nQueueBuf = */CallInst::Create(queueBufferFunction, ArrayRef<Value*>(cArg, 1),
                                                        "", I);                   
                    I++;
                    cArgs[0] = ConstantInt::get(int8Ty, 0);
                    // Record the barrier exit
                    debugLog("storeBarrierFunction @" << __LINE__);
                    CallInst* nStoreBarEx = CallInst::Create(storeBarrierFunction, ArrayRef<Value*>(cArgs,3),
                                                        "", I);
                    I = nStoreBarEx;
                    containingEvent = ct_event_barrier;
                    containQueueBuf = true;
                    iPt = nStoreBarEn;
                }
                break;
                case (THREAD_JOIN):
                {
                    //Value* vPtr = new BitCastInst(ci->getOperand(0), voidPtrTy, "hideInPtr", ci);
                    //errs() << *ci->getOperand(0) << "\t" << *ci->getOperand(0)->getType() << "\n";
                    //errs() << *storeThreadJoinFunction->getType() << "\n";
                    Value* c1 = ConstantInt::get(int8Ty, 1);
                    Value* cArgQB[] = {c1};
                    debugLog("queueBufferFunction @" << __LINE__);
                    /*CallInst* nQueueBuf = */CallInst::Create(queueBufferFunction, ArrayRef<Value*>(cArgQB, 1),
                                                        "", ci);
                    CallInst* nGetTick = CallInst::Create(getCurrentTickFunction, "tick", ci);
                    Value* cArg[] = {ci->getOperand(0), nGetTick};
                    debugLog("storeThreadJoinFunction @" << __LINE__);
                    I++;
                    CallInst* nStoreJ = CallInst::Create(storeThreadJoinFunction, ArrayRef<Value*>(cArg, 2), 
                                                         Twine(""), I);
                    containingEvent = ct_event_task_join;
                    I = nStoreJ;
                    iPt = nGetTick;
                    containQueueBuf = true;
                }
                break;
                //int pthread_create(pthread_t * thread, const pthread_attr_t * attr,
                //                   void * (*start_routine)(void *), void * arg);
                //
                case (THREAD_CREATE):
                {
                    Value* cTcArg[] = {ci->getArgOperand(0), 
                                       new BitCastInst(ci->getArgOperand(1), voidPtrTy, "", ci), 
                                       ci->getArgOperand(2), 
                                       ci->getArgOperand(3)};
                    debugLog("createThreadActualFunction @" << __LINE__);
                    CallInst* nThreadCreate = CallInst::Create(createThreadActualFunction,
                                                               ArrayRef<Value*>(cTcArg, 4), "", ci);
                    ci->replaceAllUsesWith(nThreadCreate);
                    ci->eraseFromParent();
                    I = nThreadCreate;
                    //ci->setCalledFunction(createThreadActualFunction);
                }
                break;
                case (OMP_FORK):
                {
                    // Simple case, push and pop the parent id
                    // And Transform the arguments to the function call
                    CallInst::Create(ompPushParentFunction, "", ci);
                    ++I;
                    CallInst* nPopParent = CallInst::Create(ompPopParentFunction, "", &*I);
                    I = nPopParent;
                    
                    // Add one to the number of arguments
                    //   TODO: Make this a ConstantExpr
                    ci->setArgOperand(1, BinaryOperator::Create(Instruction::Add,
                                                ci->getArgOperand(1),
                                                ConstantInt::get(int32Ty, 1),
                                                "", ci));
                    
                    
                    
                    // Change the function called to a wrapper routine
                    Value* arg2 = ci->getArgOperand(2);
                    Function* ompMicroTask = NULL;
                    ConstantExpr* bci = NULL;
                    if ((bci = dyn_cast<ConstantExpr>(arg2)) != NULL)
                    {
                        if (bci->isCast())
                        {
                            ompMicroTask = dyn_cast<Function>(bci->getOperand(0));
                        }
                    }
                    else
                    {
                        errs() << "Need new casting route for omp fork call\n";
                    }
                    ompMicroTaskFunctions.insert(ompMicroTask);
                    Function* wrapMicroTask = createMicroTaskWrap(ompMicroTask, M);
                    contechAddedFunctions.insert(wrapMicroTask);
                    ci->setArgOperand(2, ConstantExpr::getBitCast(wrapMicroTask, bci->getType()));
                                                              
                    //ci->setArgOperand(2, bci->getWithOperandReplaced(0,wrapMicroTask));
                    
                    // One cannot simply add an argument to an instruction
                    // Instead we have to copy the arguments over and create a new instruction
                    Value** cArg = new Value*[ci->getNumArgOperands() + 1];
                    for (unsigned int i = 0; i < ci->getNumArgOperands(); i++)
                    {
                        cArg[i] = ci->getArgOperand(i);
                    }
                    
                    // Now add a new argument
                    debugLog("getThreadNumFunction @" << __LINE__);
                    cArg[ci->getNumArgOperands()] = CallInst::Create(getThreadNumFunction, "", ci);
                    Value* cArgQB[] = {ConstantInt::get(int8Ty, 1)};
                    debugLog("queueBufferFunction @" << __LINE__);
                    CallInst::Create(queueBufferFunction, ArrayRef<Value*>(cArgQB, 1),
                                                            "", ci);
                    debugLog("kmpc_fork_call @" << __LINE__);
                    CallInst* nForkCall = CallInst::Create(ci->getCalledFunction(),
                                                           ArrayRef<Value*>(cArg, 1 + ci->getNumArgOperands()),
                                                           ci->getName(), ci);
                    ci->replaceAllUsesWith(nForkCall);
                    ci->eraseFromParent();
                    delete [] cArg;
                }
                break;
                case (OMP_FOR_ITER):
                {
                    CallInst::Create(ompTaskJoinFunction, "", ci);
                    ++I;
                    Value* cArg[] = {ci};
                    CallInst* nCreate = CallInst::Create(ompTaskCreateFunction, ArrayRef<Value*>(cArg, 1), "", I);
                    I = nCreate;
                }
                break;
                case (OMP_BARRIER):
                {
                    // OpenMP barriers use argument 1 for barrier ID
                    CallInst* nGetTick = CallInst::Create(getCurrentTickFunction, "tick", ci);
                    //IntToPtrInst* bci = new IntToPtrInst(ci->getArgOperand(1), voidPtrTy, "locktovoid", I);
                    IntToPtrInst* bci = new IntToPtrInst(CallInst::Create(ompGetParentFunction, "", I), voidPtrTy, "locktovoid", I);
                    Value* c1 = ConstantInt::get(int8Ty, 1);
                    Value* cArgs[] = {c1, bci, nGetTick};
                    // Record the barrier entry
                    debugLog("storeBarrierFunction @" << __LINE__);
                    CallInst* nStoreBarEn = CallInst::Create(storeBarrierFunction, ArrayRef<Value*>(cArgs,3),
                                                        "", I);
                    Value* cArg[] = {c1};
                    debugLog("queueBufferFunction @" << __LINE__);
                    /*CallInst* nQueueBuf = */CallInst::Create(queueBufferFunction, ArrayRef<Value*>(cArg, 1),
                                                        "", I);                   
                    ++I;
                    cArgs[0] = ConstantInt::get(int8Ty, 0);
                    // Record the barrier exit
                    debugLog("storeBarrierFunction @" << __LINE__);
                    CallInst* nStoreBarEx = CallInst::Create(storeBarrierFunction, ArrayRef<Value*>(cArgs,3),
                                                        "", I);
                    I = nStoreBarEx;
                    containingEvent = ct_event_barrier;
                    containQueueBuf = true;
                    iPt = nStoreBarEn;
                }
                break;
                default:
                    // TODO: Function->isIntrinsic()
                    if (0 == __ctStrCmp(fn, "memcpy"))
                    {
                        Value* cArgL[] = {ConstantInt::get(int8Ty, 0), ci->getArgOperand(2), ci->getArgOperand(1)};
                        CallInst::Create(storeBulkMemoryOpFunction, ArrayRef<Value*>(cArgL, 3), "", I);
                        Value* cArgS[] = {ConstantInt::get(int8Ty, 1), ci->getArgOperand(2), ci->getArgOperand(0)};
                        CallInst::Create(storeBulkMemoryOpFunction, ArrayRef<Value*>(cArgS, 3), "", I);
                    }
                    else if (0 == __ctStrCmp(fn, "llvm."))
                    {
                        if (0 == __ctStrCmp(fn + 5, "memcpy"))
                        {
                            Value* cArgL[] = {ConstantInt::get(int8Ty, 0), ci->getArgOperand(2), ci->getArgOperand(1)};
                            CallInst::Create(storeBulkMemoryOpFunction, ArrayRef<Value*>(cArgL, 3), "", I);
                            Value* cArgS[] = {ConstantInt::get(int8Ty, 1), ci->getArgOperand(2), ci->getArgOperand(0)};
                            CallInst::Create(storeBulkMemoryOpFunction, ArrayRef<Value*>(cArgS, 3), "", I);
                        }
                        else if (0 == __ctStrCmp(fn + 5, "dbg") ||
                                 0 == __ctStrCmp(fn + 5, "lifetime"))
                        {
                            // IGNORE
                        }
                        else
                        {
                            errs() << "Builtin - " << fn << "\n";
                        }
                    }
                    else if (ci != sbb && ci != sbbc)
                    {
                        // We added a storeBasicBlock to this basic block
                        //  Ignore it as an insertion point
                        iPt = ci;
                        containCall = true;
                    }
                    else
                    {
                    }
                }
                if (status == 0)
                {
                    free(fdn);
                }
            }
        }
        
        //
        // Being conservative, if another function was called, then
        // the insturmentation needs to check that the buffer isn't full
        //
        // Being really conservative every block has a check, this also
        //   requires disabling the dominator tree traversal in the runOnModule routine
        //
        //if (/*containCall == true && */containQueueBuf == false && markOnly == false)
        if (B.getTerminator()->getNumSuccessors() != 1 && markOnly == false)
        {
            Value* argsCheck[] = {sbbc};
            debugLog("checkBufferFunction @" << __LINE__);
            CallInst::Create(checkBufferFunction, ArrayRef<Value*>(argsCheck, 1), "", iPt);
            containCall = true;
        }
        
        
        // Finally record the information about this basic block
        //  into the CFG structure, so that targets can be matched up
        //  once all basic blocks have been parsed
        {
            // Does this basic block check its buffer?
            bi->hasCheckBuffer = (containCall)?2:0;
            bi->ev = containingEvent;
            {
                TerminatorInst* t = B.getTerminator();
                unsigned i = t->getNumSuccessors();
                unsigned j;
                
                // Let's assume that a basic block can only go to at most two other blocks
                for (j = 0; j < 2; j++)
                {
                    if (j < i)
                    {
                        bi->tgts[j] = t->getSuccessor(j);
                    }
                    else
                    {
                        bi->tgts[j] = NULL;
                    }
                }
                
            }
            cfgInfoMap.insert(pair<BasicBlock*, llvm_basic_block*>(&B, bi));
            
        }
        
        return true;
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
    argTy[baseFunType->getNumParams()] = int32Ty;
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
    CallInst::Create(ompThreadCreateFunction, ArrayRef<Value*>(cArg, 1), "", soloBlock);
    CallInst* wrappedCall = CallInst::Create(ompMicroTask, ArrayRef<Value*>(cArgExt, argListSize - 1), "", soloBlock);
    CallInst::Create(ompThreadJoinFunction, ArrayRef<Value*>(cArg, 1), "", soloBlock);
    if (ompMicroTask->getReturnType() != voidTy)
        ReturnInst::Create(M.getContext(), wrappedCall, soloBlock);
    else
        ReturnInst::Create(M.getContext(), soloBlock);
    
    delete [] argTy;
    delete [] cArgExt;
    
    return extFun;
}
    
char Contech::ID = 0;
static RegisterPass<Contech> X("Contech", "Contech Pass", false, false);
