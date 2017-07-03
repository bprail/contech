#include "DynamicAnalysis.h"

#define DEBUG_TYPE "dynamic-analysis"

DynamicAnalysis::DynamicAnalysis(bool PerTaskCommFactor,
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
                                 vector<unsigned>    MemAccessGranularity,
                                 int InstructionFetchBandwidth,
                                 int ReservationStationSize,
                                 int ReorderBufferSize,
                                 int LoadBufferSize,
                                 int StoreBufferSize,
                                 int LineFillBufferSize,
                                 bool ReportOnlyPerformance)
{
    
    
    vector< unsigned > emptyVector;
    // =================== Sandy Bridge config ================================//
    
    if (Microarchitecture.compare("SB") == 0) {
        uarch = SANDY_BRIDGE;
        nExecutionUnits = SANDY_BRIDGE_EXECUTION_UNITS;
        nCompExecutionUnits = SANDY_BRIDGE_COMP_EXECUTION_UNITS;
        nMemExecutionUnits = SANDY_BRIDGE_MEM_EXECUTION_UNITS;
        nCompNodes = SANDY_BRIDGE_COMP_NODES;
        nMemNodes = SANDY_BRIDGE_MEM_NODES;
        nPorts = SANDY_BRIDGE_DISPATCH_PORTS;
        nBuffers = SANDY_BRIDGE_BUFFERS;
        nAGUs = SANDY_BRIDGE_AGU;
        nLoadAGUs = SANDY_BRIDGE_LOAD_AGUS;
        nStoreAGUs = SANDY_BRIDGE_STORE_AGUS;
        nNodes = SANDY_BRIDGE_NODES;
        
        // Mapping between nodes and execution units. ExecutionUnit[] vector contains
        // one entry for every type of node, and the associated execution unit.
        
        for (unsigned i = 0; i < nNodes; i++)
            ExecutionUnit.push_back(0);
        
        for (unsigned i = 0; i < 5; i++) {
            Q.push_back(0);
        }
        Q.push_back(0);

        ExecutionUnit[INT_ADD_NODE] = INT_ADDER;
        ExecutionUnit[INT_MUL_NODE] = INT_MULTIPLIER;
        ExecutionUnit[INT_DIV_NODE] = INT_DIVIDER;
        ExecutionUnit[INT_SHUFFLE_NODE] = INT_SHUFFLE;

        ExecutionUnit[FP_ADD_NODE] = FP_ADDER;
        ExecutionUnit[FP_MUL_NODE] = FP_MULTIPLIER;
        ExecutionUnit[FP_DIV_NODE] = FP_DIVIDER;
        ExecutionUnit[VECTOR_SHUFFLE_NODE] = FP_SHUFFLE;
        ExecutionUnit[L1_STORE_NODE] = L1_STORE_CHANNEL;
        ExecutionUnit[L1_LOAD_NODE] = L1_LOAD_CHANNEL;
        ExecutionUnit[L2_STORE_NODE] = L2_STORE_CHANNEL;
        ExecutionUnit[L2_LOAD_NODE] = L2_LOAD_CHANNEL;
        ExecutionUnit[L3_STORE_NODE] = L3_STORE_CHANNEL;
        ExecutionUnit[L3_LOAD_NODE] = L3_LOAD_CHANNEL;
        ExecutionUnit[MEM_STORE_NODE] = MEM_STORE_CHANNEL;
        ExecutionUnit[MEM_LOAD_NODE] = MEM_LOAD_CHANNEL;
        
        ExecutionUnit[AGU_NODE] = ADDRESS_GENERATION_UNIT ;
        
        ExecutionUnit[PORT_0_NODE] = PORT_0;
        ExecutionUnit[PORT_1_NODE] = PORT_1;
        ExecutionUnit[PORT_2_NODE] = PORT_2;
        ExecutionUnit[PORT_3_NODE] = PORT_3;
        ExecutionUnit[PORT_4_NODE] = PORT_4;
        
        // Mapping between execution units and dispatch ports
        
        for (unsigned i = 0; i < nCompNodes+nMemNodes; i++)
            DispatchPort.push_back(emptyVector);
        
        /*
         Port 0 -> FP_ADDER, INT_ADDER
         Port 1 -> FP_MULTIPLIER, FP_DIVIDER, FP_SHUFFLE, INT_MULTIPLIER, INT_DIVIDER, INT_SHUFFLE
         Port 2 -> STORE_CHANNEL (L1, L2, L3 and MEM)
         Port 3 -> LOAD (L1, L2, L3 and MEM)
         Port 4 -> LOAD (L1, L2, L3 and MEM)
         */
        // Associate Dispatch ports to nodes instead of execution resources. Because
        // otherwise there is a problem when different nodes share execution unit
        // but no dispatch ports
        
        emptyVector.push_back(0);
        DispatchPort[INT_ADD_NODE] = emptyVector;
        emptyVector.clear();
        emptyVector.push_back(1);
        DispatchPort[INT_MUL_NODE] = emptyVector;
        emptyVector.clear();
        emptyVector.push_back(1);
        DispatchPort[INT_DIV_NODE] = emptyVector;
        emptyVector.clear();
        emptyVector.push_back(1);
        DispatchPort[INT_SHUFFLE_NODE] = emptyVector;
        emptyVector.clear();

        emptyVector.push_back(0);
        DispatchPort[FP_ADD_NODE] = emptyVector;
        emptyVector.clear();
        emptyVector.push_back(1);
        DispatchPort[FP_MUL_NODE] = emptyVector;
        emptyVector.clear();
        emptyVector.push_back(1);
        DispatchPort[FP_DIV_NODE] = emptyVector;
        emptyVector.clear();
        emptyVector.push_back(1);
        DispatchPort[VECTOR_SHUFFLE_NODE] = emptyVector;
        emptyVector.clear();
        emptyVector.push_back(2);
        DispatchPort[L1_STORE_NODE] = emptyVector;
        DispatchPort[L2_STORE_NODE] = emptyVector;
        DispatchPort[L3_STORE_NODE] = emptyVector;
        DispatchPort[MEM_STORE_NODE] = emptyVector;
        emptyVector.clear();
        emptyVector.push_back(3);
        emptyVector.push_back(4);
        DispatchPort[L1_LOAD_NODE] = emptyVector;
        DispatchPort[L2_LOAD_NODE] = emptyVector;
        DispatchPort[L3_LOAD_NODE] = emptyVector;
        DispatchPort[MEM_LOAD_NODE] = emptyVector;
        
        for (unsigned i = 0; i < nPorts; i++) 
        {
            ExecutionPort.push_back(0);
        }
        
        ExecutionPort[0] = PORT_0;
        ExecutionPort[1] = PORT_1;
        ExecutionPort[2] = PORT_2;
        ExecutionPort[3] = PORT_3;
        ExecutionPort[4] = PORT_4;
    }
    
    // =================== Some general checking ================================//
    
    if (!ExecutionUnitsLatency.empty() && ExecutionUnitsLatency.size() != nExecutionUnits)
        report_fatal_error("The number of latencies does not match the number of execution units");
    
    if(!ExecutionUnitsThroughput.empty() && ExecutionUnitsThroughput.size() != nExecutionUnits)
        report_fatal_error("The number of throughputs does not match the number of execution units");
    
    if(!ExecutionUnitsParallelIssue.empty() && ExecutionUnitsParallelIssue.size() != nExecutionUnits)
        report_fatal_error("The number of execution units parallel issue does not match the number of execution units");
    // NUmber of ports is 5, therefore parallel issue can be atmost 5 
    if (L1CacheSize != 0 && L1CacheSize < CacheLineSize)
        report_fatal_error("L1 cache size < cache line size");
    if (L2CacheSize!= 0 && L2CacheSize < CacheLineSize)
        report_fatal_error("L2 cache size < cache line size");
    if (LLCCacheSize != 0 && LLCCacheSize < CacheLineSize)
        report_fatal_error("LLC cache size < cache line size");
    
    if (CacheLineSize % MemoryWordSize != 0)
        report_fatal_error("Cache line size is not a multiple of memory word size");
    
    // Initialize local variables with command-line arguemtns
    this->PerTaskCommFactor = PerTaskCommFactor;
    this->CommFactorRead = CommFactorRead;
    this->CommFactorWrite = CommFactorWrite;

    this->MemoryWordSize = MemoryWordSize;
    this->CacheLineSize = CacheLineSize/(this->MemoryWordSize);
    
    // If caches sizes are not multiple of a power of 2, force it.
    this->L1CacheSize = roundNextPowerOfTwo(L1CacheSize);
    this->L2CacheSize = roundNextPowerOfTwo(L2CacheSize);
    this->LLCCacheSize = roundNextPowerOfTwo(LLCCacheSize);
    this->L1CacheSize = this->L1CacheSize /CacheLineSize;
    this->L2CacheSize = this->L2CacheSize/CacheLineSize;
    this->LLCCacheSize = this->LLCCacheSize/CacheLineSize;
    
    this->ReservationStationSize = ReservationStationSize;
    this->InstructionFetchBandwidth = InstructionFetchBandwidth;
    this->ReorderBufferSize = ReorderBufferSize;
    this->LoadBufferSize = LoadBufferSize;
    this->StoreBufferSize = StoreBufferSize;
    this->LineFillBufferSize = LineFillBufferSize;
    this->ReportOnlyPerformance = ReportOnlyPerformance;
    
    
    LoadBufferCompletionCyclesTree = NULL;
    DispatchToLoadBufferQueueTree = NULL;
    MinLoadBuffer = 0;
    MaxDispatchToLoadBufferQueueTree = 0;
    
    BitsPerCacheLine = log2(this->CacheLineSize * (this->MemoryWordSize));
    
    // Make sure that there are no more parallel execution units that dispatch ports associated
    // to these units
    if (ExecutionUnitsParallelIssue.empty() == true) 
    {
        for (unsigned i = 0; i < nCompNodes +nMemNodes; i++) { // Dispatch ports are associated to nodes
            ExecutionUnitsParallelIssue.push_back(-1);
        }
    }
    for (unsigned i = 0; i < nCompNodes +nMemNodes; i++)  // Dispatch ports are associated to nodes
    { 
        if (ExecutionUnitsParallelIssue[ExecutionUnit[i]] > 0 && DispatchPort[i].size() < (unsigned)ExecutionUnitsParallelIssue[ExecutionUnit[i]]) {
            DEBUG(dbgs() << "ExecutionUnit " << i << "\n");
            DEBUG(dbgs() << "DispatchPort[i].size() " << DispatchPort[i].size() << "\n");
            DEBUG(dbgs() << "ExecutionUnitsParallelIssue[i] " << ExecutionUnitsParallelIssue[i] << "\n");
            report_fatal_error("There are more execution units that ports that can dispatch them\n");
        }
    }
    if (!MemAccessGranularity.empty() && MemAccessGranularity.size() != nMemExecutionUnits)
        report_fatal_error("Mem access granularities do not match the number of memory execution units");
    
    // We have latency and throughput for every resource in which we schedule nodes.
    // Latency and throughput of execution resources can be specified via command line.
    for (unsigned i = 0; i< nExecutionUnits; i++) 
    {
        this->ExecutionUnitsLatency.push_back(1); //Default value for latency
        this->ExecutionUnitsThroughput.push_back(-1); // Infinite throughput
        this->ExecutionUnitsBaselineThroughput.push_back(-1); // Infinite throughput
        this->ExecutionUnitsParallelIssue.push_back(1); // Infinite throughput
        
        if (i < nCompExecutionUnits) 
        {
            AccessGranularities.push_back(1);
        }
        else
        {
            AccessGranularities.push_back(this->MemoryWordSize);
        }

        map<uint64_t, unsigned> tempMap;
        this->histogram.push_back(tempMap);

        map<unsigned, uint64_t> tMap;
        this->nUsesHist.push_back(tMap);
//        this->ddgDepth.push_back(0);

        vector<float> BnkVec;
        for (unsigned j = 0; j < nBuffers + 2; ++j)
        {
            BnkVec.push_back(INF);
        } 
        this->BnkMat.push_back(BnkVec);
    }
    
    this->ExecutionUnitsBaselineThroughput[INT_ADDER]       = 3.0;
    this->ExecutionUnitsBaselineThroughput[INT_MULTIPLIER]  = 0.5;
    this->ExecutionUnitsBaselineThroughput[INT_DIVIDER]     = 1.0 / 73;
    this->ExecutionUnitsBaselineThroughput[INT_SHUFFLE]     = 1.0;
    this->ExecutionUnitsBaselineThroughput[FP_ADDER]       = 0.5;
    this->ExecutionUnitsBaselineThroughput[FP_MULTIPLIER]  = 0.5;
    this->ExecutionUnitsBaselineThroughput[FP_DIVIDER]     = 1.0 / 23;
    this->ExecutionUnitsBaselineThroughput[FP_SHUFFLE]     = 1.0;
    this->ExecutionUnitsBaselineThroughput[L1_LOAD_CHANNEL]     = 8.0;
    this->ExecutionUnitsBaselineThroughput[L1_STORE_CHANNEL]    = 8.0;
    this->ExecutionUnitsBaselineThroughput[L2_LOAD_CHANNEL]     = 32.0;
    this->ExecutionUnitsBaselineThroughput[L2_STORE_CHANNEL]    = 32.0;
    this->ExecutionUnitsBaselineThroughput[L3_LOAD_CHANNEL]     = 32.0;
    this->ExecutionUnitsBaselineThroughput[L3_STORE_CHANNEL]    = 32.0;
    this->ExecutionUnitsBaselineThroughput[MEM_LOAD_CHANNEL]    = 8.0;
    this->ExecutionUnitsBaselineThroughput[MEM_STORE_CHANNEL]   = 8.0;
    
    assert(!ExecutionUnitsLatency.empty());
    for (unsigned i = 0; i< nExecutionUnits; i++)
        this->ExecutionUnitsLatency[i] = ceil(ExecutionUnitsLatency[i]);
    
    if (!ExecutionUnitsThroughput.empty())
    {
        for (unsigned i = 0; i< nExecutionUnits; i++)
        {
            this->ExecutionUnitsThroughput[i] = ExecutionUnitsThroughput[i];
            assert(this->ExecutionUnitsThroughput[i] >= this->ExecutionUnitsBaselineThroughput[i]);
        }
    }
    
    if (!ExecutionUnitsParallelIssue.empty())
        for (unsigned i = 0; i< nExecutionUnits; i++)
            this->ExecutionUnitsParallelIssue[i] = ExecutionUnitsParallelIssue[i];
    
    if (!MemAccessGranularity.empty())
        for (unsigned i = 0; i< nMemExecutionUnits; i++)
            AccessGranularities[i+nCompExecutionUnits] = MemAccessGranularity[i];
    
    
    
    
    // Latency and throughput of AGUs
    if (nAGUs > 0) 
    {
        this->ExecutionUnitsLatency.push_back(1);
        
        this->ExecutionUnitsThroughput.push_back(-1);
        this->ExecutionUnitsParallelIssue.push_back(-1);
        AccessGranularities.push_back(1);
    }
    
    if (nLoadAGUs > 0) 
    {
        this->ExecutionUnitsLatency.push_back(1);
        this->ExecutionUnitsThroughput.push_back(1);
        this->ExecutionUnitsParallelIssue.push_back(nLoadAGUs);
        AccessGranularities.push_back(1);
    }
    
    if (nStoreAGUs > 0) 
    {
        this->ExecutionUnitsLatency.push_back(1);
        this->ExecutionUnitsThroughput.push_back(1);
        this->ExecutionUnitsParallelIssue.push_back(nStoreAGUs);
        AccessGranularities.push_back(1);
    }
    
    // Latency and throughput of ports
    for (unsigned i = 0; i< nPorts; i++) 
    {
        this->ExecutionUnitsLatency.push_back(1); //Default value for latency
        this->ExecutionUnitsThroughput.push_back(-1); // Infinite throughput
        this->ExecutionUnitsParallelIssue.push_back(-1);
        AccessGranularities.push_back(1);
    }
    
    // Latency and throughput of buffers. Although it has no effect, these
    // values are used
    for (unsigned i = 0; i< nBuffers; i++) 
    {
        this->ExecutionUnitsLatency.push_back(1); //Default value for latency
        this->ExecutionUnitsThroughput.push_back(1); // Infinite throughput
        this->ExecutionUnitsParallelIssue.push_back(1);
        AccessGranularities.push_back(1);
    }
    
    
    // We need AccessWidth and Throughput for every resource for which we calculate
    // span, including ports
    for (unsigned i = 0; i < nExecutionUnits + nAGUs +nPorts + nBuffers; i++) 
    {
        
        unsigned IssueCycleGranularity = 0;
        unsigned AccessWidth = 0;
        
        if (i < nCompExecutionUnits){
            AccessWidth = 1;
            // Computational units throughput must also be rounded
            if(this->ExecutionUnitsThroughput[i]!= INF){
                if (this->ExecutionUnitsThroughput[i] >= 1){
                    this->ExecutionUnitsThroughput[i] = roundNextMultiple(this->ExecutionUnitsThroughput[i] , 1);
                }
                
                
            }
        }else{
            if (i >= nCompExecutionUnits && i < nCompExecutionUnits + nMemExecutionUnits) {
                AccessWidth = roundNextMultiple(MemoryWordSize, AccessGranularities[i]);
                // Round throughput of memory resources to the next multiple of AccessWidth
                // (before it was MemoryWordSize)
                if (this->ExecutionUnitsThroughput[i]!= INF){
                    if (this->ExecutionUnitsThroughput[i] < AccessWidth){
                    // if (this->ExecutionUnitsThroughput[i] < this->MemoryWordSize){
                        if (this->ExecutionUnitsThroughput[i] < 1){
                            float Inverse =ceil(1/this->ExecutionUnitsThroughput[i]);
                            float Rounded =roundNextPowerOfTwo(Inverse);
                            
                            if (Inverse == Rounded) {
                                
                                this->ExecutionUnitsThroughput[i] = float(1)/float(Rounded);
                                
                            }else{
                                
                                this->ExecutionUnitsThroughput[i] = float(1)/float((Rounded/float(2)));
                            }
                        }else{
                            this->ExecutionUnitsThroughput[i] = roundNextPowerOfTwo(ceil(this->ExecutionUnitsThroughput[i]));
                        }
                    } else{
                        // Round to the next multiple of AccessGranularities...
                        //                    this->ExecutionUnitsThroughput[i] = roundNextMultiple(this->ExecutionUnitsThroughput[i],this->MemoryWordSize);
                        this->ExecutionUnitsThroughput[i] = roundNextMultiple(this->ExecutionUnitsThroughput[i],AccessGranularities[i]);
                    }
                }
            }else{
                AccessWidth = 1;
            }
        }
        
        if (this->ExecutionUnitsThroughput[i]>0) {
            DEBUG(dbgs() << "AccessWidth " << AccessWidth << "\n");
            DEBUG(dbgs() << "ExecutionUnitsThroughput[i] " << this->ExecutionUnitsThroughput[i] << "\n");
            IssueCycleGranularity = ceil(AccessWidth/this->ExecutionUnitsThroughput[i]);
        }else
            IssueCycleGranularity = 1;
        
        AccessWidths.push_back(AccessWidth);
        IssueCycleGranularities.push_back(IssueCycleGranularity);
        DEBUG(dbgs() << "IssueCycleGranularities["<<i<<"]=" << IssueCycleGranularities[i] << "\n");
    }
    
    
    
    DEBUG(dbgs() << "Number of resources " << nExecutionUnits + nPorts + nAGUs + nLoadAGUs + nStoreAGUs + nBuffers << "\n");
    
    ResourcesNames.push_back("INT_ADDER");
    ResourcesNames.push_back("INT_MULT");
    ResourcesNames.push_back("INT_DIV");
    ResourcesNames.push_back("INT_SHUF");
    ResourcesNames.push_back("FP_ADDER");
    ResourcesNames.push_back("FP_MULT");
    ResourcesNames.push_back("FP_DIV");
    ResourcesNames.push_back("FP_SHUF");
    ResourcesNames.push_back("L1_LD");
    ResourcesNames.push_back("L1_ST");
    ResourcesNames.push_back("L2");
    ResourcesNames.push_back("L3");
    ResourcesNames.push_back("MEM");
    
    /*
     ResourcesNames.push_back("L2_PREFETCH");
     ResourcesNames.push_back("L3_PREFETCH");
     ResourcesNames.push_back("MEM_PREFETCH");
     */
    ResourcesNames.push_back("AGU");
    
    ResourcesNames.push_back("PORT_0");
    ResourcesNames.push_back("PORT_1");
    ResourcesNames.push_back("PORT_2");
    ResourcesNames.push_back("PORT_3");
    ResourcesNames.push_back("PORT_4");
    
    ResourcesNames.push_back("RS");
    ResourcesNames.push_back("ROB");
    ResourcesNames.push_back("LB");
    ResourcesNames.push_back("SB");
    ResourcesNames.push_back("LFB");
    
    // Nodes names
    NodesNames.push_back("INT_ADD_NODE");
    NodesNames.push_back("INT_MUL_NODE");
    NodesNames.push_back("INT_DIV_NODE");
    NodesNames.push_back("INT_SHUFFLE_NODE");
    NodesNames.push_back("FP_ADD_NODE");
    NodesNames.push_back("FP_MUL_NODE");
    NodesNames.push_back("FP_DIV_NODE");
    NodesNames.push_back("VECTOR_SHUFFLE_NODE");
    NodesNames.push_back("L1_LOAD_NODE ");
    NodesNames.push_back("L1_STORE_NODE");
    NodesNames.push_back("L2_LOAD_NODE ");
    NodesNames.push_back("L2_STORE_NODE");
    NodesNames.push_back("L3_LOAD_NODE ");
    NodesNames.push_back("L3_STORE_NODE");
    NodesNames.push_back("MEM_LOAD_NODE ");
    NodesNames.push_back("MEM_STORE_NODE");
    
    NodesNames.push_back("AGU_NODE ");
    
    NodesNames.push_back("PORT_0_NODE");
    NodesNames.push_back("PORT_1_NODE");
    NodesNames.push_back("PORT_2_NODE");
    NodesNames.push_back("PORT_3_NODE");
    NodesNames.push_back("PORT_4_NODE");
    
    NodesNames.push_back("RS_STALL_NODE");
    NodesNames.push_back("ROB_STALL_NODE");
    NodesNames.push_back("LB_STALL_NODE");
    NodesNames.push_back("SB_STALL_NODE");
    NodesNames.push_back("LFB_STALL_NODE");
    
    //Some checks....
    if (ResourcesNames.size() !=    nExecutionUnits + nPorts + nAGUs + nLoadAGUs + nStoreAGUs + nBuffers)
        report_fatal_error("ResourcesNames does not match with number of resources");
    
    if (LoadBufferSize > 0 && ReservationStationSize == 0)
        report_fatal_error("RS cannot be zero if LB exists");
    
    if (StoreBufferSize > 0 && ReservationStationSize == 0)
        report_fatal_error("RS cannot be zero if SB exists");
    
    if (LineFillBufferSize > 0 && LoadBufferSize == 0)
        report_fatal_error("LB cannot be zero if LFB exists");
    
    //Check that access granularities are either memory word size or cache line size
    for (unsigned i = 0; i < MemAccessGranularity.size(); i++)
        if (MemAccessGranularity[i] != MemoryWordSize && MemAccessGranularity[i] != CacheLineSize )
            report_fatal_error("Memory access granularity is not memory word size, nor cache line size\n");
    
    // Itinitalize global variables to zero...
    CycleOffset = 0;
    InstOffset = 0;
    TotalInstructions = 0;
    BasicBlockBarrier = 0;
    BasicBlockLookAhead = 0;
    InstructionFetchCycle = 0;
    LoadDispatchCycle = 0;
    StoreDispatchCycle = 0;
    LastLoadIssueCycle = 0;
    LastStoreIssueCycle = 0;
    RemainingInstructionsFetch = InstructionFetchBandwidth;
    ReuseTree = NULL;
    
    // For resources with throughput and latency, i.e., resources for which we insert
    // cycles
    for (unsigned i = 0; i < nExecutionUnits + nPorts + nAGUs + nLoadAGUs + nStoreAGUs + nBuffers; i++) 
    {
        InstructionsCount.push_back(0);
        InstructionsCountExtended.push_back(0);
        InstructionsSpan.push_back(0);
        InstructionsLastIssueCycle.push_back(0);
        IssueSpan.push_back(0);
        SpanGaps.push_back(0);
        FirstNonEmptyLevel.push_back(0);
        MaxOccupancy.push_back(0);
        NInstructionsStalled.push_back(0);
        FirstIssue.push_back(false);
    }
    
    for (unsigned i = 0; i <nBuffers; i++)
        BuffersOccupancy.push_back(0);
    
    for (unsigned i = 0; i< nExecutionUnits + nPorts + nAGUs + nLoadAGUs + nStoreAGUs; i++)
        AvailableCyclesTree.push_back(NULL);    
    
    CGSFCache.resize(MAX_RESOURCE_VALUE);
    CISFCache.resize(MAX_RESOURCE_VALUE);
}
