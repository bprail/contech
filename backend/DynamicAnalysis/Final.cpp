#include "DynamicAnalysis.h"

using namespace llvm;
using namespace SplayTree;

/* An alternative to optimize calculateSpan could be merging the
 AvailableCyclesTree and FullOccupancyCyclesTree and doing and
 ca inorder/ postoder travesal */
uint64_t DynamicAnalysis::CalculateSpanFinal(int ResourceType)
{    
    uint64_t Span = 0;
    
    //If there are instructions of this type....
    if (InstructionsCountExtended[ResourceType] > 0) 
    {
        uint64_t Latency = ExecutionUnitsLatency[ResourceType];
        uint64_t First= FirstNonEmptyLevel[ResourceType];
        uint64_t DominantLevel = First;
        uint64_t LastCycle = LastIssueCycleVector[ResourceType];
        
        Span += Latency;
        
        //Start from next level to first non-emtpy level
        for (unsigned i = First + 1; i <= LastCycle; i += 1)
        {
            //Check whether there is instruction scheduled in this cycle!
            if (IsEmptyLevelFinal( ResourceType,i) == false) 
            {
                if ( DominantLevel+Latency != 0 && i <= (DominantLevel + Latency - 1))
                {
                    if (i + Latency > DominantLevel + Latency && Latency != 0) 
                    {
                        Span += ((i+Latency)-max((DominantLevel+Latency),(uint64_t)1));
                        DominantLevel = i;
                    }
                }
                else
                {
                    Span += Latency;
                    DominantLevel = i;
                }
            }
        }
    }
    
    return Span;
}

uint64_t
DynamicAnalysis::GetLastIssueCycle(unsigned ExecutionResource)
{    
    Tree<uint64_t> * NodeAvailable = NULL;
    unsigned IssueCycleGranularity = IssueCycleGranularities[ExecutionResource];
    uint64_t LastCycle = InstructionsLastIssueCycle[ExecutionResource];
    
    if (ExecutionResource <= nExecutionUnits)
    {    
        AvailableCyclesTree[ExecutionResource] = splay(LastCycle,AvailableCyclesTree[ExecutionResource]);
        NodeAvailable = AvailableCyclesTree[ExecutionResource];
        
        if ( NodeAvailable != NULL && 
             NodeAvailable->key== LastCycle && 
             NodeAvailable->issueOccupancy == 0 ) 
        {
            LastCycle = LastCycle-IssueCycleGranularity;
        }
    }
    return LastCycle;
}

bool
DynamicAnalysis::IsEmptyLevelFinal(unsigned ExecutionResource, uint64_t Level)
{
    if (ExecutionResource <= nExecutionUnits) 
    {    
        if (ACTFinal.get_node(Level, ExecutionResource))
        {
            return false;
        }
    }

    int TreeChunk = Level/SplitTreeRange;
    return !FullOccupancyCyclesTree[TreeChunk].get_node(Level, ExecutionResource);
}

unsigned
DynamicAnalysis::CalculateGroupSpanFinal(vector<int> & ResourcesVector)
{    
    unsigned Span = 0;
    unsigned MaxLatency = 0;
    uint64_t First = 0;
    bool EmptyLevel = true;
    bool IsGap = false;
    int NResources = ResourcesVector.size();
    uint64_t LastCycle = 0;
    uint64_t ResourceLastCycle = 0;
    unsigned MaxLatencyLevel = 0;
    unsigned ResourceType = 0;
    unsigned AccessWidth = 0;
    unsigned SpanIncrease = 0;
    
    for (unsigned i = 0; i < MAX_RESOURCE_VALUE; i++)
    {
        if (LastIssueCycleVector[i] > LastCycle) LastCycle = LastIssueCycleVector[i];
    }
    LastCycle += 100; // to be safe

    // Prepare a cache of values
    if (NResources == 1)
    {
        CGSFCache[ResourcesVector[0]].resize(LastCycle, false);
    }
    else
    {
        dynamic_bitset<> BitMesh(LastCycle);
        
        for (int j = 0; j < NResources; j++)
        {
            // Should probably recurse and calculate this value just in case
            if (CGSFCache[ResourcesVector[j]].size() == 0) 
            {
                vector<int> tv;
                tv.push_back(ResourcesVector[j]);
                CalculateGroupSpanFinal(tv);
            }
            BitMesh |= CGSFCache[ResourcesVector[j]];
        }

        //errs() << "BM: " << BitMesh.count() << "\n";

        return BitMesh.count();
    }
    LastCycle = 0;

    //Determine first non-empty level and LastCycle
    for (int j= 0; j< NResources; j++) 
    {    
        ResourceType = ResourcesVector[j];
        
        if (InstructionsCountExtended[ResourceType]>0) 
        {
            AccessWidth = AccessWidths[ResourceType];
            
            if (EmptyLevel == true) 
            { // This will be only executed the first time of a non-empty level
                EmptyLevel = false;
                First = FirstNonEmptyLevel[ResourceType];
                
                if (ExecutionUnitsThroughput[ResourceType] == INF) 
                {
                    MaxLatency = ExecutionUnitsLatency[ResourceType];
                }
                else
                {
                    MaxLatency = max(ExecutionUnitsLatency[ResourceType],(unsigned)ceil(AccessWidth/ExecutionUnitsThroughput[ResourceType]));
                }
            }
            else
            {
                if (First == FirstNonEmptyLevel[ResourceType])
                {
                    if (ExecutionUnitsThroughput[ResourceType] == INF) 
                    {
                        MaxLatency = max(MaxLatency,ExecutionUnitsLatency[ResourceType]);
                    }
                    else
                    {
                        MaxLatency = max(MaxLatency,max(ExecutionUnitsLatency[ResourceType],(unsigned)ceil(AccessWidth/ExecutionUnitsThroughput[ResourceType])));
                    }
                }
                else
                {
                    First = min(First,FirstNonEmptyLevel[ResourceType]);
                    if (First == FirstNonEmptyLevel[ResourceType])
                    {
                        if (ExecutionUnitsThroughput[ResourceType] == INF) 
                        {
                            MaxLatency =ExecutionUnitsLatency[ResourceType];
                        }
                        else
                        {
                            MaxLatency = max(ExecutionUnitsLatency[ResourceType],(unsigned)ceil(AccessWidth/ExecutionUnitsThroughput[ResourceType]));
                        }
                    }
                }
            }
            
            ResourceLastCycle = LastIssueCycleVector[ResourceType];
            LastCycle = max(LastCycle, ResourceLastCycle);
        }
    }
    
    unsigned DominantLevel = First;
    
    if (NResources == 1 && MaxLatency > 0)
    {
        //unsigned nBits = 0;
        for (unsigned q = 0; q < MaxLatency; q++)
        {
            CGSFCache[ResourcesVector[0]][First + q] = 1;
        }
    }    
    
    if (EmptyLevel == false) 
    {
        Span += MaxLatency;
        
        for(unsigned i = First + 1; i <= LastCycle; i++)
        {
            // For sure there is at least resource for which this level is not empty.
            //Determine MaxLatency of Level
            MaxLatencyLevel = 0;
            for(int j=0; j < NResources; j++)
            {
                ResourceType = ResourcesVector[j];
                
                if (i <= LastIssueCycleVector[ResourceType]) 
                {
                    if (IsEmptyLevelFinal(ResourceType, i) == false) 
                    {
                        IsGap = false;
                        // MaxLatencyLevel = max(MaxLatencyLevel, GetInstructionLatency(ResourcesVector[j]));
                        AccessWidth =AccessWidths[ResourceType];
                            
                        if (ExecutionUnitsThroughput[ResourceType] == INF) 
                        {
                            MaxLatencyLevel = max(MaxLatencyLevel, ExecutionUnitsLatency[ResourceType]);
                        }
                        else
                        {
                            MaxLatencyLevel = max(MaxLatencyLevel, max(ExecutionUnitsLatency[ResourceType],(unsigned)ceil(AccessWidth/ExecutionUnitsThroughput[ResourceType])));
                        }
                    }
                }
            }
            
            //That is, only if there are instructions scheduled in this cycle
            if (MaxLatencyLevel != 0)
            {    
                // Add the first condition because if Latency=0 is allowed, it can happen
                // that DominantLevel+MaxLatency-1 is a negative number, so the loop
                // is entered incorrectly.
                if ( DominantLevel+MaxLatency!= 0 && i <= DominantLevel+MaxLatency-1)
                {
                    if (i+MaxLatencyLevel > DominantLevel+MaxLatency && MaxLatencyLevel!=0) 
                    {
                        SpanIncrease = ((i+MaxLatencyLevel)-max((DominantLevel+MaxLatency),(unsigned)1)) ;
                        Span+=SpanIncrease;
                        DominantLevel = i;
                        MaxLatency = MaxLatencyLevel;
                    }
                }
                else
                {
                    SpanIncrease = MaxLatencyLevel;
                    Span += MaxLatencyLevel;
                    DominantLevel = i;
                    MaxLatency = MaxLatencyLevel;
                }
            }
            else
            {
                if ( i > (DominantLevel + MaxLatency - 1))
                {
                    if (NResources == 1 && IsGap == false) 
                    {
                        SpanGaps[ResourceType]++;
                        IsGap = true;
                    }
                }
            }
            
            if (NResources == 1 && MaxLatencyLevel > 0)
            {
                for (unsigned q = 0; q < MaxLatencyLevel; q++)
                {
                    CGSFCache[ResourcesVector[0]][i+q] = 1;
                }
            }
        }
    }
    
    // Delta should be 0
    unsigned delta = Span - CGSFCache[ResourcesVector[0]].count();
    if (delta != 0 && NResources == 1)
    {
        LastCycle = CGSFCache[ResourcesVector[0]].size() - 100;
        for (; delta != 0; delta--)
        {
            CGSFCache[ResourcesVector[0]][LastCycle + delta] = 1;
        }
    }

    return Span;
}

unsigned
DynamicAnalysis::CalculateIssueSpanFinal(vector<int> & ResourcesVector)
{    
    unsigned Span = 0;
    unsigned MaxLatency = 0;
    uint64_t First = 0;
    bool EmptyLevel = true;
    int NResources = ResourcesVector.size();
    uint64_t LastCycle = 0;
    uint64_t ResourceLastCycle = 0;
    unsigned MaxLatencyLevel = 0;
    unsigned ResourceType = 0;
    unsigned AccessWidth = 0;
    unsigned TmpLatency = 0;
    
    for (unsigned i = 0; i < MAX_RESOURCE_VALUE; i++)
    {
        if (LastIssueCycleVector[i] > LastCycle) LastCycle = LastIssueCycleVector[i];
    }
    LastCycle += 100; // to be safe

    // Prepare a cache of values
    if (NResources == 1)
    {
        CISFCache[ResourcesVector[0]].resize(LastCycle, false);
    }
    else
    {
        dynamic_bitset<> BitMesh(LastCycle);
        
        for (int j = 0; j < NResources; j++)
        {
            // Should probably recurse and calculate this value just in case
            if (CISFCache[ResourcesVector[j]].size() == 0) 
            {
                vector<int> tv;
                tv.push_back(ResourcesVector[j]);
                CalculateIssueSpanFinal(tv);
            }
            BitMesh |= CISFCache[ResourcesVector[j]];
        }

        return BitMesh.count();
    }
    
    //Determine first non-empty level and LastCycle
    for (int j = 0; j < NResources; j++) 
    {
        ResourceType = ResourcesVector[j];
        
        if (InstructionsCountExtended[ResourceType]>0) 
        {
            AccessWidth = AccessWidths[ResourceType];
            if (ExecutionUnitsThroughput[ResourceType] == INF)
            {
                TmpLatency = 1;
            }
            else
            {
                TmpLatency = ceil(AccessWidth/ExecutionUnitsThroughput[ResourceType]);
            }
            
            if (EmptyLevel == true) 
            { // This will be only executed the first time of a non-empty level
                EmptyLevel = false;
                First = FirstNonEmptyLevel[ResourceType];
                //        MaxLatency = ceil(AccessWidth/ExecutionUnitsThroughput[ResourceType]);
                MaxLatency = TmpLatency;
            }
            else
            {
                if (First == FirstNonEmptyLevel[ResourceType])
                {
                    MaxLatency = max(MaxLatency,TmpLatency);
                }
                else
                {
                    First = min(First,FirstNonEmptyLevel[ResourceType]);
                    if (First == FirstNonEmptyLevel[ResourceType])
                    {
                        MaxLatency = TmpLatency;
                    }
                }
            }
            ResourceLastCycle = LastIssueCycleVector[ResourceType];
            
            LastCycle = max(LastCycle, ResourceLastCycle);
        }
    }
    
    unsigned DominantLevel = First;
    if (NResources == 1 && MaxLatency > 0)
    {
        //unsigned nBits = 0;
        for (unsigned q = 0; q < MaxLatency; q++)
        {
            CISFCache[ResourcesVector[0]][First + q] = 1;
        }
    }
    
    if (EmptyLevel == false) 
    {
        Span += MaxLatency;
        
        //Start from next level to first non-empty level
        for (unsigned i = First + 1; i <= LastCycle; i++)
        {
            //Determine MaxLatency of Level
            MaxLatencyLevel = 0;
            for (int j = 0; j < NResources; j++)
            {
                ResourceType = ResourcesVector[j];
                
                if (i <= LastIssueCycleVector[ResourceType]) 
                {
                    if (IsEmptyLevelFinal(ResourceType, i) == false) 
                    {
                        AccessWidth = AccessWidths[ResourceType];
                        if (ExecutionUnitsThroughput[ResourceType] == INF)
                        {
                            TmpLatency = 1;
                        }
                        else
                        {
                            TmpLatency = ceil(AccessWidth/ExecutionUnitsThroughput[ResourceType]);
                        }
                        MaxLatencyLevel = max(MaxLatencyLevel, TmpLatency);
                    }
                }
            }
            
            //That is, only if there are instructions scheduled in this cycle
            if (MaxLatencyLevel != 0)
            {
                if ( i <= DominantLevel+MaxLatency-1)
                {
                    if (i+MaxLatencyLevel > DominantLevel+MaxLatency && MaxLatencyLevel!=0) 
                    {                  
                        Span += ((i+MaxLatencyLevel)-max((DominantLevel+MaxLatency),(unsigned)1));
                        DominantLevel = i;
                        MaxLatency = MaxLatencyLevel;
                    }
                }
                else
                {
                    Span += MaxLatencyLevel;
                    DominantLevel = i;
                    MaxLatency = MaxLatencyLevel;
                }
            }
            
            if (NResources == 1 && MaxLatencyLevel > 0)
            {
                for (unsigned q = 0; q < MaxLatencyLevel; q++)
                {
                    CISFCache[ResourcesVector[0]][i+q] = 1;
                }
            }
        }
    }
    
    assert(CISFCache[ResourcesVector[0]].count() == Span);
    
    return Span;
}

// Tree is unbalanced, switch from recursive to iterative method
void DynamicAnalysis::ComputeAvailableTreeFinalHelper(uint p, Tree<uint64_t>* t, uint d)
{
    uint64_t lastKey = 0;
    
    while (true)
    {
        if (t->left != NULL)
        {
            t->left->prev = t;
            t = t->left;
            continue;
        }
        // insert element
        if (t->key >= lastKey)
        {
            ACTNode* n = new ACTNode;
          
            n->key = t->key;
            n->issueOccupancy = t->issueOccupancy;
            n->widthOccupancy = t->widthOccupancy;
            n->occupancyPrefetch = t->occupancyPrefetch;
            n->address = t->address;
            ACTFinal.push_back(n, p);
            
            lastKey = t->key;
        }
        
        if (t->right != NULL)
        {
            t->right->prev = t;
            t = t->right;
            continue;
        }
        
        if (t->prev != NULL)
        {
            Tree<uint64_t>* old = t;
            t = t->prev;
            if (t->left == old) t->left = NULL;
            if (t->right == old) t->right = NULL;
            delete old;
            continue;
        }
        else
        {
            break;
        }
    }  
}

void DynamicAnalysis::ComputeAvailableTreeFinal()
{
    uint p = 0;
    
    for (auto it = AvailableCyclesTree.begin(), et = AvailableCyclesTree.end(); it != et; ++it)
    {
        if ((*it) != NULL)
        {
            DEBUG(dbgs() << "ACT convert on " << p << "\t" << (*it)->size << "\t");
            ComputeAvailableTreeFinalHelper(p, *it, 0);
            *it = NULL;
        }
        p++;
    }
}

void ACT::push_back(ACTNode* n, unsigned BitPosition)
{
    uint64_t i = n->key;
    uint64_t TreeChunk = i/SplitTreeRange;
    if (TreeChunk >= act_vec.size()) {
        act_vec.resize(TreeChunk+1);
    }
    
    bool cond = (n->issueOccupancy != 0); // Add optional prefetch conditional
    if (cond)
        act_vec[TreeChunk].insert_node(n->key, BitPosition);
    
    delete n;
}

bool ACT::get_node(uint64_t key, unsigned BitPosition)
{
    uint64_t TreeChunk = key / SplitTreeRange;
    if (TreeChunk >= act_vec.size())
    {
        return false;
    }
    
    return act_vec[TreeChunk].get_node(key, BitPosition);
}

size_t ACT::size()
{
    //return act_map.size();
    return 0;
}

void ACT::clear()
{
    act_vec.clear();
}

void DynamicAnalysis::DebugACT(uint p)
{
    /*errs() << "P: " << p << "\t";
    ACTFinal[p].DebugACT();*/
}

void ACT::DebugACT()
{
    /*errs() << act_map.size() << "\n";
    errs() << "currNode: " << &currNode ;
    if (currNode != act_map.end()) 
    {
        errs() << "\t" << *currNode << "\n";
        if (*currNode != NULL)
        {
            errs() << "key: " << (*currNode)->key << "\n";
        }
    }
    else {errs() << "\n";}*/
}

// Only SandyBridge based reset done after analysis
// ILP_SCHEDULING and MICROSCHEDULING assumed off
// Basically flushing pipeline and buffers
void 
DynamicAnalysis::resetAnalysis() 
{
    nExecutionUnits = SANDY_BRIDGE_EXECUTION_UNITS;
    nPorts = SANDY_BRIDGE_DISPATCH_PORTS;
    nAGUs = SANDY_BRIDGE_AGU;
    nBuffers = SANDY_BRIDGE_BUFFERS;

    BasicBlockBarrier = 0;
    RemainingInstructionsFetch = InstructionFetchBandwidth; 
    InstructionFetchCycle = 0;

    ReservationStationIssueCycles.clear();
    ReorderBufferCompletionCycles.clear();
    LoadBufferCompletionCycles.clear();

    delete_all(LoadBufferCompletionCyclesTree);
    LoadBufferCompletionCyclesTree = NULL;

    StoreBufferCompletionCycles.clear();
    LineFillBufferCompletionCycles.clear();
    DispatchToLoadBufferQueue.clear();

    delete_all(DispatchToLoadBufferQueueTree);
    DispatchToLoadBufferQueueTree = NULL;

    DispatchToStoreBufferQueue.clear();
    DispatchToLineFillBufferQueue.clear();

    for (unsigned i = 0; i < 5; i++) 
    {
        Q[i] = 0;
    }
    Q[5] = 0;

    #ifdef DEBUG_OP_COVERAGE
    ignoredOps.clear();
    #endif

    InstOffset += 2*ExecutionUnitsLatency[nExecutionUnits-1] + TotalInstructions;
    TotalInstructions = 0; // Reset => Reuse distance info lost
    CycleOffset += 2*ExecutionUnitsLatency[nExecutionUnits-1] + max(LastLoadIssueCycle, LastStoreIssueCycle);
    
    #ifdef DEBUG_GENERIC
    dbgs() << "InstOffset: " << InstOffset << "\n";
    dbgs() << "CycleOffset: " << CycleOffset << "\n";
    #endif

    //TotalSpan = 0;
    for (unsigned i = 0; i< TotalResources; i++) 
    {
        InstructionsCount[i] = 0;
        InstructionsCountExtended[i] = 0;
        InstructionsLastIssueCycle[i] = 0;
        IssueSpan[i] = 0;
        SpanGaps[i] = 0;
        FirstNonEmptyLevel[i] = 0;
        MaxOccupancy[i] = 0;
        FirstIssue[i] = false;
        #if DEBUG
        NInstructionsStalled[i] = 0;
        #endif
    }
    
    for (unsigned i = 0; i <nBuffers; i++) 
    {
        BuffersOccupancy[i] = 0;
    }
    
    LastIssueCycleVector.clear();

    LastLoadIssueCycle = 0;
    LastStoreIssueCycle = 0;

    // Don't leak memory by naively .clear()ing even though program semantics may not be affected at this point
    for (unsigned i = 0; i < nExecutionUnits + nPorts + nAGUs; i++) 
    {
        Tree<uint64_t>* tree = AvailableCyclesTree[i];
        delete_all(tree);
        AvailableCyclesTree[i] = NULL;    
    }

    ACTFinal.clear();

    FullOccupancyCyclesTree.clear();

    MinLoadBuffer = 0;
    MaxDispatchToLoadBufferQueueTree = 0;

    InstructionValueIssueCycleMap.clear();
    //CacheLineIssueCycleMap.clear(); // Clear => Reuse distance info lost
    MemoryAddressIssueCycleMap.clear(); // Load buffer dependency check dies with Task 
    //ReuseTree = NULL;

    for (unsigned i = 0; i < CGSFCache.size(); i++) 
    {
        CGSFCache[i].clear();
    }
    
    for (unsigned i = 0; i < CISFCache.size(); i++) 
    {
        CISFCache[i].clear();
    }
}

void
DynamicAnalysis::finishAnalysis(contech::TaskId taskId, bool reset, bool isBnkReqd) 
{
//    printHeaderStat(taskId.toString());
    dbgs() << taskId.toString() << "\n"; 

    residingTask = taskId;

    finishAnalysis(isBnkReqd);

    errs() << InstructionValueIssueCycleMap.size()  << "\n";
    
    if (reset)
    {
        resetAnalysis();
    }
}

void
DynamicAnalysis::finishAnalysis(bool isBnkReqd)
{        
    bool PrintWarning = false;
    unsigned long long TotalSpan = 0;
    uint64_t TotalStallSpan = 0;
    uint64_t PairSpan = 0;
    unsigned Span = 0;
    float Performance = 0;
    uint64_t Total;
    uint64_t T1, T2, OverlapCycles;
    vector<int> compResources;
    vector<int> memResources;
    vector<uint64_t> ResourcesSpan(TotalResources);
    vector<uint64_t> ResourcesTotalStallSpanVector(TotalResources);
    vector< vector<uint64_t> > ResourcesResourcesNoStallSpanVector(nExecutionUnits, vector<uint64_t>(nExecutionUnits));
    vector< vector<uint64_t> > ResourcesResourcesSpanVector(nExecutionUnits, vector<uint64_t>(nExecutionUnits));
    vector< vector<uint64_t> > ResourcesStallSpanVector(nExecutionUnits, vector<uint64_t>(nExecutionUnits));
    vector< vector<uint64_t> > StallStallSpanVector(nBuffers, vector<uint64_t>(nBuffers));
    vector< vector<uint64_t> > ResourcesIssueStallSpanVector(nExecutionUnits, vector<uint64_t>(nBuffers));
    
    // Increase FetchCycle until all buffers are empty
    while (ReservationStationIssueCycles.size() != 0 || 
           ReorderBufferCompletionCycles.size() != 0 || 
           LoadBufferCompletionCycles.size() != 0 || 
           StoreBufferCompletionCycles.size() != 0 || 
           LineFillBufferCompletionCycles.size() != 0) 
    {
        // In IncreaseInstructionFetchCycle(), InstructionFetchCycle only increases when
        // RS or ROB are full. But in this case, they may not get full, but we just
        // want to empty them
        // We don't increase fetch cycle here anymore because it is increased in the
        // function IncreaseInstructionFetchCycle() by setting the argument to true
        //    InstructionFetchCycle++;
        IncreaseInstructionFetchCycle(true);
    }
    
    for (unsigned i = 0; i < nCompNodes; i++)
    {
        compResources.push_back(i);
    }
    
    for (unsigned i = N_MEM_NODES_START ; i <= N_MEM_NODES_END; i++)
    {
        memResources.push_back(i);
    }
    
    for (unsigned j = 0; j < TotalResources; j++)
    {
        LastIssueCycleVector.push_back(GetLastIssueCycle(j));
        
        // If all buffers sizes are infinity, or a buffer does not get full, we don't
        // increment in the previous while loop InstructionFetchCycle. So this check
        // only makes sense when RS or ROB have limited size (because they hold
        // all type of instructions until they are issued)
        if (InstructionFetchCycle != 0 && 
            LastIssueCycleVector[j] > InstructionFetchCycle && 
            ReservationStationIssueCycles.size() != 0 && 
            ReorderBufferCompletionCycles.size() != 0) 
        {
            report_fatal_error("LastIssueCycle > InstructionFetchCycle for resource\n");
        }
    }

    #if 0
    for (unsigned j = 0; j < nExecutionUnits; ++j)
    {
        // ACT was just splayed for GetLastIssueCycle, so go backwards
        for (auto t = LastIssueCycleVector[j]; t >= 0; --t)
        {
            AvailableCyclesTree[j] = splay(t, AvailableCyclesTree[j]);
            Tree<uint64_t>* availableNode = AvailableCyclesTree[j]; 
            if (availableNode == NULL || availableNode->key != t)
            {
                unsigned TreeChunk = GetTreeChunk(t);
                if (FullOccupancyCyclesTree[TreeChunk].get_node((t), j))
                {
                    unsigned nFlight = ceil(ExecutionUnitsThroughput[j] * 1.0 / ExecutionUnitsBaselineThroughput[j]); 
                    histogram[j][nFlight] += nFlight;
                }
            }
            else
            {
                //dbgs() << ResourcesNames[j] << ":\t" << t << " (time) :\t " <<availableNode->issueOccupancy << "\n";
                histogram[j][availableNode->issueOccupancy] += availableNode->issueOccupancy; 
            }
            if (t == 0) break;
        }
    }
    #endif

    ComputeAvailableTreeFinal();

    if (!isBnkReqd)
    {
        unsigned long long _InstructionLatency = 0;
        uint64_t _LastCycle = 0;
        for (unsigned j = 0; j < nExecutionUnits; j++)
        {
            // If there are instructions of this type
            if (InstructionsCountExtended[j] > 0) 
            {
                _InstructionLatency = ExecutionUnitsLatency[j];
                
                _LastCycle = LastIssueCycleVector[j];
                TotalSpan = max(_LastCycle + _InstructionLatency, TotalSpan);
            }
        }
        dbgs() << "TOTAL FLOPS"<< "\t"<<InstructionsCount[0] <<"\t\t"<<CalculateGroupSpanFinal(compResources)<<" \n";
        dbgs() << "TOTAL MOPS"<< "\t"<<InstructionsCount[1]<<"\t\t"<<CalculateGroupSpanFinal(memResources)<<" \n";
        dbgs() << "TOTAL"<< "\t\t"<<InstructionsCount[0] +InstructionsCount[1]<<"\t\t"<<TotalSpan<<" \n";
        Performance = (float)InstructionsCount[0]/((float)TotalSpan);
        fprintf(stderr, "PERFORMANCE %1.3f\n", Performance);

        #ifdef DEBUG_TASK
        for (int i = 0; i < 5; i++) 
        {
            dbgs() << ResourcesNames[i+8] << " (bytes): " << Q[i] << "\n";
            Q[5] += Q[i];
        }
        dbgs() << "Total (bytes): " << Q[5] << "\n";
        #endif
        return;
    }

    {
        vector<int> tv;
        tv.resize(1);
        for (unsigned j = 0; j < nExecutionUnits; j++) 
        {
            tv[0] = j;
            IssueSpan[j] = CalculateIssueSpanFinal(tv);
            
            DEBUG(dbgs() << "Calculating group span for resource " << j << "\n");
            Span = CalculateGroupSpanFinal(tv);
            DEBUG(dbgs() << "Span " << Span << "\n");
            ResourcesSpan[j] = Span;
        }
    
        //Span for OOO buffers
        for (unsigned j = RS_STALL; j <= LFB_STALL; j++) 
        {
            tv[0] = j;
            IssueSpan[j] = CalculateIssueSpanFinal(tv);
            
            // Calculate span is an expensive operation. Therefore, whenever we can, we
            //   obtain the span from a different way.
            Span = InstructionsCountExtended[j];
            DEBUG(dbgs() << "Span    " << Span << "\n");
            ResourcesSpan[j] = Span;
            DEBUG(dbgs() << "Storing span    " <<    ResourcesSpan[j] << "\n");
    #ifdef ASSERT
            tv[0] = j;
            uint64_t CalculateSpanResult = CalculateSpanFinal(j);
            uint64_t CalculateGroupSpanResult = CalculateGroupSpanFinal(tv);
            DEBUG(dbgs() << "CalculateGroupSpanResult    " <<    CalculateGroupSpanResult << "\n");
            
            if (!(CalculateSpanResult == Span && Span == CalculateGroupSpanResult))
                report_fatal_error("Spans differ: Span (" + Twine(Span)+"), CalculateSpan ("+Twine(CalculateSpanResult)+
                                                     "), CalculateGroupSpan ("+Twine(CalculateGroupSpanResult)+")");
    #endif
        }
    }
    
#ifdef PRINT_ALL
    //Reuse Distance Distribution
    printHeaderStat("Reuse Distance distribution");
    
    map <int,int>::iterator ReuseDistanceMapIt;
    for(ReuseDistanceMapIt = ReuseDistanceDistribution.begin();
            ReuseDistanceMapIt != ReuseDistanceDistribution.end(); ++ReuseDistanceMapIt)
    {
        dbgs() << ReuseDistanceMapIt->first << " "<< ReuseDistanceMapIt->second<< "\n";
    }
    dbgs() << "DATA_SET_SIZE\t" << node_size(ReuseTree) << "\n";

    printHeaderStat("Statistics");
#endif
    
    unsigned long long InstructionLatency = 0;
    uint64_t LastCycle = 0;
    
    //================= Calculate total span ==========================//
    
    for (unsigned j = 0; j < nExecutionUnits; j++)
    {
        // If there are instructions of this type
        if (InstructionsCountExtended[j] > 0) 
        {
            InstructionLatency = ExecutionUnitsLatency[j];
            
            LastCycle = LastIssueCycleVector[j];
            TotalSpan = max(LastCycle + InstructionLatency, TotalSpan);
        }
    }
    
    // Calculate Resources-total stall span
    {
        vector<int> tv;
        tv.resize(1);
        for (uint j = RS_STALL; j <= LFB_STALL; j++)
        {
            if (InstructionsCountExtended[j] != 0) 
            {
                tv.push_back(j);
            }
        }
        
        for (unsigned i = 0; i < nExecutionUnits; i++)
        {
            tv[0] = i;
            
            ResourcesTotalStallSpanVector[i] = CalculateGroupSpanFinal(tv);
        }
    }
#ifdef PRINT_ALL
    //==================== Print resource statistics =============================//
    
    dbgs() << "RESOURCE\tN_OPS_ISSUED\tSPAN\t\tISSUE-SPAN\tSTALL-SPAN\t\tSPAN-GAPS\t\tMAX DAG LEVEL OCCUPANCY\n";
    
    for (unsigned j = 0; j < nExecutionUnits; j++)
    {
        dbgs() << ResourcesNames[j]<< "\t\t"<<InstructionsCountExtended[j]<<"\t\t"<<ResourcesSpan[j]<<"\t\t"<<IssueSpan[j]<<
                "\t\t"<<ResourcesTotalStallSpanVector[j] <<"\t\t"<< SpanGaps[j]<<"\t\t"<< MaxOccupancy[j] << " \n";
    }

    //==================== Print stall cycles =============================//
    
    printHeaderStat("Stall Cycles");
    
    dbgs() << "RESOURCE\tN_STALL_CYCLES\t\tAVERAGE_OCCUPANCY\n";
    
    for (int j = RS_STALL; j <= LFB_STALL; j++)
    {
        if (TotalSpan == 0) 
        {
            dbgs() << ResourcesNames[j]<< "\t\t" << ResourcesSpan[j] << "\t\t" << INF <<"\n";
        } 
        else
        {
            dbgs() << ResourcesNames[j]<< "\t\t" << ResourcesSpan[j] << "\t\t" << BuffersOccupancy[j-RS_STALL]/TotalSpan<<"\n";
        }
    }
    
    printHeaderStat("Span Only Stalls");
#endif // print all

    {
        vector<int> tv;
        for (unsigned i = RS_STALL; i <= LFB_STALL; i++) 
        {
            if (InstructionsCountExtended[i] > 0) 
            {
                // This TotalStallSpan is just in case there are only stalls from one buffer
                TotalStallSpan = ResourcesSpan[i];
                tv.push_back(i);
            }
        }
        if (tv.empty() == true) 
        {
            TotalStallSpan = 0;
        } 
        else
        {
            if (tv.size() != 1) 
            {
                TotalStallSpan = CalculateGroupSpanFinal(tv);
            }
        }
    }
    
#ifdef PRINT_ALL
    dbgs() << TotalStallSpan << "\n";
    
    
    //==================== Print port Occupancy =============================//
    
    printHeaderStat("Port occupancy");
    
    dbgs() << "PORT\t\tDISPATCH CYCLES\n";
    
    {
        vector<int> tv;
        tv.resize(1);
        for (int j = PORT_0; j <= PORT_4; j++)
        {
            tv[0] = j;
            dbgs() << ResourcesNames[j]<< "\t\t" << CalculateGroupSpanFinal(tv) << "\n";
        }
    }
#endif

    if (!ReportOnlyPerformance) 
    {
        //==================== Resource-Stall Span =============================//
        
#ifdef PRINT_ALL
        printHeaderStat("Resource-Stall Span");
        dbgs() << "RESOURCE";
        for(int j = RS_STALL; j <= LFB_STALL; j++){
            dbgs() << "\t"<<ResourcesNames[j];
        }
        dbgs() << "\n";
#endif
        {
            vector<int> tv(2);
            
            for (unsigned i = 0; i < nExecutionUnits; i++)
            {
                tv[0] = i;
    #ifdef PRINT_ALL
                dbgs() << ResourcesNames[i]<< "\t\t";
    #endif
                for (uint j = RS_STALL; j <= LFB_STALL; j++)
                {
                    if (InstructionsCountExtended[i] != 0 && InstructionsCountExtended[j] != 0 ) 
                    {
                        tv[1] = j;
                        
                        PairSpan = CalculateGroupSpanFinal(tv);
                    }
                    else
                    {
                        if (InstructionsCountExtended[i] == 0) 
                        {
                            PairSpan = InstructionsCountExtended[j];
                        }
                        else
                        {
                            if (InstructionsCountExtended[j]== 0 )
                            {
                                PairSpan = ResourcesSpan[i];
                            }
                        }
                    }
    #ifdef PRINT_ALL
                    dbgs() << PairSpan << "\t";
    #endif
                    ResourcesStallSpanVector[i][j-RS_STALL] = PairSpan; // Store the Span value
                }
    #ifdef PRINT_ALL
                dbgs() << "\n";
    #endif
            }
        }
        
        //==================== Resource-Stall Overlap =============================//
        
#ifdef PRINT_ALL
        printHeaderStat("Resource-Stall Overlap (0-1)");
        dbgs() << "RESOURCE";
        for (unsigned j = RS_STALL; j <= LFB_STALL; j++)
        {
            dbgs() << "\t"<<ResourcesNames[j];
        }
        dbgs() << "\n";
#endif
        
        float OverlapPercetage;
        for (unsigned i = 0; i < nExecutionUnits; i++)
        {
#ifdef PRINT_ALL
            dbgs() << ResourcesNames[i]<< "\t\t";
#endif
            for (uint j = RS_STALL; j <= LFB_STALL; j++)
            {
                if (InstructionsCountExtended[i] != 0 && 
                    InstructionsCountExtended[j] != 0 && 
                    ResourcesSpan[i] != 0 && 
                    ResourcesSpan[j] != 0)
                {
                    Total = ResourcesStallSpanVector[i][j-RS_STALL];
                    // When latency is zero, ResourcesSpan is zero. However, IssueSpan
                    // might not be zero.
                    T1 = ResourcesSpan[i];
                    T2 = ResourcesSpan[j];
                     assert(Total <= T1+T2);
                    OverlapCycles = T1+T2-Total;
                    OverlapPercetage = (float)OverlapCycles/(float(min(T1, T2)));
                    if (OverlapPercetage > 1.0) 
                    {
                        report_fatal_error("Overlap > 1.0 R-S overlap (0-1)");
                    }
                }
                else
                {
#ifdef PRINT_ALL
                    OverlapPercetage = 0;
#endif
                }
#ifdef PRINT_ALL
                fprintf(stderr, " %1.3f ", OverlapPercetage);
#endif
            }
#ifdef PRINT_ALL
            dbgs() << "\n";
#endif
        }
        
        //==================== ResourceIssue-Stall Span =============================//
#ifdef PRINT_ALL
        
        printHeaderStat("ResourceIssue-Stall Span");
        dbgs() << "RESOURCE";
        for(unsigned j = RS_STALL; j <= LFB_STALL; j++)
        {
            dbgs() << "\t"<<ResourcesNames[j];
        }
        dbgs() << "\n";
#endif
        {
            vector<int> tv(2);
            for(unsigned i = 0; i < nExecutionUnits; i++)
            {
                tv[0] = i;
    #ifdef PRINT_ALL
                dbgs() << ResourcesNames[i]<< "\t\t";
    #endif                        
                for (uint j = RS_STALL; j <= LFB_STALL; j++)
                {
                    if (InstructionsCountExtended[i] != 0 && InstructionsCountExtended[j] != 0 ) 
                    {
                        tv[1] = j;
                        PairSpan = CalculateIssueSpanFinal(tv);
                        ResourcesIssueStallSpanVector[i][j-RS_STALL] = PairSpan;
                    }
                    else
                    {
                        if (InstructionsCountExtended[i] == 0)
                        {
                            PairSpan = InstructionsCountExtended[j];
                            ResourcesIssueStallSpanVector[i][j-RS_STALL] = PairSpan;
                        }
                        else
                        {
                            if (InstructionsCountExtended[j]== 0 ) 
                            {
                                PairSpan = IssueSpan[i];
                                ResourcesIssueStallSpanVector[i][j-RS_STALL] = PairSpan;
                            }
                        }
                    }
    #ifdef PRINT_ALL
                    dbgs() << PairSpan << "\t";
    #endif
                }
    #ifdef PRINT_ALL
                dbgs() << "\n";
    #endif
            }
        }
        
        //==================== ResourceIssue-Stall Overlap =============================//
        
#ifdef PRINT_ALL
        printHeaderStat("ResourceIssue-Stall Overlap (0-1)");
        dbgs() << "RESOURCE";
        for(unsigned j=RS_STALL; j<= LFB_STALL; j++)
        {
            dbgs() << "\t"<<ResourcesNames[j];
        }
        dbgs() << "\n";
#endif
        float OverlapPercentage;
        
        for (unsigned i = 0; i < nExecutionUnits; i++)
        {
#ifdef PRINT_ALL
            dbgs() << ResourcesNames[i]<< "\t\t";
#endif
            for (uint j = RS_STALL; j <= LFB_STALL; j++)
            {
                if (InstructionsCountExtended[i] != 0 && InstructionsCountExtended[j] != 0)
                {
                    Total = ResourcesIssueStallSpanVector[i][j-RS_STALL];
                    T1 = IssueSpan[i];
                    T2 = InstructionsCountExtended[j];
                    assert(Total <= T1+T2);
                    OverlapCycles =    T1+T2-Total;
                    OverlapPercentage = (float)OverlapCycles/(float(min(T1, T2)));
                    if (OverlapPercentage > 1.0) 
                    {
                        report_fatal_error("Overlap > 1.0 RI-S Overlap (0-1)");
                    }
                }
                else
                {
#ifdef PRINT_ALL
                    OverlapPercentage = 0;
#endif
                }
#ifdef PRINT_ALL
                fprintf(stderr, " %1.3f ", OverlapPercentage);
#endif
            }
#ifdef PRINT_ALL
            dbgs() << "\n";
#endif
        }
        
        //==================== Resource-Resource Span =============================//
#ifdef PRINT_ALL
        
        printHeaderStat("Resource-Resource Span (resources span without stalls)");
        
        dbgs() << "RESOURCE";
        for (unsigned j = 0; j < nExecutionUnits; j++)
        {
            dbgs() << "\t"<<ResourcesNames[j];
        }
        dbgs() << "\n";
#endif

        {
            vector<int> tv(2);
            for (unsigned j = 1; j < nExecutionUnits; j++)
            {
                tv[0] = j;
    #ifdef PRINT_ALL
                dbgs() << ResourcesNames[j]<< "\t\t";
    #endif
                for (unsigned i = 0; i < j; i++)
                {
                    if (InstructionsCountExtended[i] != 0 && InstructionsCountExtended[j] != 0) 
                    {
                        tv[1] = i;
                        PairSpan = CalculateGroupSpanFinal(tv);
                    }
                    else
                    {
                        if (InstructionsCountExtended[i] == 0)
                        {
                            PairSpan = ResourcesSpan[j];
                        }
                        else if (InstructionsCountExtended[j] == 0)
                        {
                            PairSpan = ResourcesSpan[i];
                        }
                    }
                        
    #ifdef PRINT_ALL
                    dbgs() << PairSpan << "\t";
    #endif
                    ResourcesResourcesNoStallSpanVector[j][i] = PairSpan;
                }
    #ifdef PRINT_ALL
                dbgs() << "\n";
    #endif
            }
        }
        
#ifdef PRINT_ALL
        printHeaderStat("Resource-Resource Overlap Percentage (resources span without stall)");
        
        dbgs() << "RESOURCE";
        for (unsigned j = 0; j < nExecutionUnits; j++)
        {
            dbgs() << "\t"<<ResourcesNames[j];
        }
        dbgs() << "\n";
#endif
        
        for (unsigned j = 0; j < nExecutionUnits; j++)
        {
#ifdef PRINT_ALL
            dbgs() << ResourcesNames[j]<< "\t\t";
#endif
            for (unsigned i = 0; i < j; i++)
            {
                if (InstructionsCountExtended[i] != 0 &&
                    InstructionsCountExtended[j] !=0 && 
                    ResourcesSpan[j] != 0 && 
                    ResourcesSpan[j] != 0) 
                {
                    Total = ResourcesResourcesNoStallSpanVector[j][i];
                    T1 = ResourcesSpan[j];
                    T2 = ResourcesSpan[i];
                    OverlapCycles =    T1+T2-Total;
                    
                    OverlapPercetage = (float)OverlapCycles/(float(min(T1, T2)));
                    if (OverlapPercetage > 1.0) 
                    {
                        report_fatal_error("Overlap > 1.0 R-R overlap % (resources span without stall)");
                    }
                }
                else
                {
#ifdef PRINT_ALL
                    OverlapPercetage = 0;
#endif
                }
#ifdef PRINT_ALL
                fprintf(stderr, " %1.3f ", OverlapPercetage);
#endif
            }
#ifdef PRINT_ALL
            dbgs() << "\n";
#endif
        }
        
#ifdef PRINT_ALL
        printHeaderStat("Resource-Resource Span (resources span with stalls)");
#endif            
        vector<int> StallsVector;
        
#ifdef PRINT_ALL
        dbgs() << "RESOURCE";
        for (unsigned j = 0; j < nExecutionUnits; j++)
        {
            dbgs() << "\t"<<ResourcesNames[j];
        }
        dbgs() << "\n";
#endif
        {
            vector<int> tv(2);
            for (unsigned k = RS_STALL; k <= LFB_STALL; k++) 
            {
                tv.push_back(k);
            }
            
            for (unsigned j = 0; j < nExecutionUnits; j++)
            {
                tv[0] = j;
    #ifdef PRINT_ALL
                dbgs() << ResourcesNames[j]<< "\t\t";
    #endif
                for (unsigned i = 0; i < j; i++)
                {
                    if (InstructionsCountExtended[i] != 0 && InstructionsCountExtended[j] != 0) 
                    {
                        tv[1] = i;
                        PairSpan = CalculateGroupSpanFinal(tv);
                    }
                    else
                    {
                        if (InstructionsCountExtended[i] == 0)
                        {
                            PairSpan = TotalStallSpan;
                        }
                        else if (InstructionsCountExtended[j] == 0)
                        {
                            PairSpan = ResourcesTotalStallSpanVector[i];
                        }
                    }
                    
    #ifdef PRINT_ALL
                    dbgs() << PairSpan << "\t";
    #endif
                    ResourcesResourcesSpanVector[j][i] = PairSpan;
                }
    #ifdef PRINT_ALL
                dbgs() << "\n";
#endif
            }
        }
        
#ifdef PRINT_ALL
        printHeaderStat("Resource-Resource Overlap Percentage (resources span with stall)");
        
        dbgs() << "RESOURCE";
        for (unsigned j = 0; j < nExecutionUnits; j++)
        {
            dbgs() << "\t"<<ResourcesNames[j];
        }
        dbgs() << "\n";
#endif
        
        for(unsigned j = 0; j< nExecutionUnits; j++)
        {
#ifdef PRINT_ALL
            dbgs() << ResourcesNames[j]<< "\t\t";
#endif
            for (unsigned i = 0; i < j; i++)
            {
                if (InstructionsCountExtended[i] != 0 && 
                        InstructionsCountExtended[j] != 0 && 
                        ResourcesTotalStallSpanVector[j] != 0 && 
                        ResourcesTotalStallSpanVector[i] != 0) 
                {
                    Total = ResourcesResourcesSpanVector[j][i];
                    T1 = ResourcesTotalStallSpanVector[j];
                    T2 = ResourcesTotalStallSpanVector[i];
                    
                    assert(Total <= T1+T2);
                    OverlapCycles = T1+T2-Total;
                    OverlapPercetage = (float)OverlapCycles/(float(min(T1, T2)));
                    if (OverlapPercetage > 1.0) 
                    {
                        report_fatal_error("Overlap > 1.0 R-R overlap % (resources span with stall)");
                    }
                }
                else
                {
#ifdef PRINT_ALL
                    OverlapPercetage = 0;
#endif
                }
#ifdef PRINT_ALL
                fprintf(stderr, " %1.3f ", OverlapPercetage);
#endif
            }
#ifdef PRINT_ALL
            dbgs() << "\n";
#endif
        }
        
        
#ifdef PRINT_ALL
        printHeaderStat("Stall-Stall Span");
        
        dbgs() << "RESOURCE";
        for (unsigned j = RS_STALL; j <= LFB_STALL; j++)
        {
            dbgs() << "\t"<<ResourcesNames[j];
        }
        dbgs() << "\n";
#endif   
        {
            vector<int> tv(2);
            for (unsigned j = RS_STALL; j <= LFB_STALL; j++)
            {
                tv[0] = j;
    #ifdef PRINT_ALL
                dbgs() << ResourcesNames[j]<< "\t\t";
    #endif
                for (unsigned i = RS_STALL; i < j; i++)
                {
                    if (InstructionsCountExtended[j] != 0 && InstructionsCountExtended[i] != 0) 
                    {
                        tv[1] = i;
                        PairSpan = CalculateGroupSpanFinal(tv);
                    }
                    else
                    {
                        if (InstructionsCountExtended[i] == 0)
                        {
                            PairSpan = ResourcesSpan[j];
                        }
                        else if (InstructionsCountExtended[j] == 0)
                        {
                            PairSpan = ResourcesSpan[i];
                        }
                    }
    #ifdef PRINT_ALL
                    dbgs() << PairSpan << "\t";
    #endif
                    StallStallSpanVector[j-RS_STALL][i-RS_STALL] = PairSpan;
                }
    #ifdef PRINT_ALL
                dbgs() << "\n";
    #endif
            }
        }
        
#ifdef PRINT_ALL
        printHeaderStat("Stall-Stall Overlap Percentage ");
        
        dbgs() << "RESOURCE";
        for (unsigned j = RS_STALL; j <= LFB_STALL; j++)
        {
            dbgs() << "\t"<<ResourcesNames[j];
        }
        dbgs() << "\n";
#endif
        
        for (unsigned j = RS_STALL; j <= LFB_STALL; j++)
        {
#ifdef PRINT_ALL
            dbgs() << ResourcesNames[j]<< "\t\t";
#endif
            for (unsigned i = RS_STALL; i < j; i++)
            {
                if (InstructionsCountExtended[j] != 0 && InstructionsCountExtended[i] != 0) 
                {
                    Total = StallStallSpanVector[j-RS_STALL][i-RS_STALL];
                    T1 = ResourcesSpan[j];
                    T2 = ResourcesSpan[i];
                    assert(Total <= T1+T2);
#ifdef PRINT_ALL
                    OverlapCycles =    T1+T2-Total;
                    OverlapPercetage = (float)OverlapCycles/(float(min(T1, T2)));
                    
                }
                else
                {
                    OverlapPercetage = 0;
#endif
                }
#ifdef PRINT_ALL
                fprintf(stderr, " %1.3f ", OverlapPercetage);
#endif
            }
#ifdef PRINT_ALL
            dbgs() << "\n";
#endif
        }
        
        
#ifdef PRINT_ALL
        printHeaderStat("Bottlenecks");
#endif
        #ifndef READ_BOTTLENECK
        dbgs() << "Bottleneck\tISSUE\tLAT\t";
        for (int j = RS_STALL; j <= LFB_STALL; j++)
        {
            dbgs() << ResourcesNames[j] << "\t";
        }
        dbgs() << "\n";
        #endif
        uint64_t Work;
        
        for (unsigned i = 0; i < nExecutionUnits; i++)
        {
            auto& BnkVec = BnkMat[i];

            // Work is always the total number of floating point operations... Otherwise it makes
            // no sense to compare with the performance for memory nodes which is calcualted
            // with total work
            Work = InstructionsCount[0];
            #ifndef READ_BOTTLENECK
            dbgs() << ResourcesNames[i]<< "\t\t";
            #endif

            if (IssueSpan[i] > 0)
            {
                Performance = (float)Work/((float)IssueSpan[i]);
                #ifndef READ_BOTTLENECK
                fprintf(stderr, " %1.3f ", Performance);
                #endif

                BnkVec[0] = Performance;
            } 
            else
            {
                #ifndef READ_BOTTLENECK
                dbgs() << INF<<"\t";
                #endif
                
                BnkVec[0] = INF;
            }

            if (ResourcesSpan[i] > 0)
            {
                Performance = (float)Work/((float)ResourcesSpan[i]);
                #ifndef READ_BOTTLENECK
                fprintf(stderr, " %1.3f ", Performance);
                #endif

                BnkVec[1] = Performance;
            }
            else
            {
                #ifndef READ_BOTTLENECK
                dbgs() << INF<<"\t";
                #endif

                BnkVec[1] = INF;
            }

            for (unsigned j = 0; j < nBuffers; j++)
            {
                if (ResourcesIssueStallSpanVector[i][j] > 0 && ResourcesSpan[j+RS_STALL] != 0 )
                {
                    Performance = (float)Work/((float)ResourcesIssueStallSpanVector[i][j]);
                    #ifndef READ_BOTTLENECK
                    fprintf(stderr, " %1.3f ", Performance);
                    #endif

                    BnkVec[j + 2] = Performance;
                }
                else
                {
                    #ifndef READ_BOTTLENECK
                    dbgs() << INF<<"\t";
                    #endif

                    BnkVec[j + 2] = INF;
                }
            }
            #ifndef READ_BOTTLENECK
            dbgs() << "\n";
            #endif
        }
#ifdef PRINT_ALL
        printHeaderStat("Execution Times Breakdowns");
        dbgs() << "RESOURCE\tMIN-EXEC-TIME\tISSUE-EFFECTS\tLATENCY-EFFECTS\tSTALL-EFFECTS\tTOTAL\n";
#endif
        unsigned MinExecutionTime;
        unsigned IssueEffects;
        unsigned LatencyEffects;
        unsigned StallEffects;
        float Throughput = 0;
        
        for (unsigned i = 0; i < nExecutionUnits; i++)
        {
            if (InstructionsCountExtended[i] == 0) 
            {
                MinExecutionTime = 0;
                LatencyEffects = 0;
                IssueEffects = 0;
                StallEffects = ResourcesTotalStallSpanVector[i];
            }
            else
            {
                if (ExecutionUnitsParallelIssue[i] == INF) 
                {
                    if (ExecutionUnitsThroughput[i] == INF) 
                    {
                        Throughput = INF;
                    }
                    else
                    {
                        Throughput = ExecutionUnitsThroughput[i];
                    }
                }
                else
                {
                    if (ExecutionUnitsThroughput[i] == INF) 
                    {
                        Throughput = ExecutionUnitsParallelIssue[i];
                    }
                    else
                    {
                        Throughput = ExecutionUnitsThroughput[i]*ExecutionUnitsParallelIssue[i];
                    }
                }
                
                if (ExecutionUnitsLatency[i] == 0 && Throughput == INF)
                {
                    MinExecutionTime = 0;
                }
                else
                {
                    if (i < nCompExecutionUnits) 
                    {
                        if (Throughput == INF) 
                        {
                            MinExecutionTime = 1;
                        }
                        else
                        {
                            MinExecutionTime = (unsigned)ceil(InstructionsCountExtended[i]/Throughput);
                        }
                    }
                    else
                    {
                        if (Throughput == INF) 
                        {
                            MinExecutionTime = 1;
                        }
                        else
                        {
                            MinExecutionTime = (unsigned)ceil(InstructionsCountExtended[i]*AccessGranularities[i]/(Throughput));
                        }
                    }
                }
                
                if (IssueSpan[i] < MinExecutionTime) 
                {
                    PrintWarning = true;
                    IssueSpan[i] = MinExecutionTime;
                }
                IssueEffects = IssueSpan[i] - MinExecutionTime;
                
                if (ResourcesSpan[i] != 0) 
                {
                    LatencyEffects = ResourcesSpan[i] - IssueSpan[i];
                }
                
                StallEffects = ResourcesTotalStallSpanVector[i] - ResourcesSpan[i];
            }
            
#ifdef PRINT_ALL
            dbgs() << ResourcesNames[i]<< "\t\t";
            dbgs() << " " << MinExecutionTime;
            //    fprintf(stderr, " %1.3f ", MinExecutionTime);
            dbgs() << "\t";
            dbgs() << " " << IssueEffects;
            // fprintf(stderr, " %1.3f ", IssueEffects);
            dbgs() << "\t";
            dbgs() << " " << LatencyEffects;
            // fprintf(stderr, " %1.3f ", LatencyEffects);
            dbgs() << "\t";
            dbgs() << " " << StallEffects;
            // fprintf(stderr, " %1.3f ", StallEffects);
#endif
            if ((MinExecutionTime + IssueEffects + LatencyEffects + StallEffects != ResourcesTotalStallSpanVector[i]) && 
                MinExecutionTime!= 0) 
            {
                report_fatal_error("Breakdown of execution time does not match total execution time\n");
            }
            else
            {
#ifdef PRINT_ALL
                dbgs() << "\t"<< ResourcesTotalStallSpanVector[i]<<"\n";
#endif
            }
        }
        
        
        dbgs() << "TOTAL FLOPS"<< "\t"<<InstructionsCount[0] <<"\t\t"<<CalculateGroupSpanFinal(compResources)<<" \n";
        dbgs() << "TOTAL MOPS"<< "\t"<<InstructionsCount[1]<<"\t\t"<<CalculateGroupSpanFinal(memResources)<<" \n";
        dbgs() << "TOTAL"<< "\t\t"<<InstructionsCount[0] +InstructionsCount[1]<<"\t\t"<<TotalSpan<<" \n";
        Performance = (float)InstructionsCount[0]/((float)TotalSpan);
        fprintf(stderr, "PERFORMANCE %1.3f\n", Performance);
        if(PrintWarning == true)
            dbgs() << "WARNING: IssueSpan < MinExecutionTime\n";
        
        #ifndef READ_BOTTLENECK
        //#ifdef DEBUG_TASK
        for (int i = 0; i < 5; i++) 
        {
            dbgs() << ResourcesNames[i+8] << " (bytes): " << Q[i] << "\n";
            Q[5] += Q[i];
        }
        dbgs() << "Total (bytes): " << Q[5] << "\n";
        //#endif
        #endif

        //dumpHistogram();

        //dumpDepth();

    }
}

void
DynamicAnalysis::dumpHistogram()
{
    dbgs() << "Histogram\n";    
    for (unsigned i = 0; i < nExecutionUnits; ++i)
    {
        const auto& hist_map = histogram[i];
        dbgs() << ResourcesNames[i] << "\n";
        for (auto occupancy : hist_map)
        {
            dbgs() << "\t" << std::get<0>(occupancy) << ":\t" << std::get<1>(occupancy) << " issues\n";
        } 
    }
}

#if 0
void
DynamicAnalysis::dumpDepth()
{
    dbgs() << "DDG Degree\n";
    for (unsigned i = 0; i < nExecutionUnits; ++i)
    {
        dbgs() << ResourcesNames[i] << "\t" << ddgDepth[i] << " ddg depth total\n";
    }
}
#endif
