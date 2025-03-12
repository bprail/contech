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

//#define DISABLE_MEM

#define __ctStrCmp(x, y) strncmp(x, y, sizeof(y) - 1)

namespace llvm {
    class Contech;
    ModulePass* createContechPass();
    extern DebugLoc defLoc;

    typedef struct _llvm_mem_op {
        bool isWrite;
        bool isGlobal;
        bool isDep;
        bool isLoopElide;
        bool isCrossPresv; // if isDep, then this is the elide, else this is the presv
        bool isTSC; // If a special op to record the TSC
        char size;
        union {
            unsigned short depMemOp;
            unsigned short loopMemOp;  // which base address for that header
            unsigned short crossBlockID;
        };
        
        int64_t depMemOpDelta;
        
        int loopIVSize;
        BasicBlock* loopHeaderId;  // which loop header
        
        Value* addr;
        Instruction* inst;
        struct _llvm_mem_op* next;
    } llvm_mem_op, *pllvm_mem_op;

    typedef struct _llvm_basic_block {
        unsigned int id, len, lineNum, numIROps, critPathLen;
        int32_t next_id; // TODO: can we get rid of this?
        std::vector<std::pair<int32_t, int32_t> > path_ids;
        bool containCall;
        bool containGlobalAccess;
        bool containAtomic;
        bool isLoopEntry;
        BasicBlock* isFuncExit; // NULL, not exit, other is id of entry block
        int crossOpCount; // N >= 0 if entry and num of ops, -1 o.w.
        int stepIV;
        uint32_t stepBlock;
        pllvm_mem_op first_op;
        std::string fnName;
        std::string callFnName;
        //const char* fnName;
        const char* fileName;
        unsigned int fileNameSize;
    } llvm_basic_block, *pllvm_basic_block;

    typedef struct _llvm_path_info {
        unsigned int id, pathDepth;
        std::vector<BasicBlock*> condBranchBlocks; // TODO: remove this.
        std::map<std::pair<BasicBlock*, BasicBlock*>, int> edgeValues;
    } llvm_path_info, *pllvm_path_info;
    
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
        Value* startIV;    //start val of IV
        BasicBlock* stepBlock;
        BasicBlock* headerBlock;
        // 4 is chosen from the loopUnroll code.
        SmallVector<BasicBlock*, 4> exitBlocks;  //
        std::vector<Value*> addtComponents;
        int stepIV;             // IV increment/decrement
        bool canElide;          // can the memory op be elided?
        bool wasElide;          // If the op can only be elided by loop code.
        bool loopInv;           // Op is loop invariant
    } llvm_loopiv_block, *pllvm_loopiv_block;
    
    typedef struct _llvm_loop_track
    {
        bool loopUsed;
        Value* startIV;
        BasicBlock* stepBlock;
        Instruction* memIV;
        int stepIV;
        // 4 is chosen from the loopUnroll code.
        SmallVector<BasicBlock*, 4> exitBlocks;
        std::vector<Value*> baseAddr;
        std::map<int, std::vector<std::pair<Value*, int> > > compMap;
    } llvm_loop_track, *pllvm_loop_track;
    
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
        MPI_BROADCAST,
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
        FunctionCallee storeBasicBlockFunction;
        FunctionCallee storeBasicBlockCompFunction;
        FunctionCallee extendPathInfoFunction;
        FunctionCallee storePathInfoFunction;
        FunctionCallee storeMemOpFunction;
        FunctionCallee allocateBufferFunction;
        FunctionCallee checkBufferFunction;
        FunctionCallee storeThreadCreateFunction;
        FunctionCallee storeSyncFunction;
        FunctionCallee storeMemoryEventFunction;
        FunctionCallee queueBufferFunction;
        FunctionCallee storeBarrierFunction;
        FunctionCallee allocateCTidFunction;
        FunctionCallee allocateTicketFunction;
        FunctionCallee getThreadNumFunction;
        FunctionCallee storeThreadJoinFunction;
        FunctionCallee storeThreadInfoFunction;
        FunctionCallee storeBulkMemoryOpFunction;
        FunctionCallee getCurrentTickFunction;
        FunctionCallee createThreadActualFunction;
        FunctionCallee checkBufferLargeFunction;
        FunctionCallee getBufPosFunction;
        FunctionCallee getBufFunction;
        FunctionCallee getBufPtrFunction;
        FunctionCallee writeElideGVEventsFunction;
        FunctionCallee storeGVEventFunction;
        FunctionCallee storeLoopEntryFunction;
        FunctionCallee storeLoopExitFunction;
        FunctionCallee storeLoopShortFunction;
        
        FunctionCallee storeMPITransferFunction;
        FunctionCallee storeMPIAllOneFunction;
        FunctionCallee storeMPIWaitFunction;

        FunctionCallee ompThreadCreateFunction;
        FunctionCallee ompThreadJoinFunction;
        FunctionCallee ompTaskCreateFunction;
        FunctionCallee ompTaskJoinFunction;
        FunctionCallee ompPushParentFunction;
        FunctionCallee ompPopParentFunction;
        FunctionCallee ctPeekParentIdFunction;
        FunctionCallee ompProcessJoinFunction;
        FunctionCallee ompGetNestLevelFunction;

        FunctionCallee ompGetParentFunction;
        FunctionCallee ompPrepareTaskFunction;
        FunctionCallee ompStoreInOutDepsFunction;

        FunctionCallee cilkInitFunction;
        FunctionCallee cilkCreateFunction;
        FunctionCallee cilkSyncFunction;
        FunctionCallee cilkRestoreFunction;
        FunctionCallee cilkParentFunction;

        FunctionCallee pthreadExitFunction;

        Type* int1Ty;
        Type* int8Ty;
        Type* int16Ty;
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
    private:
        void visitVertex(
            BasicBlock*, 
            std::vector<BasicBlock*>&, 
            std::map<BasicBlock*, unsigned char>&, 
            std::map<BasicBlock*, bool>);
    public:
        static char ID; // Pass identification, replacement for typeid
        ConstantsCT cct;
        const DataLayout* currentDataLayout;
        DominatorTree * DT;

        std::set<Function*> contechAddedFunctions;
        std::set<Function*> ompMicroTaskFunctions;
        int lastAssignedElidedGVId;
        std::map<Constant*, uint16_t> elidedGlobalValues;
        std::vector <llvm_loopiv_block*> LoopMemoryOps;
        std::map<Value*, int> loopMemOps;
        std::map<BasicBlock*, llvm_loop_track*> loopInfoTrack;
        std::map<BasicBlock*, llvm_basic_block*> cfgInfoMap;
        std::map<BasicBlock*, llvm_path_info> pathInfoMap;

        Contech() : ModulePass(ID) {
            lastAssignedElidedGVId = -1;
        }

        virtual bool doInitialization(Module &M);
        virtual bool runOnModule(Module &M);
        
        FunctionCallee getFunction(Module &M, const char* fname, const char* fmt, bool isVarg = false);
        Type* getTypeFromStr(const char ty);
        void setElideInBlock(BasicBlock*, Instruction*, bool);
        int chainBufferCalls(Function*, std::map<int, llvm_inst_block>&, int);
        
        virtual bool internalRunOnBasicBlock(BasicBlock &B, Module &M, int bbid, const char* fnName, 
                                             std::map<int, llvm_inst_block>& costOfBlock, int& num_checks, int& origin_check);
        virtual bool internalSplitOnCall(BasicBlock &B, CallInst**, int*);
        void addCheckAfterPhi(BasicBlock* B);
        bool checkAndApplyElideId(BasicBlock* B, uint32_t bbid, std::map<int, llvm_inst_block>& costOfBlock);
        int assignIdToGlobalElide(Constant*, Module&);
        bool attemptTailDuplicate(BasicBlock* bbTail);
        pllvm_mem_op insertMemOp(Instruction* li, Value* addr, bool isWrite, unsigned int memOpPos, 
                                 Value*, bool elide, Module&, std::map<llvm::Instruction*, int>&);
        Value* convertValueToConstantEx(Value*, int64_t*, int64_t*, Value*);
        int64_t updateOffsetEx(gep_type_iterator gepit, int64_t val, int64_t*);
        unsigned int getSizeofType(Type*);
        unsigned int getSimpleLog(unsigned int);
        unsigned int getCriticalPathLen(BasicBlock& B);
        int getLineNum(Instruction* I);
        Function* createMicroTaskWrapStruct(Function* ompMicroTask, Type* arg, Module &M);
        Function* createMicroTaskWrap(Function* ompMicroTask, Module &M);
        Function* createMicroDependTaskWrap(Function* ompMicroTask, Module &M, size_t taskOffset, size_t numDep);
        Value* castSupport(Type*, Value*, Instruction*);
        Value* castWalk(Value*);
        Value* findCilkStructInBlock(BasicBlock& B, bool insert);
        bool blockContainsFunctionName(BasicBlock* B, _CONTECH_FUNCTION_TYPE cft);
        bool verifyFunctionInvariant(Function*);

        void crossBlockCalculation(Function* F, std::map<int, llvm_inst_block>& costPerBlock);
        Value* findSimilarMemoryInstExt(Instruction*, Value*, int64_t*);
        Value* findSimilarMemoryInstExt(Instruction*, Value*, int64_t*, std::vector<Value*> *);
        _CONTECH_FUNCTION_TYPE classifyFunctionName(const char* fn);

        void getAnalysisUsage(AnalysisUsage &AU) const;
        LoopInfo* getAnalysisLoopInfo(Function&);
        ScalarEvolution* getAnalysisSCEV(Function&);
        void collectLoopExits(Function* fblock, std::map<int, Loop*>& loopmap, LoopInfo*);
        Loop* isLoopEntry(BasicBlock* bb, std::unordered_set<Loop*>& lps);
        void collectLoopBelong(Function* fblock, std::map<int, Loop*>& loopmap, LoopInfo*);
        int is_loop_computable(Instruction* memI, int* offset);
        std::unordered_map<Loop*, int> collectLoopEntry(Function* fblock, LoopInfo*);
        void addToLoopTrack(pllvm_loopiv_block llb, BasicBlock* bbid, Instruction*, Value* addr, unsigned short* memOpPos, int64_t* memOpDelta, int* loopIVSize);

    }; // end of class Contech

}; // end namespace
    
#endif
