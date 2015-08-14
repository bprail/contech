
#include "DynamicAnalysis.h"

// Main use is to check config
void
DynamicAnalysis::PrintMe()
{
    dbgs() << "Private Fields\n";

    //dbgs() << "CommFactorRead:\t" << CommFactorRead << "\n";
    //dbgs() << "CommFactorWrite:\t" << CommFactorWrite << "\n";

    //dbgs() << "TotalResources:\t" << TotalResources << "\n";
    //dbgs() << "nExecutionUnits:\t" << nExecutionUnits << "\n";
    //dbgs() << "nCompExecutionUnits:\t" << nCompExecutionUnits << "\n";
    //dbgs() << "nMemExecutionUnits:\t" << nMemExecutionUnits << "\n";
    //dbgs() << "nAGUs:\t" << nAGUs << "\n";
    //dbgs() << "nBuffers:\t" << nBuffers << "\n";
    //dbgs() << "nLoadAGUs:\t" << nLoadAGUs << "\n";
    //dbgs() << "nStoreAGUs:\t" << nStoreAGUs << "\n";
    //dbgs() << "nNodes:\t" << nNodes << "\n";
    //dbgs() << "nCompNodes:\t" << nCompNodes << "\n";
    //dbgs() << "nMemNodes:\t" << nMemNodes << "\n";
    //dbgs() << "MemoryWordSize:\t" << MemoryWordSize << "\n";
    //dbgs() << "CacheLineSize:\t" << CacheLineSize << "\n";

    //dbgs() << "L1CacheSize:\t" << L1CacheSize << "\n";
    //dbgs() << "L2CacheSize:\t" << L2CacheSize << "\n";
    //dbgs() << "LLCCacheSize:\t" << LLCCacheSize << "\n";
    
    //dbgs() << "BitsPerCacheLine:\t" << BitsPerCacheLine << "\n";

    dbgs() << "ExecutionUnitsLatency:\n";
    PrintVector(ExecutionUnitsLatency);

    //dbgs() << "ExecutionUnitsThroughput\n";
    //PrintVector(ExecutionUnitsThroughput);

    dbgs() << "ExecutionUnitsBaselineThroughput\n";
    PrintVector(ExecutionUnitsBaselineThroughput);

    //dbgs() << "ExecutionUnitsParallelIssue\n";
    //PrintVector(ExecutionUnitsParallelIssue);
    
    //dbgs() << "IssueCycleGranularities\n";
    //PrintVector(IssueCycleGranularities);

    //dbgs() << "AccessWidths\n";
    //PrintVector(AccessWidths);

    //dbgs() << "Throughputs\n";
    //PrintVector(Throughputs);

    //dbgs() << "AccessGranularities\n";
    //PrintVector(AccessGranularities);

    //dbgs() << "ResourcesNames\n";
    //PrintVector(ResourcesNames);

    //dbgs() << "ReservationStationSize:\t" << ReservationStationSize << "\n";
    //dbgs() << "InstructionFetchBandwidth:\t" << InstructionFetchBandwidth << "\n";
    //dbgs() << "ReorderBufferSize:\t" << ReorderBufferSize << "\n";
    //dbgs() << "LoadBufferSize:\t" << LoadBufferSize << "\n";
    //dbgs() << "StoreBufferSize:\t" << StoreBufferSize << "\n";
    //dbgs() << "LineFillBufferSize:\t" << LineFillBufferSize << "\n";


    dbgs() << "Public Fields\n";

    dbgs() << "nExecutionUnits:\t" << nExecutionUnits << "\n";
    dbgs() << "nCompExecutionUnits:\t" << nCompExecutionUnits << "\n";
    dbgs() << "nMemExecutionUnits:\t" << nMemExecutionUnits << "\n";
    dbgs() << "nBuffers:\t" << nBuffers << "\n";

    dbgs() << "ExecutionUnitsThroughput\n";
    PrintVector(ExecutionUnitsThroughput);

    dbgs() << "ExecutionUnitsParallelIssue\n";
    PrintVector(ExecutionUnitsParallelIssue);

    dbgs() << "IssueCycleGranularities\n";
    PrintVector(IssueCycleGranularities);

    dbgs() << "AccessWidths\n";
    PrintVector(AccessWidths);

    dbgs() << "AccessGranularities\n";
    PrintVector(AccessGranularities);

    dbgs() << "ReservationStationSize:\t" << ReservationStationSize << "\n";
    dbgs() << "ReorderBufferSize:\t" << ReorderBufferSize << "\n";
    dbgs() << "LoadBufferSize:\t" << LoadBufferSize << "\n";
    dbgs() << "StoreBufferSize:\t" << StoreBufferSize << "\n";
    dbgs() << "LineFillBufferSize:\t" << LineFillBufferSize << "\n";

    //dbgs() << "ResourcesNames\n";
    //PrintVector(ResourcesNames);

    //dbgs() << "PerTaskCommFactor:\t" << PerTaskCommFactor << "\n";
    //dbgs() << "TaskCFR:\t" << TaskCFR << "\n";
    //dbgs() << "TaskCFW:\t" << TaskCFW << "\n";

    dbgs() << "residingTask:\t" << residingTask.toString() << "\n";
}

template<class T> void
DynamicAnalysis::PrintVector(vector<T>& vec)
{
    for (auto I = vec.begin(), Ie = vec.end(); I != Ie; ++I)
    {
        cerr << std::setprecision(3) << *I << " ";    
    }
    dbgs() << "\n";
}

void
DynamicAnalysis::PrintReorderBuffer()
{    
    DEBUG(dbgs() << "Reorder Buffer:\n");
    for (unsigned i = 0; i < ReorderBufferCompletionCycles.size(); i++) 
    {
        DEBUG(dbgs() << ReorderBufferCompletionCycles[i] << " ");
    }
    DEBUG(dbgs() << "\n");
}



void
DynamicAnalysis::PrintReservationStation()
{    
    DEBUG(dbgs() << "Reservation Station:\n");
    for (unsigned i = 0; i < ReservationStationIssueCycles.size(); i++) 
    {
        DEBUG(dbgs() << ReservationStationIssueCycles[i] << " ");
    }
    DEBUG(dbgs() << "\n");
}



void
DynamicAnalysis::PrintLoadBuffer()
{    
    DEBUG(dbgs() << "Load Buffer:\n");
    for (unsigned i = 0; i < LoadBufferCompletionCycles.size(); i++) 
    {
        DEBUG(dbgs() << LoadBufferCompletionCycles[i] << " ");
    }
    DEBUG(dbgs() << "\n");
}

void
DynamicAnalysis::PrintLoadBufferTreeRecursive(SimpleTree<uint64_t> * p)
{
    if(p != NULL)
    {
        if(p->left) PrintLoadBufferTreeRecursive(p->left);
        if(p->right) PrintLoadBufferTreeRecursive(p->right);
        DEBUG(dbgs() <<" "<<p->key);
    }
    else return;
}


void
DynamicAnalysis::PrintDispatchToLoadBufferTreeRecursive(ComplexTree<uint64_t> * p, bool key)
{
    if (p != NULL)
    {
        if (p->left) PrintDispatchToLoadBufferTreeRecursive(p->left, key);
        if (p->right) PrintDispatchToLoadBufferTreeRecursive(p->right, key);
        if (key) 
        {
            DEBUG(dbgs() <<" "<<p->key);
        }
        else
            DEBUG(dbgs() <<" "<<p->IssueCycle);
        
    }
    else return;
}



void
DynamicAnalysis::PrintLoadBufferTree()
{    
    DEBUG(dbgs() << "Load Buffer Tree:\n");
    PrintLoadBufferTreeRecursive(LoadBufferCompletionCyclesTree);
    DEBUG(dbgs() << "\n");
}



void
DynamicAnalysis::PrintStoreBuffer()
{    
    DEBUG(dbgs() << "Store Buffer:\n");
    for (unsigned i = 0; i < StoreBufferCompletionCycles.size(); i++) 
    {
        DEBUG(dbgs() << StoreBufferCompletionCycles[i] << " ");
    }
    DEBUG(dbgs() << "\n");
}



void
DynamicAnalysis::PrintLineFillBuffer()
{
    DEBUG(dbgs() << "Line Fill Buffer:\n");
    for (unsigned i = 0; i < LineFillBufferCompletionCycles.size(); i++) 
    {
        DEBUG(dbgs() << LineFillBufferCompletionCycles[i] << " ");
    }
    DEBUG(dbgs() << "\n");
}



void
DynamicAnalysis::PrintDispatchToLoadBuffer()
{    
    DEBUG(dbgs() << "Dispatch to Load Buffer Issue Cycles:\n");
    for (unsigned i = 0; i < DispatchToLoadBufferQueue.size(); i++)
    {
        DEBUG(dbgs() << DispatchToLoadBufferQueue[i].IssueCycle <<    " ");
    }
    DEBUG(dbgs() << "\n");
    DEBUG(dbgs() << "Dispatch to Load Buffer Completion Cycles:\n");
    for (unsigned i = 0; i < DispatchToLoadBufferQueue.size(); i++) 
    {
        DEBUG(dbgs() << DispatchToLoadBufferQueue[i].CompletionCycle <<    " ");
    }
    DEBUG(dbgs() << "\n");
}


void
DynamicAnalysis::PrintDispatchToLoadBufferTree()
{    
    DEBUG(dbgs() << "Dispatch to Load Buffer Issue Cycles:\n");
    PrintDispatchToLoadBufferTreeRecursive(DispatchToLoadBufferQueueTree, false);
    DEBUG(dbgs() << "\n");
    DEBUG(dbgs() << "Dispatch to Load Buffer Completion Cycles:\n");
    PrintDispatchToLoadBufferTreeRecursive(DispatchToLoadBufferQueueTree, true);
    DEBUG(dbgs() << "\n");
}


void
DynamicAnalysis::PrintDispatchToStoreBuffer()
{    
    DEBUG(dbgs() << "Dispatch to Store Buffer Issue Cycles:\n");
    for (unsigned i = 0; i < DispatchToStoreBufferQueue.size(); i++) 
    {
        DEBUG(dbgs() << DispatchToStoreBufferQueue[i].IssueCycle <<    " ");
    }
    DEBUG(dbgs() << "\n");
    
    DEBUG(dbgs() << "Dispatch to Store Buffer Completion Cycles:\n");
    for (unsigned i = 0; i < DispatchToStoreBufferQueue.size(); i++) 
    {
        DEBUG(dbgs() << DispatchToStoreBufferQueue[i].CompletionCycle <<    " ");
    }
    DEBUG(dbgs() << "\n");
}



void
DynamicAnalysis::PrintDispatchToLineFillBuffer()
{    
    DEBUG(dbgs() << "Dispatch to Line Fill Buffer Issue Cycles:\n");
    for (unsigned i = 0; i < DispatchToLineFillBufferQueue.size(); i++) 
    {
        DEBUG(dbgs() << DispatchToLineFillBufferQueue[i].IssueCycle << " ");
    }
    DEBUG(dbgs() << "\n");
    
    DEBUG(dbgs() << "Dispatch to Line Fill Buffer Completion Cycles:\n");
    for (unsigned i = 0; i < DispatchToLineFillBufferQueue.size(); i++) 
    {
        DEBUG(dbgs() << DispatchToLineFillBufferQueue[i].CompletionCycle << " ");
    }
    DEBUG(dbgs() << "\n");
}

void
DynamicAnalysis::printHeaderStat(string Header){
    
    dbgs() << "//===--------------------------------------------------------------===//\n";
    dbgs() << "//                                         "<<Header <<"                                                                        \n";
    dbgs() << "//===--------------------------------------------------------------===//\n";
}

