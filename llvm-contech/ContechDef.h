#ifndef CONTECHDEF_H
#define CONTECHDEF_H

// These are included in contech.cpp, which creates the appropriate path
//    given that llvm decided to put the headers in different directories
//#include "llvm/Pass.h"
//#include "llvm/Module.h"

#include <string>
#include <map>
#include <set>
#include <vector>
#include <iostream>
#include <fstream>
#include <unordered_set>
#include <unordered_map>
#include <cxxabi.h>
#include "../common/eventLib/ct_event_st.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/Analysis/ScalarEvolution.h"

//#define DEBUG_PRINT_CALLINST
#ifdef DEBUG_PRINT_CALLINST
    #define debugLog(s) errs() << s << "\n"
#else
    #define debugLog(s)
#endif
//#define SPLIT_DEBUG

#define __ctStrCmp(x, y) strncmp(x, y, sizeof(y) - 1)

namespace llvm {
    class Contech;
    ModulePass* createContechPass();

    typedef struct _llvm_mem_op {
        bool isWrite;
        bool isGlobal;
        bool isDep;
        char size;
        unsigned short depMemOp;
        int depMemOpDelta;
        //Value* addr;
        struct _llvm_mem_op* next;
    } llvm_mem_op, *pllvm_mem_op;

    typedef struct _llvm_basic_block {
        unsigned int id, len, lineNum, numIROps, critPathLen;
        int32_t next_id;
        bool containCall;
        bool containGlobalAccess;
        bool containAtomic;
        pllvm_mem_op first_op;
        std::string fnName;
        std::string callFnName;
        //const char* fnName;
        const char* fileName;
        unsigned int fileNameSize;
    } llvm_basic_block, *pllvm_basic_block;

    typedef struct _llvm_inst_block {
        bool containQueueCall;
        bool hasCheck;
        bool preElide;
        bool hasElide;
        int cost;
        Instruction* insertPoint;
        Value* posValue;
    } llvm_inst_block, *pllvm_inst_block;
    
    typedef struct  _llvm_loopiv_block 
    {
        Instruction* memOp;     //memory op
        Instruction* memIV;     //corresponding IV
        const SCEV* startIV;    //start val of IV
        Loop* parentLoop;
        //const SCEV* iterCnt;    //loop iterations
        BasicBlock* blockID;      //BB name
        int stepIV;             //IV increment/decrement
        bool canElide;          //can the memory op be elided?
    } llvm_loopiv_block;
    
    typedef enum _CONTECH_FUNCTION_TYPE {
        NONE,
        MAIN,
        MALLOC,
        MALLOC2, // Calls like memalign(align, size)
        REALLOC,
        FREE,
        THREAD_CREATE,
        THREAD_JOIN,
        SYNC_ACQUIRE,
        SYNC_RELEASE,
        BARRIER,
        BARRIER_WAIT,
        EXIT,
        COND_WAIT,
        COND_SIGNAL,
        OMP_CALL,
        OMP_FORK,
        OMP_FOR_ITER,
        OMP_BARRIER,
        OMP_TASK_CALL,
        OMP_END,
        GLOBAL_SYNC_ACQUIRE, // Syncs that have no explicit address
        GLOBAL_SYNC_RELEASE,
        MPI_SEND_BLOCKING,
        MPI_RECV_BLOCKING,
        MPI_SEND_NONBLOCKING,
        MPI_RECV_NONBLOCKING,
        MPI_TRANSFER_WAIT,
        CILK_FRAME_CREATE,
        CILK_FRAME_DESTROY,
        CILK_SYNC,
        NUM_CONTECH_FUNCTION_TYPES
    } CONTECH_FUNCTION_TYPE;

    typedef struct _llvm_function_map {
        const char* func_name;
        size_t str_len;
        CONTECH_FUNCTION_TYPE typeID;
    } llvm_function_map, *pllvm_function_map;

    extern llvm_function_map functionsInstrument[];

    typedef struct _ConstantsCT {
        Constant* storeBasicBlockFunction;
        Constant* storeBasicBlockCompFunction;
        Constant* storeMemOpFunction;
        Constant* allocateBufferFunction;
        Constant* checkBufferFunction;
        Constant* storeThreadCreateFunction;
        Constant* storeSyncFunction;
        Constant* storeMemoryEventFunction;
        Constant* queueBufferFunction;
        Constant* storeBarrierFunction;
        Constant* allocateCTidFunction;
        Constant* allocateTicketFunction;
        Constant* getThreadNumFunction;
        Constant* storeThreadJoinFunction;
        Constant* storeThreadInfoFunction;
        Constant* storeBulkMemoryOpFunction;
        Constant* getCurrentTickFunction;
        Constant* createThreadActualFunction;
        Constant* checkBufferLargeFunction;
        Constant* getBufPosFunction;
        Constant* getBufFunction;
        Constant* writeElideGVEventsFunction;
        Constant* storeGVEventFunction;

        Constant* storeBasicBlockMarkFunction;
        Constant* storeMemReadMarkFunction;
        Constant* storeMemWriteMarkFunction;

        Constant* storeMPITransferFunction;
        Constant* storeMPIWaitFunction;

        Constant* ompThreadCreateFunction;
        Constant* ompThreadJoinFunction;
        Constant* ompTaskCreateFunction;
        Constant* ompTaskJoinFunction;
        Constant* ompPushParentFunction;
        Constant* ompPopParentFunction;
        Constant* ctPeekParentIdFunction;
        Constant* ompProcessJoinFunction;
        Constant* ompGetNestLevelFunction;

        Constant* ompGetParentFunction;
        Constant* ompPrepareTaskFunction;
        Constant* ompStoreInOutDepsFunction;

        Constant* cilkInitFunction;
        Constant* cilkCreateFunction;
        Constant* cilkSyncFunction;
        Constant* cilkRestoreFunction;
        Constant* cilkParentFunction;

        Constant* pthreadExitFunction;

        Type* int8Ty;
        Type* int32Ty;
        Type* voidTy;
        PointerType* voidPtrTy;
        Type* int64Ty;
        Type* pthreadTy;
        int pthreadSize;

        unsigned ContechMDID;
    } ConstantsCT, *pConstantsCT;

    //
    // Contech - First record every load or store in a program
    //
    class Contech : public ModulePass {
    public:
        static char ID; // Pass identification, replacement for typeid
        ConstantsCT cct;
        const DataLayout* currentDataLayout;

        std::set<Function*> contechAddedFunctions;
        std::set<Function*> ompMicroTaskFunctions;
        int lastAssignedElidedGVId;
        std::map<Constant*, uint16_t> elidedGlobalValues;
        std::unordered_map<Loop*, int> collectLoopEntry(Function* fblock, LoopInfo*);
        //std::vector <llvm_loopiv_block*> LoopMemoryOps;
        std::map<Value*, bool> loopMemOps;

        Contech() : ModulePass(ID) {
            lastAssignedElidedGVId = -1;
        }

        virtual bool doInitialization(Module &M);
        virtual bool runOnModule(Module &M);
        virtual bool internalRunOnBasicBlock(BasicBlock &B,    Module &M, int bbid, bool markOnly, const char* fnName, 
                                                           std::map<int, llvm_inst_block>& costOfBlock, int& num_checks, int& origin_check);
        virtual bool internalSplitOnCall(BasicBlock &B, CallInst**, int*);
        void addCheckAfterPhi(BasicBlock* B);
        bool checkAndApplyElideId(BasicBlock* B, uint32_t bbid, std::map<int, llvm_inst_block>& costOfBlock);
        int assignIdToGlobalElide(Constant*, Module&);
        bool attemptTailDuplicate(BasicBlock* bbTail);
        pllvm_mem_op insertMemOp(Instruction* li, Value* addr, bool isWrite, unsigned int memOpPos, Value*, bool elide, Module&);
        Value* convertValueToConstant(Value*, int*);
        int updateOffset(gep_type_iterator gepit, int val);
        unsigned int getSizeofType(Type*);
        unsigned int getSimpleLog(unsigned int);
        unsigned int getCriticalPathLen(BasicBlock& B);
        int getLineNum(Instruction* I);
        GetElementPtrInst* createGEPI(Type* t, Value* v, ArrayRef<Value*> ar, const Twine& tw, BasicBlock* B);
        GetElementPtrInst* createGEPI(Type* t, Value* v, ArrayRef<Value*> ar, const Twine& tw, Instruction* I);
        Function* createMicroTaskWrapStruct(Function* ompMicroTask, Type* arg, Module &M);
        Function* createMicroTaskWrap(Function* ompMicroTask, Module &M);
        Function* createMicroDependTaskWrap(Function* ompMicroTask, Module &M, size_t taskOffset, size_t numDep);
        Value* castSupport(Type*, Value*, Instruction*);
        Value* findCilkStructInBlock(BasicBlock& B, bool insert);
        bool blockContainsFunctionName(BasicBlock* B, _CONTECH_FUNCTION_TYPE cft);

        Value* findSimilarMemoryInst(Instruction*, Value*, int*);
        _CONTECH_FUNCTION_TYPE classifyFunctionName(const char* fn);

        void getAnalysisUsage(AnalysisUsage &AU) const;
        LoopInfo* getAnalysisLoopInfo(Function&);
        ScalarEvolution* getAnalysisSCEV(Function&);
        void collectLoopExits(Function* fblock, std::map<int, Loop*>& loopmap, LoopInfo*);
        Loop* isLoopEntry(BasicBlock* bb, std::unordered_set<Loop*>& lps);
        void collectLoopBelong(Function* fblock, std::map<int, Loop*>& loopmap, LoopInfo*);
        bool is_loop_computable(Instruction* memI, int* offset);

    }; // end of class Contech

}; // end namespace
    
#endif