//=-------------------- llvm/Support/DynamicAnalysis.h ------======= -*- C++ -*//
//
//                     The LLVM Compiler Infrastructure
//
//  Victoria Caparros Cabezas <caparrov@inf.ethz.ch>
//===----------------------------------------------------------------------===//


#ifndef LLVM_SUPPORT_DYNAMIC_ANALYSIS_H
#define LLVM_SUPPORT_DYNAMIC_ANALYSIS_H

#define DEBUG_TYPE "dynamic-analysis"
#define NDEBUG

#include "llvm/IR/CallSite.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"


#include "top-down-size-splay.hpp"

#include "../../common/taskLib/TaskGraph.hpp"

#include <iostream>
#include <map>
#include <list>
#include <stdarg.h>
#include <stdio.h>
#include <iomanip>
#include <fstream>
#include <random>

#include <deque>

//#define PRINT_ALL
//#define READ_BOTTLENECK

//#define DEBUG_TASK
//#define DEBUG_GENERIC
//#define DEBUG_OP_COVERAGE

#define ASSERT

/*The YMM registers are aliased over the older 128-bit XMM registers used for
 Intel SSE, treating the XMM registers as the lower half of the corresponding
 YMM register.
 */

#define INF -1



// Can be further broken down into FPMults, FPAdds, etc..
#define N_INST_TYPES 2

#define INT_ADD          0
#define INT_SUB          0
#define INT_MUL          0
#define INT_DIV          0

#define INT_REM         -1
#define INT_LD_4_BITS    1
#define INT_LD_8_BITS    1
#define INT_LD_16_BITS   1
#define INT_LD_32_BITS   1
#define INT_LD_64_BITS   1
#define INT_LD_80_BITS   1
#define INT_LD_128_BITS  1
#define INT_ST_4_BITS    1
#define INT_ST_8_BITS    1
#define INT_ST_16_BITS   1
#define INT_ST_32_BITS   1
#define INT_ST_64_BITS   1
#define INT_ST_80_BITS   1
#define INT_ST_128_BITS  1


#define FP_ADD           0
#define FP_SUB           0
#define FP_MUL           0
#define FP_DIV           0
#define FP_REM          -1
#define FP_LD_16_BITS    1
#define FP_LD_32_BITS    1
#define FP_LD_64_BITS    1
#define FP_LD_80_BITS    1
#define FP_LD_128_BITS   1
#define FP_ST_16_BITS    1
#define FP_ST_32_BITS    1
#define FP_ST_64_BITS    1
#define FP_ST_80_BITS    1
#define FP_ST_128_BITS   1
#define MISC_MEM        -1
#define CTRL            -1
#define VECTOR_SHUFFLE   0
#define MISC            -1

// =================== Sandy Bridge config ================================//


// These are execution units, o resources -> there is an available and
// and full occupancy tree from them
#define SANDY_BRIDGE_EXECUTION_UNITS 16 //9
#define SANDY_BRIDGE_NODES 28 //23
#define SANDY_BRIDGE_COMP_EXECUTION_UNITS 8 //4

#define SANDY_BRIDGE_DISPATCH_PORTS 5
#define SANDY_BRIDGE_BUFFERS 5
#define SANDY_BRIDGE_AGUS 2
#define SANDY_BRIDGE_LOAD_AGUS 0
#define SANDY_BRIDGE_STORE_AGUS 0
#define SANDY_BRIDGE_PREFETCH_NODES 3

#define SANDY_BRIDGE_MEM_EXECUTION_UNITS 8
#define SANDY_BRIDGE_AGU 1


enum {
    INT_ADDER = 0,
    INT_MULTIPLIER,
    INT_DIVIDER,
    INT_SHUFFLE,
    FP_ADDER,
    FP_MULTIPLIER,
    FP_DIVIDER,
    FP_SHUFFLE,
    L1_LOAD_CHANNEL,
    L1_STORE_CHANNEL,
    L2_LOAD_CHANNEL,
    L2_STORE_CHANNEL = L2_LOAD_CHANNEL,
    L3_LOAD_CHANNEL,
    L3_STORE_CHANNEL = L3_LOAD_CHANNEL,
    MEM_LOAD_CHANNEL,
    MEM_STORE_CHANNEL = MEM_LOAD_CHANNEL,
    ADDRESS_GENERATION_UNIT,
    PORT_0,
    PORT_1,
    PORT_2,
    PORT_3,
    PORT_4,
    RS_STALL,
    ROB_STALL,
    LB_STALL,
    SB_STALL,
    LFB_STALL,
    MAX_RESOURCE_VALUE
};

// Nodes for SANDY BRIDGE. Set to -1 those that
// do not exists


#define N_COMP_NODES_START INT_ADD_NODE
#define N_COMP_NODES_END VECTOR_SHUFFLE_NODE
#define SANDY_BRIDGE_COMP_NODES (1 + N_COMP_NODES_END - N_COMP_NODES_START)

#define N_MEM_NODES_START L1_LOAD_NODE
#define N_MEM_NODES_END MEM_STORE_NODE
#define SANDY_BRIDGE_MEM_NODES (1 + N_MEM_NODES_END - N_MEM_NODES_START)

#define N_AGU_NODES     1
#define N_PORT_NODES  (1 + PORT_4_NODE - PORT_0_NODE)

#define N_MISC_RESOURCES_START AGU_NODE
#define N_MISC_RESOURCES_END PORT_4_NODE
#define N_MISC_RESOURCES (1 + N_MISC_RESOURCES_END - N_MISC_RESOURCES_START)


#define N_BUFFER_NODES_START RS_STALL_NODE
#define N_BUFFER_NODES_END LFB_STALL_NODE
#define N_BUFFER_NODES (1 + N_BUFFER_NODES_END - N_BUFFER_NODES_START)

enum {
    INT_ADD_NODE = 0,
    INT_MUL_NODE,
    INT_DIV_NODE,
    INT_SHUFFLE_NODE,
    FP_ADD_NODE,
    FP_MUL_NODE,
    FP_DIV_NODE,
    VECTOR_SHUFFLE_NODE,
    
    L1_LOAD_NODE,
    L1_STORE_NODE,
    L2_LOAD_NODE,
    L2_STORE_NODE,
    L3_LOAD_NODE,
    L3_STORE_NODE,
    MEM_LOAD_NODE,
    MEM_STORE_NODE,

    AGU_NODE,
    PORT_0_NODE,
    PORT_1_NODE,
    PORT_2_NODE,
    PORT_3_NODE,
    PORT_4_NODE,

    RS_STALL_NODE,
    ROB_STALL_NODE,
    LB_STALL_NODE,
    SB_STALL_NODE,
    LFB_STALL_NODE,
    
    TOTAL_NODES
};

// ====================================================================//




#define N_TOTAL_NODES TOTAL_NODES


//Define Microarchitectures
#define SANDY_BRIDGE 0
#define ATOM 1

using namespace std;
using namespace SimpleSplayTree;
using namespace ComplexSplayTree;

// For FullOccupancyCyles, the vector has a different meaning that for AvailableCycles.
// Each element of the vector contains the elements of the tree in a corresponding
// rage.
static const int SplitTreeRange = 131072;

struct CacheLineInfo{
    uint64_t IssueCycle;
    uint64_t LastAccess;
};


struct InstructionDispatchInfo{
    uint64_t IssueCycle;
    uint64_t CompletionCycle;
};


struct LessThanOrEqualValuePred
{
    uint64_t CompareValue;
  
    bool operator()(const uint64_t Value) const
    {
        return Value <= CompareValue;
    }
};
    
struct StructMemberLessThanOrEqualThanValuePred
{
    const uint64_t CompareValue;

    bool operator()(const InstructionDispatchInfo& v) const
    {
        return v.CompletionCycle <= CompareValue;
    }
};
    

  
class TBV {
    
    private:
        vector<bool> BitVector;
        bool e;
    
    public:
        TBV();
        bool get_node(uint64_t key, unsigned bitPosition);
        bool get_node_nb(uint64_t key, unsigned bitPosition);
        void insert_node(uint64_t key, unsigned bitPosition);
        void delete_node(uint64_t key, unsigned bitPosition);
        bool empty();
};

struct ACTNode {
    public:
        uint64_t key;
        int32_t issueOccupancy;
        int32_t widthOccupancy;
        int32_t occupancyPrefetch;
        uint64_t address;
};
  
class ACT {
	private:
        vector< TBV> act_vec;
    
	public:
	    bool get_node(uint64_t, unsigned);
        void push_back(ACTNode*, unsigned);
        void DebugACT();
        size_t size();
        void clear();
};

uint64_t BitScan(vector< TBV> &FullOccupancyCyclesTree, uint64_t key, unsigned bitPosition);
    
class DynamicAnalysis {
    
private:
    // rd generates random values used as part of comm factor
    random_device rd;

    float CommFactorRead;
    float CommFactorWrite;
    
    // vector iterates over execution unit types
    // map maps number of in-flight instructions to number of issue cycles
    vector<map<uint64_t, unsigned> > histogram;

    #if 0
    //  records total recursive depth. Repititions are re-counted. Too slow?
    vector<uint64_t> ddgDepth;
    #endif

    unsigned TotalResources;
    unsigned nPorts;
    unsigned nAGUs;
    unsigned nNodes;
    unsigned nCompNodes;
    unsigned nMemNodes;
    unsigned MemoryWordSize;
    unsigned CacheLineSize;
    
    //Cache sizes are specified in number of cache lines of size CacheLineSize
    unsigned L1CacheSize;
    unsigned L2CacheSize;
    unsigned LLCCacheSize;
    
    unsigned BitsPerCacheLine;

    //For every node, the execution unit in which it executes.
    vector<unsigned> ExecutionUnit;
    vector<unsigned> ExecutionPort;
    vector<vector<unsigned> > DispatchPort;
    
    vector<unsigned> ExecutionUnitsLatency;
    
    vector<float> Throughputs;
    
    vector<string> NodesNames;
    
    uint64_t BasicBlockBarrier;
    int64_t RemainingInstructionsFetch;
    uint64_t InstructionFetchCycle;
    uint64_t LoadDispatchCycle;
    uint64_t StoreDispatchCycle;
    vector<uint64_t> ReservationStationIssueCycles;
    deque<uint64_t> ReorderBufferCompletionCycles;
    vector<uint64_t> LoadBufferCompletionCycles;
    SimpleTree<uint64_t> *LoadBufferCompletionCyclesTree;
    vector<uint64_t> StoreBufferCompletionCycles;
    vector<uint64_t> LineFillBufferCompletionCycles;
    vector<InstructionDispatchInfo> DispatchToLoadBufferQueue;
    ComplexTree<uint64_t> *DispatchToLoadBufferQueueTree;

    vector<InstructionDispatchInfo> DispatchToStoreBufferQueue;
    vector<InstructionDispatchInfo> DispatchToLineFillBufferQueue;
    
    bool ReportOnlyPerformance;
    
    int InstructionFetchBandwidth;
    
    uint8_t uarch;

    // Number of bytes transferred from/to L1_LD, L1_ST, L2, LLC, Mem, Total respectively
    // Depends on program
    vector<unsigned> Q;
 
#ifdef DEBUG_OP_COVERAGE
    // Record ops not modeled
    set<unsigned> ignoredOps;
#endif

    // Variables to track instructions count
    uint64_t TotalInstructions;
    uint64_t CycleOffset;
    uint64_t InstOffset;
    vector<uint64_t> InstructionsCount;
    vector<uint64_t> InstructionsCountExtended;
    vector<uint64_t> InstructionsSpan;
    vector<uint64_t> InstructionsLastIssueCycle;
    vector<uint64_t> IssueSpan;
    vector<uint64_t> SpanGaps;
    vector<uint64_t> FirstNonEmptyLevel;
    vector<uint64_t> BuffersOccupancy;
    vector<uint64_t> LastIssueCycleVector;
    
    vector<unsigned> MaxOccupancy;
    vector<bool> FirstIssue;
    
    uint64_t LastLoadIssueCycle;
    uint64_t LastStoreIssueCycle;
    
    vector< SplayTree::Tree<uint64_t> * > AvailableCyclesTree;
    ACT ACTFinal;
    
    vector< TBV> FullOccupancyCyclesTree; 
    
    #if DEBUG
    vector <uint64_t> NInstructionsStalled; // debug
    #endif
    
    uint64_t MinLoadBuffer;
    uint64_t MaxDispatchToLoadBufferQueueTree;
    
    // the following map (<value*>) are the most expensive for CPU cycles
    //  This maps instructions to their current issue cycle.  Its size is
    //    bounded by the number of unique static instructions in the basic
    //    blocks executed by a trace.
    map <llvm::Value*, uint64_t> InstructionValueIssueCycleMap;
    
    map <uint64_t , CacheLineInfo> CacheLineIssueCycleMap;
    map <uint64_t , uint64_t> MemoryAddressIssueCycleMap;
    SplayTree::Tree<uint64_t> * ReuseTree;
    
    map<int,int> ReuseDistanceDistribution;
    
public:
    unsigned nExecutionUnits;
    unsigned nCompExecutionUnits;
    unsigned nMemExecutionUnits;
    unsigned nBuffers;

    vector<float> ExecutionUnitsThroughput;
    vector<float> ExecutionUnitsBaselineThroughput;
    vector<int> ExecutionUnitsParallelIssue;

    unsigned* IssueCycleGranularities;
    unsigned* AccessWidths;
    unsigned* AccessGranularities;

    unsigned ReservationStationSize;
    unsigned ReorderBufferSize;
    unsigned LoadBufferSize;
    unsigned StoreBufferSize;
    unsigned LineFillBufferSize;

    vector<string> ResourcesNames;

    bool PerTaskCommFactor;
    // Computed once per task
    float TaskCFR;
    float TaskCFW;

    contech::TaskId residingTask;
    // Stores the bottleneck of the residing task
    vector<vector<float> > BnkMat;

    vector< dynamic_bitset<> > CGSFCache;
    vector< dynamic_bitset<> > CISFCache;
    
    //Constructor
    DynamicAnalysis(bool PerTaskCommFactor,
                    float CommFactorRead,
                    float CommFactorWrite,
                    string Microarchitecture,
                    unsigned MemoryWordSize,
                    unsigned CacheLineSize,
                    unsigned L1CacheSize,
                    unsigned L2CacheSize,
                    unsigned LLCCacheSize,
                    vector<float> ExecutionUnitsLatency,
                    vector<float> ExecutionUnitsThroughput,
                    vector<int> ExecutionUnitsParallelIssue,
                    vector<unsigned> MemAccessGranularity,
                    int InstructionFetchBandwidth,
                    int ReservationStationSize,
                    int ReorderBufferSize,
                    int LoadBufferSize,
                    int StoreBufferSize,
                    int LineFillBufferSize,
                    bool ReportOnlyPerformance);

    uint64_t analyzeInstruction (llvm::Instruction &I, uint64_t addr);
    
    void insertInstructionValueIssueCycle(llvm::Value* v,uint64_t InstructionIssueCycle, bool isPHINode = 0 );
    void insertCacheLineLastAccess(uint64_t v,uint64_t LastAccess );
    void insertCacheLineInfo(uint64_t v,CacheLineInfo Info );
    void insertMemoryAddressIssueCycle(uint64_t v,uint64_t Cycle );
    
    
    uint64_t getInstructionValueIssueCycle(llvm::Value* v);
    uint64_t getCacheLineLastAccess(uint64_t v);
    CacheLineInfo getCacheLineInfo(uint64_t v);
    uint64_t getMemoryAddressIssueCycle(uint64_t v);

    uint64_t GetLastIssueCycle(unsigned ExecutionResource);
        
    uint64_t GetTreeChunk(uint64_t i);
    
    //Returns the DAG level occupancy after the insertion
    unsigned FindNextAvailableIssueCycle(unsigned OriginalCycle, unsigned ExecutionResource);
    unsigned FindNextAvailableIssueCyclePortAndThroughtput(unsigned InstructionIssueCycle, unsigned ExtendedInstructionType);
    
    bool ThereIsAvailableBandwidth(unsigned NextAvailableCycle, unsigned ExecutionResource, bool& FoundInFullOccupancyCyclesTree, bool TargetLevel);
    
    uint64_t FindNextAvailableIssueCycleUntilNotInFullOrEnoughBandwidth(unsigned NextCycle, unsigned ExecutionResource , bool& FoundInFullOccupancyCyclesTree, bool& EnoughBandwidth);
    
    bool InsertNextAvailableIssueCycle(uint64_t NextAvailableCycle, unsigned ExecutionResource);
    
    void IncreaseInstructionFetchCycle(bool EmptyBuffers = false);
    
    
    // NEW FINAL VERSIONS
    uint64_t CalculateSpanFinal(int ResourceType);
    unsigned CalculateGroupSpanFinal(vector<int> & ResourcesVector);
    unsigned CalculateIssueSpanFinal(vector<int> & ResourcesVector);
    bool IsEmptyLevelFinal(unsigned ExecutionResource, uint64_t Level);

    unsigned CalculateResourceStallSpan(int resource, int stall);
    void CalculateResourceStallOverlapCycles(SplayTree::Tree<uint64_t> * n, int resource, uint64_t & OverlapCycles);

    unsigned GetExtendedInstructionType(int OpCode, int ReuseDistance=0);
            
    uint64_t GetMinIssueCycleReservationStation();
    uint64_t GetMinCompletionCycleLoadBuffer();
    uint64_t GetMinCompletionCycleLoadBufferTree();

    uint64_t GetMinCompletionCycleStoreBuffer();
    uint64_t GetMinCompletionCycleLineFillBuffer();
    
    void RemoveFromReservationStation(uint64_t Cycle);
    void RemoveFromReorderBuffer(uint64_t Cycle);
    void RemoveFromLoadBuffer(uint64_t Cycle);
    void RemoveFromLoadBufferTree(uint64_t Cycle);

    void RemoveFromStoreBuffer(uint64_t Cycle);
    void RemoveFromLineFillBuffer(uint64_t Cycle);
    
    void RemoveFromDispatchToLoadBufferQueue(uint64_t Cycle);
    void RemoveFromDispatchToLoadBufferQueueTree(uint64_t Cycle);
    void RemoveFromDispatchToStoreBufferQueue(uint64_t Cycle);
    void RemoveFromDispatchToLineFillBufferQueue(uint64_t Cycle);
    
    ComplexTree<uint64_t> * RemoveFromDispatchAndInsertIntoLoad(uint64_t i, ComplexTree<uint64_t> * t);
    ComplexTree<uint64_t> * inOrder(uint64_t i, ComplexTree<uint64_t> * n);
    
    void DispatchToLoadBuffer(uint64_t Cycle);
    void DispatchToLoadBufferTree(uint64_t Cycle);

    void DispatchToStoreBuffer(uint64_t Cycle);
    void DispatchToLineFillBuffer(uint64_t Cycle);
    
    uint64_t FindIssueCycleWhenLoadBufferIsFull();
    uint64_t FindIssueCycleWhenLoadBufferTreeIsFull();

    uint64_t FindIssueCycleWhenStoreBufferIsFull();
    uint64_t FindIssueCycleWhenLineFillBufferIsFull();
    
    void PrintReorderBuffer();
    void PrintReservationStation();
    void PrintLoadBuffer();
    void PrintLoadBufferTreeRecursive(SimpleTree<uint64_t> * p);
    void PrintDispatchToLoadBufferTreeRecursive(ComplexTree<uint64_t> * p, bool key);
    void PrintDispatchToLoadBufferTree();

    void PrintLoadBufferTree();
    void PrintStoreBuffer();
    void PrintLineFillBuffer();
    void PrintDispatchToStoreBuffer();
    void PrintDispatchToLoadBuffer();
    void PrintDispatchToLineFillBuffer();
    
    int ReuseDistance(uint64_t Last, uint64_t Current, uint64_t address);
    int ReuseTreeSearchDelete(uint64_t Current, uint64_t address);
    void updateReuseDistanceDistribution(int Distance, uint64_t InstructionIssueCycle);
    unsigned int roundNextPowerOfTwo(unsigned int v);
    unsigned int roundNextMultiple(uint64_t num, int multiple);
    unsigned int roundNextMultipleOf2(uint64_t num);
    unsigned int DivisionRoundUp(float a, float b);
    void resetAnalysis(); 
    void finishAnalysis(contech::TaskId taskId, bool, bool);
    void finishAnalysis(bool);
    void dumpHistogram();
    void dumpNuses();
    //void dumpDepth();
    void printHeaderStat(string Header);
    
    int getInstructionType(llvm::Instruction &I);
    void processNonPhiNode(llvm::Instruction& UseI, map<unsigned, uint64_t>* useDegree);
    void processPhiNode(llvm::Instruction& UseI, map<unsigned, uint64_t>* useDegree);
    
    void ComputeAvailableTreeFinalHelper(uint p, SplayTree::Tree<uint64_t>* t, uint d);
    void ComputeAvailableTreeFinal();
    void DebugACT(uint p);

    template<class T> void PrintArray(T* arr, size_t len);
    template<class T> void PrintVector(vector<T>& vec);
    void PrintMe(); 
};
#endif
