
//
//                                         The LLVM Compiler Infrastructure
//
//    Victoria Caparros Cabezas <caparrov@inf.ethz.ch>
//===----------------------------------------------------------------------===//

#include "DynamicAnalysis.h"

#define DEBUG_TYPE "dynamic-analysis"

//===----------------------------------------------------------------------===//
//                                General functions for the analysis
//===----------------------------------------------------------------------===//

#define HANDLE_MEMORY_TYPE(TY, type, operand)    \
Ty = I.getOperand(operand)->getType();    \
if(PointerType* PT = dyn_cast<PointerType>(Ty)){    \
switch (PT->getElementType()->getTypeID()) {    \
case Type::HalfTyID:    return FP_##TY##_16_BITS; \
case Type::FloatTyID:    return FP_##TY##_32_BITS;    \
case Type::DoubleTyID:    return FP_##TY##_64_BITS; \
case Type::X86_FP80TyID:    return FP_##TY##_80_BITS; \
case Type::PPC_FP128TyID:    return FP_##TY##_128_BITS; \
case Type::X86_MMXTyID:    return FP_##TY##_64_BITS;    \
case Type::VectorTyID: \
switch (PT->getElementType()->getVectorElementType()->getTypeID()) {    \
case Type::HalfTyID:    return FP_##TY##_16_BITS; \
case Type::FloatTyID:    \
return FP_##TY##_32_BITS;    \
case Type::DoubleTyID:    \
return FP_##TY##_64_BITS; \
case Type::IntegerTyID: \
switch (PT->getElementType()->getVectorElementType()->getIntegerBitWidth()) { \
case 4: return INT_##TY##_4_BITS; \
case 8: return INT_##TY##_8_BITS; \
case 16: return INT_##TY##_16_BITS; \
case 32: return INT_##TY##_32_BITS; \
case 64: return INT_##TY##_64_BITS; \
default: return MISC;    \
} \
default: return MISC;\
}\
case Type::IntegerTyID: \
IntegerTy = dyn_cast<IntegerType>(PT-> getElementType());    \
switch (IntegerTy -> getBitWidth()){    \
case 4: return INT_##TY##_4_BITS; \
case 8: return INT_##TY##_8_BITS; \
case 16: return INT_##TY##_16_BITS; \
case 32: return INT_##TY##_32_BITS; \
case 64: return INT_##TY##_64_BITS; \
default: return MISC;    \
} \
default: return MISC; \
} \
}else{    \
errs() << "Trying to load a non-pointer type.\n"; \
} \



// Copy from Instruction.cpp-getOpcodeName()
int
DynamicAnalysis::getInstructionType(Instruction &I)
{
    IntegerType *IntegerTy;
    Type* Ty;
    unsigned Opcode = I.getOpcode();
    
    switch (Opcode) {
            // Terminators
        case Instruction::Ret:        return CTRL;
        case Instruction::Br:         return CTRL;
        case Instruction::Switch: return CTRL;
        case Instruction::IndirectBr: return CTRL;
        case Instruction::Invoke: return CTRL;
        case Instruction::Resume: return CTRL;
        case Instruction::Unreachable: return CTRL;
            
            // Standard binary operators...
        case Instruction::Add: return INT_ADD;
        case Instruction::FAdd: return FP_ADD;
        case Instruction::Sub: return INT_SUB;
        case Instruction::FSub: return FP_SUB;
        case Instruction::Mul: return INT_MUL;
        case Instruction::FMul: return FP_MUL;
        case Instruction::UDiv: return INT_DIV;
        case Instruction::SDiv: return INT_DIV;
        case Instruction::FDiv: return FP_DIV;
        case Instruction::URem: return INT_DIV;
        case Instruction::SRem: return INT_DIV;
        case Instruction::FRem: return FP_DIV;
            
            // Logical operators...
        case Instruction::And: return INT_ADD;
        case Instruction::Or : return INT_ADD;
        case Instruction::Xor: return INT_ADD;
            
            // Memory instructions...
        case Instruction::Alloca:                return MISC_MEM;
        case Instruction::Load:                    HANDLE_MEMORY_TYPE(LD, Load, 0)
        case Instruction::Store:                 HANDLE_MEMORY_TYPE(ST, Store, 1);
        case Instruction::AtomicCmpXchg: return MISC_MEM;
        case Instruction::AtomicRMW:         return MISC_MEM;
        case Instruction::Fence:                 return MISC_MEM;
        case Instruction::GetElementPtr: return MISC_MEM;
            // Convert instructions...
        case Instruction::Trunc:         return MISC;
        case Instruction::ZExt:            return MISC;
        case Instruction::SExt:            return MISC;
        case Instruction::FPTrunc:     return MISC;
        case Instruction::FPExt:         return MISC;
        case Instruction::FPToUI:        return MISC;
        case Instruction::FPToSI:        return MISC;
        case Instruction::UIToFP:        return MISC;
        case Instruction::SIToFP:        return MISC;
        case Instruction::IntToPtr:    return MISC;
        case Instruction::PtrToInt:    return MISC;
        case Instruction::BitCast:     return MISC;
            
            // Other instructions...
        case Instruction::ICmp:                     return INT_ADD;
        case Instruction::FCmp:                     return FP_ADD;
        case Instruction::PHI:                        return CTRL;
        case Instruction::Select:                 return CTRL;
        case Instruction::Call:                     return CTRL;
        case Instruction::Shl:                        return INT_ADD;
        case Instruction::LShr:                     return INT_ADD;
        case Instruction::AShr:                     return INT_ADD;
        case Instruction::VAArg:                    return MISC;
        case Instruction::ExtractElement: return MISC;
        case Instruction::InsertElement:    return MISC;
        case Instruction::ShuffleVector:    return VECTOR_SHUFFLE;
        case Instruction::ExtractValue:     return MISC;
        case Instruction::InsertValue:        return MISC;
        case Instruction::LandingPad:         return MISC;
            
        default: return -1;
    }
}



//===----------------------------------------------------------------------===//
//                                    Routines for Analysis of ILP
//===----------------------------------------------------------------------===//

uint64_t
DynamicAnalysis::getInstructionValueIssueCycle(Value* v)
{    
    uint64_t InstructionIssueCycle = 0;
    map <Value*, uint64_t>::iterator IssueCycleMapIt;
    
    //&I
    IssueCycleMapIt = InstructionValueIssueCycleMap.find(v);
    if (IssueCycleMapIt != InstructionValueIssueCycleMap.end()) 
    {
        InstructionIssueCycle = IssueCycleMapIt->second;
        // Reset the value of issue cyle after reading it so that
        // when the next time this instruction is executed, it it not poluted
        // with a previous value. This problems arises when two instances of the
        // same instruction are represented by the same value.
        //    InstructionValueIssueCycleMap.erase(IssueCycleMapIt);
        IssueCycleMapIt->second = 0;
    }
    else
        InstructionIssueCycle = 0; // First usage
    
    return InstructionIssueCycle;
}



CacheLineInfo
DynamicAnalysis::getCacheLineInfo(uint64_t v)
{    
    CacheLineInfo Info;
    uint64_t CacheLineIssueCycle = 0;
    uint64_t CacheLineLastAccess = 0;
    map <uint64_t, CacheLineInfo>::iterator IssueCycleMapIt;
    
    //&I
    IssueCycleMapIt = CacheLineIssueCycleMap.find(v);
    if (IssueCycleMapIt != CacheLineIssueCycleMap.end()) 
    {
        CacheLineIssueCycle = IssueCycleMapIt->second.IssueCycle;
        CacheLineLastAccess = IssueCycleMapIt->second.LastAccess;
    }
    else
    {
        CacheLineIssueCycle = 0; // First usage
        CacheLineLastAccess = 0;
    }
    
    Info.LastAccess = CacheLineLastAccess;
    Info.IssueCycle = CacheLineIssueCycle;
    // Now this is moved to to getMemoryAddressIssueCycle
    // Info.IssueCycle = max(max(InstructionFetchCycle,BasicBlockBarrier),InstructionIssueCycle);
    
    return (Info);
}



uint64_t
DynamicAnalysis::getCacheLineLastAccess(uint64_t v)
{    
    uint64_t InstructionLastAccess = 0;
    map <uint64_t, CacheLineInfo>::iterator IssueCycleMapIt;
    
    //&I
    IssueCycleMapIt = CacheLineIssueCycleMap.find(v);
    if (IssueCycleMapIt != CacheLineIssueCycleMap.end())
        InstructionLastAccess = IssueCycleMapIt->second.LastAccess;
    
    return InstructionLastAccess;
}



uint64_t
DynamicAnalysis::getMemoryAddressIssueCycle(uint64_t v)
{
    uint64_t IssueCycle = CycleOffset;
    map <uint64_t, uint64_t>::iterator IssueCycleMapIt;
    
    //&I
    IssueCycleMapIt = MemoryAddressIssueCycleMap.find(v);
    if (IssueCycleMapIt != MemoryAddressIssueCycleMap.end())
        IssueCycle = IssueCycleMapIt->second;
    
    return IssueCycle;
}



void
DynamicAnalysis::insertInstructionValueIssueCycle(Value* v,uint64_t InstructionIssueCycle, bool isPHINode)
{    
    map <Value*, uint64_t>::iterator IssueCycleMapIt;
    
    IssueCycleMapIt = InstructionValueIssueCycleMap.find(v);
    if (IssueCycleMapIt != InstructionValueIssueCycleMap.end())
    {
        if (isPHINode == true)
            IssueCycleMapIt->second = InstructionIssueCycle;
        else
            IssueCycleMapIt->second = max(IssueCycleMapIt->second, InstructionIssueCycle/*+1*/);
    }
    else //Insert an entry for the instruction.
        InstructionValueIssueCycleMap[v] = InstructionIssueCycle/*+1*/;
    
}



void
DynamicAnalysis::insertCacheLineInfo(uint64_t v,CacheLineInfo Info)
{    
    map <uint64_t, CacheLineInfo>::iterator IssueCycleMapIt;
    //*i
    IssueCycleMapIt = CacheLineIssueCycleMap.find(v);
    if (IssueCycleMapIt != CacheLineIssueCycleMap.end())
    {
        IssueCycleMapIt->second.IssueCycle = max(IssueCycleMapIt->second.IssueCycle, Info.IssueCycle/*+1*/);
        IssueCycleMapIt->second.LastAccess = Info.LastAccess;
    }
    else
    { //Insert an entry for the instruction.
        CacheLineIssueCycleMap[v].IssueCycle = Info.IssueCycle/*+1*/;
        CacheLineIssueCycleMap[v].LastAccess = Info.LastAccess;
    }
}



void
DynamicAnalysis::insertCacheLineLastAccess(uint64_t v,uint64_t LastAccess)
{    
    map <uint64_t, CacheLineInfo>::iterator IssueCycleMapIt;
    //*i
    IssueCycleMapIt = CacheLineIssueCycleMap.find(v);
    if (IssueCycleMapIt != CacheLineIssueCycleMap.end()){
        IssueCycleMapIt->second.LastAccess = LastAccess;
    }else//Insert an entry for the instrucion.
        CacheLineIssueCycleMap[v].LastAccess = LastAccess;
}



void
DynamicAnalysis::insertMemoryAddressIssueCycle(uint64_t v,uint64_t Cycle )
{
    map <uint64_t, uint64_t>::iterator IssueCycleMapIt;
    //*i
    IssueCycleMapIt = MemoryAddressIssueCycleMap.find(v);
    if (IssueCycleMapIt != MemoryAddressIssueCycleMap.end()){
        IssueCycleMapIt->second = Cycle;
    }else //Insert an entry for the instrucion.
        MemoryAddressIssueCycleMap[v] = Cycle;
}


uint64_t
DynamicAnalysis::GetTreeChunk(uint64_t i)
{
    uint64_t TreeChunk = i/SplitTreeRange;
    if (TreeChunk >= FullOccupancyCyclesTree.size()) 
    {
        FullOccupancyCyclesTree.resize(TreeChunk+1);
    }
    return TreeChunk;
}

unsigned
DynamicAnalysis::FindNextAvailableIssueCyclePortAndThroughtput(unsigned InstructionIssueCycle, unsigned ExtendedInstructionType)
{    
    unsigned ExecutionResource = ExecutionUnit[ExtendedInstructionType];
    unsigned InstructionIssueCycleThroughputAvailable = InstructionIssueCycle;
    uint64_t InstructionIssueCycleFirstTimeAvailable = 0;
    uint64_t InstructionIssueCyclePortAvailable =InstructionIssueCycle;
    uint64_t Port = 0;
    bool FoundInThroughput = false;
    bool FoundInPort = false;
    
    while (FoundInThroughput == false || FoundInPort == false) 
    {
        // First, find next available issue cycle based on node throughput
        InstructionIssueCycleThroughputAvailable = FindNextAvailableIssueCycle(InstructionIssueCyclePortAvailable, ExecutionResource);
        if(InstructionIssueCycleThroughputAvailable == InstructionIssueCyclePortAvailable)
            FoundInThroughput = true;
        
        // Check that the port is available
        // Get the ports to which this node binds
        for (unsigned i = 0; i < DispatchPort[ExtendedInstructionType].size(); i++) 
        {
            InstructionIssueCyclePortAvailable = FindNextAvailableIssueCycle(InstructionIssueCycleThroughputAvailable, ExecutionPort[DispatchPort[ExtendedInstructionType][i]]);
            
            if (InstructionIssueCyclePortAvailable!=InstructionIssueCycleThroughputAvailable)
            {
                if (i==0)
                    InstructionIssueCycleFirstTimeAvailable = InstructionIssueCyclePortAvailable;
                else
                {
                    InstructionIssueCyclePortAvailable = min(InstructionIssueCyclePortAvailable, InstructionIssueCycleFirstTimeAvailable);
                    if (InstructionIssueCyclePortAvailable == InstructionIssueCycleFirstTimeAvailable) 
                    {
                        Port = i;
                        if (InstructionIssueCyclePortAvailable == InstructionIssueCycleThroughputAvailable) 
                        {
                            FoundInPort = true;
                            break;
                        }
                    }
                }
            }
            else
            {
                // If Node is NULL, it is available for sure.
                DEBUG(dbgs() << "Node is NULL, so port is "<<DispatchPort[ExtendedInstructionType][i] <<"\n");
                FoundInPort = true;
                Port = i;
                break;
            }
        }
    }
    
    //Insert issue cycle in Port and in resource
    InsertNextAvailableIssueCycle(InstructionIssueCyclePortAvailable, ExecutionPort[DispatchPort[ExtendedInstructionType][Port]]);
    
    // Insert in resource!!!
    InsertNextAvailableIssueCycle(InstructionIssueCyclePortAvailable, ExecutionResource);
    
    return InstructionIssueCyclePortAvailable;
}


bool
DynamicAnalysis::ThereIsAvailableBandwidth(unsigned NextAvailableCycle, unsigned ExecutionResource, bool& FoundInFullOccupancyCyclesTree, bool TargetLevel)
{    
    bool EnoughBandwidth;
    unsigned IssueCycleGranularity = 0;
    unsigned TmpTreeChunk;
    if (TargetLevel == true && FoundInFullOccupancyCyclesTree == false) 
    {
        IssueCycleGranularity = IssueCycleGranularities[ExecutionResource];
        
        // Assume initially that there is enough bandwidth
        EnoughBandwidth = true;
        
        //There is enough bandwidth if:
        // 1. The comp/load/store width fits within the level, or the level is empty.
        // 2. If IssueCycleGranularity > 1, we have to make sure that there were no instructions
        // executed with the same IssueCycleGranularity in previous cycles. We have to do this
        // because we don't include latency cycles in AvailableCyclesTree.
        int64_t StartingCycle = 0;
        
        int64_t tmp = (int64_t)NextAvailableCycle -(int64_t)IssueCycleGranularity+(int64_t)1;
        
        if (tmp < 0) 
        {
            StartingCycle = 0;
        }
        else
        {
            StartingCycle = NextAvailableCycle -IssueCycleGranularity+1;
        }
        
        for (uint64_t i = StartingCycle; i < NextAvailableCycle; i++) 
        {
            TmpTreeChunk = GetTreeChunk(i);
            
            if (FullOccupancyCyclesTree[TmpTreeChunk].get_node(i, ExecutionResource))
            {
                
                FoundInFullOccupancyCyclesTree = true;
                EnoughBandwidth    = false;
                break;
            }
        }
        
        // 3. The same as 2 but for next cycles. If there were loads executed on those cycles,
        // there would not be available bandwidth for the current load.
        for (uint64_t i = NextAvailableCycle+1; i < NextAvailableCycle +IssueCycleGranularity; i++) 
        {
            TmpTreeChunk = GetTreeChunk(i);
            
            if (FullOccupancyCyclesTree[TmpTreeChunk].get_node(i, ExecutionResource))
            {
                FoundInFullOccupancyCyclesTree = true;
                EnoughBandwidth = false;
                
                break;
            }
        }
    }
    else
    {
        EnoughBandwidth = true;
    }
    
    return EnoughBandwidth;
}



uint64_t
DynamicAnalysis::FindNextAvailableIssueCycleUntilNotInFullOrEnoughBandwidth(unsigned NextCycle, unsigned ExecutionResource , bool& FoundInFullOccupancyCyclesTree, bool& EnoughBandwidth)
{
    unsigned NextAvailableCycle = NextCycle;
    unsigned OriginalCycle;
    Tree<uint64_t> * Node = AvailableCyclesTree[ExecutionResource];
    Tree<uint64_t> * LastNodeVisited = NULL;
    
    NextAvailableCycle++;
    
    OriginalCycle = NextAvailableCycle;
    
    // If we loop over the first while because there is not enough bandwidth,
    // Node might be NULL because this loop has already been executed.
    Node = AvailableCyclesTree[ExecutionResource];
    
    while( Node ) 
    {
        if( Node->key > NextAvailableCycle)
        {
            if (NextAvailableCycle == OriginalCycle)
            { // i.e., it is the first iteration
                NextAvailableCycle = Node-> key;
                LastNodeVisited = Node;
                
            }
            // Search for a smaller one
            Node = Node->left;
        }
        else if( Node->key < NextAvailableCycle)
        {
            // We comment this out because this will never happen in NextAvailable because
            // for every full node we always insert the next available. The general
            // algorithm that finds the larger, if it exist, should have this code
            // uncommented.
            //UNCOMMENT THIS!!
            /*    if (NextAvailableCycle == OriginalCycle){
             NextAvailableCycle = Node->key;
             LastNodeVisited = Node;
             }*/
            if (Node->key == OriginalCycle) 
            {
                NextAvailableCycle = OriginalCycle;
                LastNodeVisited = Node;
                
                break;
            }
            else if (Node->key > OriginalCycle) 
            {
                //Search for a even smaller one
                NextAvailableCycle =Node-> key;
                LastNodeVisited = Node;
                
                // Search for a smaller one
                Node = Node-> left;
            }
            else
            { //Node->key < OriginalCycle
                // Search for a larger one, but do not store last node visited...
                Node = Node-> right;
            }
        }
        else
        { //Node->key = NextAvailableCycle
            NextAvailableCycle = Node->key;
            LastNodeVisited = Node;
            break;
        }
    }
    
    //LastNodeVisited contains the next available cycle. But we still need to check
    //that it is available for lower and upper levels.
    NextAvailableCycle = LastNodeVisited->key;
    
    FoundInFullOccupancyCyclesTree = true;
    EnoughBandwidth = false;
    
    return NextAvailableCycle;
    
}

// Find next available issue cycle depending on resource availability.
// Returns a pointer
unsigned
DynamicAnalysis::FindNextAvailableIssueCycle(unsigned OriginalCycle, unsigned ExecutionResource)
{
    uint64_t NextAvailableCycle = OriginalCycle;
    bool FoundInFullOccupancyCyclesTree = true;
    bool EnoughBandwidth = false;
    // Get the node, if any, corresponding to this issue cycle.
    unsigned TreeChunk = GetTreeChunk(NextAvailableCycle);
    
    // If full is null, then it is available for sure -> WRONG! It might happen that FULL is NULL because
    // a new chunk was created.
    // If it is not NULL and there is something scheduled in this cycle..
    // (we don't include the condition FullOccupancyNode->BitVector[ExecutionResource]==1
    // here because it could happen that it cannot be executed because of throughput<1
    // and something executed in earlier or later cycles.
    //if (FullOccupancyCyclesTree[TreeChunk] != NULL) {
    if (!FullOccupancyCyclesTree[TreeChunk].empty())
    {
        while (FoundInFullOccupancyCyclesTree == true && EnoughBandwidth ==false)
        {
            // Check if it is in full, but first make sure full is not NULL (it could happen it is NULL after
            // changing the NextAvailableCycle).
            
            if (FullOccupancyCyclesTree[TreeChunk].get_node(NextAvailableCycle, ExecutionResource))
            {
                    FoundInFullOccupancyCyclesTree = true;
            }
            else
            {
                    FoundInFullOccupancyCyclesTree = false;
            }
            
            
            // If it is not in full, it is available. But we have to make sure that
            // there is enough bandwidth (to avoid having large trees, we don't include
            // the latency cycles, so we have to make sure we don't issue in in latency cycles)
            if (ExecutionResource <= nExecutionUnits) 
            {
#ifdef DEBUG_GENERIC
                DEBUG(dbgs() << "ExecutionResource <= nExecutionUnits\n");
                DEBUG(dbgs() << "ExecutionResource "<< ExecutionResource<<"\n");
                DEBUG(dbgs() << "nExecutionUnits "<< nExecutionUnits<<"\n");
#endif
                // NEW CODE INSERTED
                EnoughBandwidth = ThereIsAvailableBandwidth(NextAvailableCycle, ExecutionResource, FoundInFullOccupancyCyclesTree, true);
                
                if (FoundInFullOccupancyCyclesTree == true || EnoughBandwidth == false) 
                {
                    // NEW CODE
                    NextAvailableCycle = FindNextAvailableIssueCycleUntilNotInFullOrEnoughBandwidth(NextAvailableCycle, ExecutionResource , FoundInFullOccupancyCyclesTree,EnoughBandwidth);
                    
                    // NextAvailableCycle has changed, possibly moving to a different chunk
                    TreeChunk = GetTreeChunk(NextAvailableCycle);
                }
            }
            else
            {
                if (FoundInFullOccupancyCyclesTree ==true) 
                {    
                    while (FoundInFullOccupancyCyclesTree) 
                    {
                        //Check if it is in full
                        if (FullOccupancyCyclesTree[TreeChunk].get_node(NextAvailableCycle, ExecutionResource))
                        {
#ifdef DEBUG_GENERIC
                            DEBUG(dbgs() << "Cycle " << NextAvailableCycle << " found in Full OccupancyCyclesTree\n");
#endif
                            // Try next cycle
                            NextAvailableCycle++;
                            TreeChunk = GetTreeChunk(NextAvailableCycle);
                            
                            
                            FoundInFullOccupancyCyclesTree = true;
                        }
                        else
                        {
#ifdef DEBUG_GENERIC
                            DEBUG(dbgs() << "Cycle " << NextAvailableCycle << " not found in Full OccupancyCyclesTree\n");
#endif
                            FoundInFullOccupancyCyclesTree = false;
                        }
                    }
                }
            }
            
        }
    }
    else
    {
        if (TreeChunk != 0) 
        {
            
            // Full is NULL, but check that TreeChunk is not zero. Otherwise, Full is not really NULL
            if (ExecutionResource <= nExecutionUnits) {
#ifdef DEBUG_GENERIC
                DEBUG(dbgs() << "ExecutionResource <= nExecutionUnits\n");
                DEBUG(dbgs() << "ExecutionResource "<< ExecutionResource<<"\n");
                DEBUG(dbgs() << "nExecutionUnits "<< nExecutionUnits<<"\n");
#endif
                // NEW CODE INSERTED
                FoundInFullOccupancyCyclesTree = false;
                EnoughBandwidth = ThereIsAvailableBandwidth(NextAvailableCycle, ExecutionResource, FoundInFullOccupancyCyclesTree, true);

#ifdef DEBUG_GENERIC
                dbgs() << "NAC: " << NextAvailableCycle << "\n";
                dbgs() << "EnoughBandwidth: " << EnoughBandwidth << "\n";
#endif
                
                if (FoundInFullOccupancyCyclesTree == true || EnoughBandwidth == false) 
                {
                    
                    // NEW CODE
                    NextAvailableCycle = FindNextAvailableIssueCycleUntilNotInFullOrEnoughBandwidth(NextAvailableCycle, ExecutionResource , FoundInFullOccupancyCyclesTree,EnoughBandwidth);
                }
            }
        }
        
    }
    return NextAvailableCycle;
}



// Find next available issue cycle depending on resource availability
bool
DynamicAnalysis::InsertNextAvailableIssueCycle(uint64_t NextAvailableCycle, unsigned ExecutionResource)
{    
    Tree<uint64_t> * Node = AvailableCyclesTree[ExecutionResource];
    unsigned NodeIssueOccupancy = 0;
    unsigned NodeWidthOccupancy = 0;
    unsigned NodeOccupancyPrefetch = 0;
    bool LevelGotFull = false;
    
    // Update Instruction count
    if (InstructionsCountExtended[ExecutionResource] == 0) 
    {
        FirstIssue[ExecutionResource] = true;
    }
    
    InstructionsCountExtended[ExecutionResource]++;
    
    unsigned AccessWidth = AccessWidths[ExecutionResource];
    
    
    if (FirstIssue[ExecutionResource] == true) 
    {
        FirstNonEmptyLevel[ExecutionResource] = NextAvailableCycle;
        FirstIssue[ExecutionResource] = false;
    }
    else
    {
        FirstNonEmptyLevel[ExecutionResource] = min(FirstNonEmptyLevel[ExecutionResource], NextAvailableCycle);
    }
    
    InstructionsLastIssueCycle[ExecutionResource] = max(InstructionsLastIssueCycle[ExecutionResource], NextAvailableCycle);
#ifdef DEBUG_GENERIC
    dbgs() << "NAC: " << NextAvailableCycle << "\n";    
    
    
    DEBUG(dbgs() << "Updating InstructionsLastIssueCycle of execution resource " << ResourcesNames[ExecutionResource] << " to " <<InstructionsLastIssueCycle[ExecutionResource]    << "\n");
#endif
    // Insert
    // If it exists already in Available... Inserting it has any effect? No, it simply returns a pointer to the node.
    // Here, use ExtendedInstructionType and not InstructioTypeStats because both prefetched loads and normal loads
    // share the same tree
    // TODO: IF we know in advanced that the available level gets full directly, we can avoid inserting it and removing it
    // from AvailableCyclesTree.
    

    AvailableCyclesTree[ExecutionResource] = insert_node(NextAvailableCycle, AvailableCyclesTree[ExecutionResource]);
    
    Node = AvailableCyclesTree[ExecutionResource];
    
    //TODO: clean this up
    if (ExecutionUnitsThroughput[ExecutionResource] < 1) 
    {
        Node->issueOccupancy++;
        Node->widthOccupancy += AccessWidth;
    }
    else
    {
        if (AccessWidth <= ExecutionUnitsThroughput[ExecutionResource]) 
        {
            Node->issueOccupancy++;
            Node->widthOccupancy += AccessWidth;
        }
        else
        {
            //    if (AccessWidth > ExecutionUnitsThroughput[ExecutionResource])
            // e.g., if I access 64B and bandwidth is 8
            //TODO: Fill in, and take into account that AccessWidth might be > than throughput
            // when finding next available issue cycle.
            Node->issueOccupancy++;
            Node->widthOccupancy += AccessWidth;
        }
    }
    
    /* Copy these values because later on the Node is not the same any more */
    NodeIssueOccupancy = Node->issueOccupancy;
    NodeWidthOccupancy = Node->widthOccupancy;
    NodeOccupancyPrefetch = Node->occupancyPrefetch;
    MaxOccupancy[ExecutionResource] = max(MaxOccupancy[ExecutionResource], NodeIssueOccupancy + NodeOccupancyPrefetch);
    
    // If ExecutionUnitsThroughput is INF, the level never gets full
    if (ExecutionUnitsThroughput[ExecutionResource] == INF) 
    {
        LevelGotFull = false;
    }
    else
    {
        if (ExecutionUnitsParallelIssue[ExecutionResource] != INF) 
        {
            if (NodeWidthOccupancy == (unsigned)ExecutionUnitsParallelIssue[ExecutionResource]*ExecutionUnitsThroughput[ExecutionResource]) 
            {
                LevelGotFull = true;
            }
            if (NodeIssueOccupancy == (unsigned)ExecutionUnitsParallelIssue[ExecutionResource] ) 
            {
                LevelGotFull = true;
            }
        }
        else
        { // If ParallelIssue is INF, but ExecutionUnitsThroughput is not INF

            // Say baseThru = 0.4 and numFU desired is 2. We make relaxThru = 0.8
            //      As NWO increments with each issue, original code accommodates just one instruction
            // Solution: Scale NodeWidthOccupancy by baseThru, making the notion of numFU clearer
            //  Ex: 1 issue => 1*0.4, 2 issues => 2*0.4 = 0.8 | Now 0.8 <= 0.8, hence 2 issues allowed
            //  Ex: 1 issue => 1*3  , 2 issues => 2*3   = 6   | Now 6   <= 6,   hence 2 issues allowed 
            //  Ex: 1 issue => 64*32, 2 issues => 128*32      | Now 64 !<= 128*32, 
            //          so exclude memories from this scaling.
            if (ExecutionResource < nCompExecutionUnits)
            {
                if (ExecutionUnitsThroughput[ExecutionResource] <= float(NodeWidthOccupancy * ExecutionUnitsBaselineThroughput[ExecutionResource]))
                {
//                    dbgs() << ResourcesNames[ExecutionResource] << "\t1\tNWO: " << NodeWidthOccupancy << "\tbase: " <<
//                        ExecutionUnitsBaselineThroughput[ExecutionResource] << "\tinput: " << 
//                        ExecutionUnitsThroughput[ExecutionResource] << "\n";
                    LevelGotFull = true;
                }
            }
            else 
            { 
                if (ExecutionUnitsThroughput[ExecutionResource] <= float(NodeWidthOccupancy*1.0) )
                {
//                    dbgs() << ResourcesNames[ExecutionResource] << "\t2\tNWO: " << NodeWidthOccupancy << "\tbase: " <<
//                        ExecutionUnitsBaselineThroughput[ExecutionResource] << "\tinput: " << 
//                        ExecutionUnitsThroughput[ExecutionResource] << "\n";
                    LevelGotFull = true;
                }
            }
        }
    }
    
    if (LevelGotFull) 
    {
        LevelGotFull = true;
        
        // Check whether next cycle is in full. because if it is, it should not be inserted into AvailableCyclesTree
        // Next cycle is not NexAvailableCycle+1, is NextAvailableCycle + 1/Throughput
        // Here is where the distinction between execution resource and instruction type is important.
        unsigned NextCycle = IssueCycleGranularities[ExecutionResource];

        AvailableCyclesTree[ExecutionResource] = delete_node(NextAvailableCycle, AvailableCyclesTree[ExecutionResource]);
        
        // Insert node in FullOccupancy
        unsigned TreeChunk = GetTreeChunk(NextAvailableCycle);
        
        FullOccupancyCyclesTree[TreeChunk].insert_node(NextAvailableCycle, ExecutionResource);
        
        TreeChunk = GetTreeChunk(NextAvailableCycle+NextCycle);
        
        if (!FullOccupancyCyclesTree[TreeChunk].get_node((NextAvailableCycle+NextCycle), ExecutionResource))
        {
            AvailableCyclesTree[ExecutionResource] = insert_node(NextAvailableCycle+NextCycle,    AvailableCyclesTree[ExecutionResource]);
            
            // In this case, although we are inserting a node into AvailableCycles, we don't insert the source
            // code line associated to the cycle because it does not mean that an instruction has actually been
            // scheduled in NextAvailableCycle+NextCycle. In this case it just means that this is the next
            // available cycle. Actually, IssueOccupacy of this new level should be zero.
            
            //Update LastIssueCycle -> Distinguish prefetch loads/stores!!
            InstructionsLastIssueCycle[ExecutionResource] = max( InstructionsLastIssueCycle[ExecutionResource] ,NextAvailableCycle+NextCycle);

        }
    }
    return LevelGotFull;
}



//===----------------------------------------------------------------------===//
//                                Routines for Analysis of Data Reuse
// From the paper "Program Locality Analysis Using Reuse Distance", by Y. Zhong,
// X. Sheng and C. DIng, 2009
//===----------------------------------------------------------------------===//

int
DynamicAnalysis::ReuseDistance(uint64_t Last, uint64_t Current, uint64_t address)
{
    int Distance = -1;
    
    if (L1CacheSize != 0)
    { // Otherwise, does not matter the distance, it is mem access
        int ReuseTreeDistance = ReuseTreeSearchDelete(Last, address);
        
        Distance = ReuseTreeDistance;
        
        if(Distance >= 0)
            Distance = roundNextPowerOfTwo(Distance);
        
        // Get a pointer to the resulting tree
        ReuseTree = insert_node(Current, ReuseTree, address);
    }
    else
    {
        ReuseTree = insert_node(address,ReuseTree, address);
    }
    
    return Distance;
}

// Return the distance of the closest node with key <= Current, i.e., all the
// nodes that have been prefetched between Last and Current.
int
DynamicAnalysis::ReuseTreeSearchDelete(uint64_t Original, uint64_t address)
{    
    Tree<uint64_t> * Node = NULL;
    int Distance = 0;
    
    Node = ReuseTree;
    
    //Once we find it, calculate the distance without deleting the node.
    
    if (Original == 0 || Node == NULL) 
    { // Did not find any node smaller
        Distance = -1;
    }
    else
    {
        while (true) 
        {
            // This is the mechanism used in the original algorithm to delete the host
            // node, decrementing the last_record attribute of the host node, and
            // the size attribute of all parents nodes.
            // Node->size = Node->size-1;
            if (Original < Node->key) 
            {
                if (Node->right != NULL)
                    Distance = Distance + Node->right->size;
                if (Node->left == NULL)
                    break;
                
                Distance = Distance + 1/*Node->last_record*/;
                Node = Node->left;
            }
            else
            {
                if (Original > Node-> key) 
                {
                    if (Node->right == NULL)
                        break;
                    Node = Node->right;
                }
                else
                { // Last = Node->key, i.e., Node is the host node
                    if (Node->right != NULL)
                        Distance = Distance + Node->right->size;
                    
                    //increase by one so that we can calculate directly the hit rate
                    // for a cache size multiple of powers of two.
                    
                    Distance = Distance+1;
                    
                    if (Node->address == address)
                    {
                        ReuseTree = delete_node(Original, ReuseTree);
                    }
                    break;
                }
            }
        }
    }
    
    return Distance;
}

void
DynamicAnalysis::updateReuseDistanceDistribution(int Distance, uint64_t InstructionIssueCycle)
{    
    map <int,int>::iterator ReuseDistanceMapIt;
    
    ReuseDistanceMapIt = ReuseDistanceDistribution.find(Distance);
    if (ReuseDistanceMapIt != ReuseDistanceDistribution.end())
    {
        ReuseDistanceMapIt->second = ReuseDistanceMapIt->second+1 ;
    }
    else
    {
        ReuseDistanceDistribution[Distance] = 1; // First usage
    }
}

unsigned int
DynamicAnalysis::DivisionRoundUp(float a, float b)
{
    return (a * b + (a+b) / 2) / (a+b);
}

// compute the next highest power of 2 of 32-bit v.
// Routine from Bit Twiddling Hacks, University of Standford.
unsigned int
DynamicAnalysis::roundNextPowerOfTwo(unsigned int v)
{
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    
    return v;
}

unsigned int
DynamicAnalysis::roundNextMultiple(uint64_t num, int factor)
{
    // This works because factor is always going to be a power of 2
    return (num + factor - 1) & ~(factor - 1);
}

unsigned
DynamicAnalysis::GetPositionSourceCodeLineInfoVector(uint64_t Resource)
{
    switch (Resource) 
    {
        case FP_ADDER:
            return 0;
            break;
        case FP_MULTIPLIER:
            return 2;
            break;
        case FP_DIVIDER:
            return 4;
            break;
        case FP_SHUFFLE:
            return 6;
            break;
            
        case L1_LOAD_CHANNEL:
            return 8;
            break;
        case L1_STORE_CHANNEL:
            return 10;
            break;
        case L2_LOAD_CHANNEL:
            return 12;
            break;
            
        case L3_LOAD_CHANNEL:
            return 14;
            break;
            
        case MEM_LOAD_CHANNEL:
            return 16;
            break;
            
        case RS_STALL:
            return 17;
            break;
        case ROB_STALL:
            return 18;
            break;
        case LB_STALL:
            return 19;
            break;
        case SB_STALL:
            return 20;
            break;
        case LFB_STALL:
            return 21;
            break;
        default:
            dbgs() << "Resource: " << ResourcesNames[Resource] << "\n";
            report_fatal_error("Unknown resource while retrieving source code line information.");
            break;
    }
}

void
printOpName(unsigned opcode)
{ 
    switch (opcode) 
    {
            // Terminators
        case Instruction::Ret: dbgs() << "Instruction::Ret\n"; break;
        case Instruction::Br: dbgs() << "Instruction::Br\n"; break;
        case Instruction::Switch: dbgs() << "Instruction::Switch\n"; break;
        case Instruction::IndirectBr: dbgs() << "Instruction::IndirectBr\n"; break;
        case Instruction::Invoke: dbgs() << "Instruction::Invoke\n"; break;
        case Instruction::Resume: dbgs() << "Instruction::Resume\n"; break;
        case Instruction::Unreachable: dbgs() << "Instruction::Unreachable\n"; break;
            
            // Standard binary operators...
        case Instruction::Add: dbgs() << "Instruction::Add\n"; break;
        case Instruction::FAdd: dbgs() << "Instruction::FAdd\n"; break;
        case Instruction::Sub: dbgs() << "Instruction::Sub\n"; break;
        case Instruction::FSub: dbgs() << "Instruction::FSub\n"; break;
        case Instruction::Mul: dbgs() << "Instruction::Mul\n"; break;
        case Instruction::FMul: dbgs() << "Instruction::FMul\n"; break;
        case Instruction::UDiv: dbgs() << "Instruction::UDiv\n"; break;
        case Instruction::SDiv: dbgs() << "Instruction::SDiv\n"; break;
        case Instruction::FDiv: dbgs() << "Instruction::FDiv\n"; break;
        case Instruction::URem: dbgs() << "Instruction::URem\n"; break;
        case Instruction::SRem: dbgs() << "Instruction::SRem\n"; break;
        case Instruction::FRem: dbgs() << "Instruction::FRem\n"; break;
            
            // Logical operators...
        case Instruction::And: dbgs() << "Instruction::And\n"; break;
        case Instruction::Or: dbgs() << "Instruction::Or\n"; break;
        case Instruction::Xor: dbgs() << "Instruction::Xor\n"; break;
            
            // Memory dbgs() << "Memor\n";
        case Instruction::Alloca: dbgs() << "Instruction::Alloca\n"; break;
        case Instruction::Load: dbgs() << "Instruction::Load\n"; break;
        case Instruction::Store: dbgs() << "Instruction::Store\n"; break;
        case Instruction::AtomicCmpXchg: dbgs() << "Instruction::AtomicCmpXchg\n"; break;
        case Instruction::AtomicRMW: dbgs() << "Instruction::AtomicRMW\n"; break;
        case Instruction::Fence: dbgs() << "Instruction::Fence\n"; break;
        case Instruction::GetElementPtr: dbgs() << "Instruction::GetElementPtr\n"; break;
            // Convert instructions...
        case Instruction::Trunc: dbgs() << "Instruction::Trunc\n"; break;
        case Instruction::ZExt: dbgs() << "Instruction::ZExt\n"; break;
        case Instruction::SExt: dbgs() << "Instruction::SExt\n"; break;
        case Instruction::FPTrunc: dbgs() << "Instruction::FPTrunc\n"; break;
        case Instruction::FPExt: dbgs() << "Instruction::FPExt\n"; break;
        case Instruction::FPToUI: dbgs() << "Instruction::FPToUI\n"; break;
        case Instruction::FPToSI: dbgs() << "Instruction::FPToSI\n"; break;
        case Instruction::UIToFP: dbgs() << "Instruction::UIToFP\n"; break;
        case Instruction::SIToFP: dbgs() << "Instruction::SIToFP\n"; break;
        case Instruction::IntToPtr: dbgs() << "Instruction::IntToPtr\n"; break;
        case Instruction::PtrToInt: dbgs() << "Instruction::PtrToInt\n"; break;
        case Instruction::BitCast: dbgs() << "Instruction::BitCast\n"; break;
            
            // Other instructions...
        case Instruction::ICmp: dbgs() << "Instruction::ICmp\n"; break;
        case Instruction::FCmp: dbgs() << "Instruction::FCmp\n"; break;
        case Instruction::PHI: dbgs() << "Instruction::PHI\n"; break;
        case Instruction::Select: dbgs() << "Instruction::Select\n"; break;
        case Instruction::Call: dbgs() << "Instruction::Call\n"; break;
        case Instruction::Shl: dbgs() << "Instruction::Shl\n"; break;
        case Instruction::LShr: dbgs() << "Instruction::LShr\n"; break;
        case Instruction::AShr: dbgs() << "Instruction::AShr\n"; break;
        case Instruction::VAArg: dbgs() << "Instruction::VAArg\n"; break;
        case Instruction::ExtractElement: dbgs() << "Instruction::ExtractElement\n"; break;
        case Instruction::InsertElement: dbgs() << "Instruction::InsertElement\n"; break;
        case Instruction::ShuffleVector: dbgs() << "Instruction::ShuffleVector\n"; break;
        case Instruction::ExtractValue: dbgs() << "Instruction::ExtractValue\n"; break;
        case Instruction::InsertValue: dbgs() << "Instruction::InsertValue\n"; break;
        case Instruction::LandingPad: dbgs() << "Instruction::LandingPad\n"; break;
    }
}

unsigned
DynamicAnalysis::GetExtendedInstructionType(int OpCode, int ReuseDistance)
{
    if (PerTaskCommFactor)
    {
        CommFactorRead = TaskCFR;
        CommFactorWrite = TaskCFW;
    }

    unsigned InstructionType = 0;
    
    switch (OpCode) 
    {
        case Instruction::And:
            return INT_ADD_NODE;

        case Instruction::Or:
            return INT_ADD_NODE;

        case Instruction::Xor:
            return INT_ADD_NODE;

        case Instruction::Shl:
        case Instruction::LShr:
        case Instruction::AShr:                     
            return INT_ADD_NODE;
            
        case Instruction::Add:
            return INT_ADD_NODE;
            
        case Instruction::Sub:
            return INT_ADD_NODE;
            
        case Instruction::ICmp:
            return INT_ADD_NODE;
            
        case Instruction::Mul:
            return INT_MUL_NODE;

        case Instruction::UDiv:
        case Instruction::SDiv:
        case Instruction::SRem:
        case Instruction::URem:
            return INT_DIV_NODE;

        case Instruction::FAdd:
        case Instruction::FCmp:
            return FP_ADD_NODE;
            
        case Instruction::FSub:
            return FP_ADD_NODE;
            
        case Instruction::FMul:
            return FP_MUL_NODE;
            
        case Instruction::FDiv:
        case Instruction::FRem:
            return FP_DIV_NODE;
            
        case Instruction::ShuffleVector:
            return VECTOR_SHUFFLE_NODE;
            
        case Instruction::Load: 
        {
            if (ReuseDistance < 0 && L1CacheSize == 0)
                return L1_LOAD_NODE;
            
            // Roll dice; infer larger latency if less than CommFactor
            // CommFactor = 1 => All communicate => All infer larger latency
            //float randN = rand() * 1.0 / RAND_MAX;
            float randN = rd() * 1.0 / (rd.max() - rd.min());

            if (ReuseDistance < 0 || (ReuseDistance > (int)LLCCacheSize && LLCCacheSize != 0))
            {
                if (randN <= CommFactorRead) { return L3_LOAD_NODE; } // Some other core would have brought this in
                return MEM_LOAD_NODE;
            }
            if (ReuseDistance <= (int)L1CacheSize) {
                if (randN <= CommFactorRead) { return L3_LOAD_NODE; } // Coherence miss
                return L1_LOAD_NODE;
            }
            if ((int)L1CacheSize < ReuseDistance && (ReuseDistance <= (int)L2CacheSize && L2CacheSize != 0)) {
                if (randN <= CommFactorRead) { return L3_LOAD_NODE; } // Coherence miss
                return L2_LOAD_NODE;
            }
            if ((int)L2CacheSize < ReuseDistance && (ReuseDistance <= (int)LLCCacheSize && LLCCacheSize != 0))
                return L3_LOAD_NODE;
            report_fatal_error("Instruction type not associated with a node");
            break;
        }    
        case Instruction::Store:
        {    
            if (ReuseDistance < 0 && L1CacheSize == 0)
                return L1_STORE_NODE;
            
            //float randN = rand() * 1.0 / RAND_MAX;
            float randN = rd() * 1.0 / (rd.max() - rd.min());

            if (ReuseDistance < 0 || (ReuseDistance > (int)LLCCacheSize && LLCCacheSize != 0))
            {
                if (randN <= CommFactorWrite) { return L3_STORE_NODE; } // Some other core would have brought this in
                return MEM_STORE_NODE;
            }
            if (ReuseDistance <= (int)L1CacheSize)
            {
                if (randN <= CommFactorWrite) { return L3_STORE_NODE; } // Coherence miss 
                return L1_STORE_NODE;
            }
            if ((int)L1CacheSize < ReuseDistance && (ReuseDistance <= (int)L2CacheSize && L2CacheSize != 0 ))
            {
                if (randN <= CommFactorWrite) { return L3_STORE_NODE; } // Coherence miss 
                return L2_STORE_NODE;
            }
            if ((int)L2CacheSize < ReuseDistance && (ReuseDistance <= (int)LLCCacheSize && LLCCacheSize != 0))
                return L3_STORE_NODE;
            report_fatal_error("Instruction type not associated with a node");
            break;
        }    
        default:
            errs() << OpCode << "\n";
            report_fatal_error("Instruction type not associated with a node");
    }
    
    return InstructionType;
}



uint64_t
DynamicAnalysis::GetMinIssueCycleReservationStation()
{
    vector<uint64_t>::iterator it;
    
    uint64_t MinIssueCycle = ReservationStationIssueCycles.front();
    for (it = ReservationStationIssueCycles.begin(); it != ReservationStationIssueCycles.end(); ++it)
        MinIssueCycle = min(MinIssueCycle, *it);
    
    return MinIssueCycle;
}



uint64_t
DynamicAnalysis::GetMinCompletionCycleLoadBuffer()
{    
    vector<uint64_t>::iterator it;
    
    uint64_t MinCompletionCycle = LoadBufferCompletionCycles.front();
    for (it = LoadBufferCompletionCycles.begin(); it != LoadBufferCompletionCycles.end(); ++it) 
    {
        MinCompletionCycle = min(MinCompletionCycle, *it);
    }
    
    return MinCompletionCycle;
}

uint64_t
DynamicAnalysis::GetMinCompletionCycleLoadBufferTree()
{
    return MinLoadBuffer;
}

uint64_t
DynamicAnalysis::GetMinCompletionCycleStoreBuffer()
{
    vector<uint64_t>::iterator it;
    
    uint64_t MinCompletionCycle = StoreBufferCompletionCycles.front();
    for (it = StoreBufferCompletionCycles.begin(); it != StoreBufferCompletionCycles.end(); ++it) 
    {
        MinCompletionCycle = min(MinCompletionCycle, *it);
    }
    
    return MinCompletionCycle;
}

uint64_t
DynamicAnalysis::GetMinCompletionCycleLineFillBuffer(){
    
    vector<uint64_t>::iterator it;
    
    uint64_t MinCompletionCycle = LineFillBufferCompletionCycles.front();
    for (it = LineFillBufferCompletionCycles.begin(); it != LineFillBufferCompletionCycles.end(); ++it) {
        MinCompletionCycle = min(MinCompletionCycle, *it);
    }
    return MinCompletionCycle;
}

// This is not yet working...
uint64_t
DynamicAnalysis::FindNextNonEmptyLevel(unsigned ExecutionResource, uint64_t Level)
{    
    bool IsInAvailableCyclesTree = true;
    bool IsInFullOccupancyCyclesTree = true;
    uint64_t Original = 0;
    uint64_t Closest = 0;
    uint64_t NextNonEmptyLevelAvailableCyclesTree = Level;
    uint64_t NextNonEmptyLevelFullOccupancyCyclesTree = Level;
    
    if (ExecutionResource <= nExecutionUnits) 
    {
        Tree<uint64_t> * Node = NULL;
        Tree<uint64_t> * LastNodeVisited = NULL;
        
        Node = AvailableCyclesTree[ExecutionResource];
        Original = Level+1;
        Closest = Original;
        
        while (IsInAvailableCyclesTree == true) 
        {
            while( true ) 
            {
                if( Node->key > Closest)
                {
                    if (Closest == Original)
                    { // i.e., it is the first iteration
                        Closest = Node-> key;
                        LastNodeVisited = Node;
                    }
                    // Search for a smaller one
                    //Node = Node-> left;
                    if (Node->left == NULL) 
                    {
                        //If, moreover, Node->right ==NULL, then break
                        if (Node->right == NULL) 
                        {
                            Closest = Node->key;
                            LastNodeVisited = Node;
                            
                            IsInAvailableCyclesTree = false;
                            break;
                        }
                        
                        // The while loop is going to finish.
                        // Again, we have to make sure
                        if( !(Node->issueOccupancy != 0 ||    Node->occupancyPrefetch != 0))
                        {
                            // We don't want the loop to finish.
                            // Splay on key and search for the next one.
                            AvailableCyclesTree[ExecutionResource]= splay(Node->key,AvailableCyclesTree[ExecutionResource]);
                            Node = AvailableCyclesTree[ExecutionResource];
                            //Keep searching starting from the next one
                            Original = Node->key+1;
                            Closest = Original;
                        }
                    }
                    else
                    {
                        Node = Node-> left;
                    }
                }
                else if( Node->key < Closest)
                {
                    if (Node->key == Original) 
                    {
                        // MODIFICATION with respect to the normal search in the tree.
                        if( Node->issueOccupancy != 0 || Node->occupancyPrefetch != 0)
                        {
                            dbgs() << "Is in available cycles for this resource and has nodes scheduled\n";
                            Closest = Node->key;
                            LastNodeVisited = Node;
                            IsInAvailableCyclesTree = false;
                            break;
                            
                        }
                        else
                        {
                            dbgs() << "Is in available, but not for this resource, so keep searching\n";
                            dbgs() << "Splay tree to "<< Node->key<<"\n";
                            AvailableCyclesTree[ExecutionResource]= splay(Node->key,AvailableCyclesTree[ExecutionResource]);
                            Node =    AvailableCyclesTree[ExecutionResource];
                            dbgs() << "Root of the tree "<< Node->key<<"\n";
                            //Keep searching starting from the next one
                            Original = Node->key+1;
                            Closest = Original;
                            
                        }
                    }
                    else if (Node->key > Original) 
                    {
                        dbgs() << "Node->key > Original\n";
                        Closest =Node-> key;
                        LastNodeVisited = Node;
                        //Search for a even smaller one
                        
                        // Node = Node-> left;
                        if (Node->left==NULL) 
                        {
                            dbgs() << "Node->left is NULL\n";
                            //If, moreover, Node->right ==NULL, then break
                            if (Node->right==NULL) 
                            {
                                dbgs() << "Node->right is NULL\n";
                                Closest = Node->key;
                                LastNodeVisited = Node;
                                
                                IsInAvailableCyclesTree = false;
                                break;
                            }
                            
                            // The while loop is going to finish.
                            // Again, we have to make sure
                            if( !(Node->issueOccupancy != 0 ||    Node->occupancyPrefetch != 0))
                            {
                                // We don't want the loop to finish.
                                // Splay on key and search for the next one.
                                AvailableCyclesTree[ExecutionResource]= splay(Node->key,AvailableCyclesTree[ExecutionResource]);
                                Node =    AvailableCyclesTree[ExecutionResource];
                                //Keep searching starting from the next one
                                Original = Node->key+1;
                                Closest = Original;
                            }
                        }
                        else
                        {
                            Node = Node-> left;
                        }
                        
                    }
                    else
                    { //Node->key < Original
                        dbgs() << "Node->key < Original\n";
                        
                        // Search for a larger one
                        
                        Node = Node->right;
                        if (Node->right==NULL) {
                            IsInAvailableCyclesTree = false;
                        }
                        
                    }
                }
                else
                { //Node->key = Closest
                    dbgs() << "Node->key = Closest\n";
                    // MODIFICATION with respect to the normal search in the tree.
                    if( Node->issueOccupancy != 0 ||    Node->occupancyPrefetch != 0)
                    {
                        dbgs() << "Is in full for this resource\n";
                        Closest = Node->key;
                        LastNodeVisited = Node;
                        IsInAvailableCyclesTree = false;
                        
                        break;
                        
                    }
                    else
                    {
                        dbgs() << "Is in full, but not for this resource, so keep searching\n";
                        AvailableCyclesTree[ExecutionResource] = splay(Node->key,AvailableCyclesTree[ExecutionResource]);
                        Node =    AvailableCyclesTree[ExecutionResource];
                        dbgs() << "Root of the tree "<< Node->key<<"\n";
                        
                        Original = Node->key+1;
                        Closest = Original;
                        
                    }
                }
            }
            
            dbgs() << "NodeFull->key " << LastNodeVisited->key<<"\n";
            // Current = LastNodeVisited->key;
        }
        
        if (IsInAvailableCyclesTree == false) 
        { // i.e., there is no non-empty level
            NextNonEmptyLevelAvailableCyclesTree = Level;
        }
        
    }
    
    Original = Level+1;
    Closest = Original;
    // Scan for next cycle where ExecutionResource == 1
    
    if (IsInFullOccupancyCyclesTree == false) 
    { // i.e., there are no non-empty level
        //NextNonEmptyLevelFullOccupancyCyclesTree = LastNodeVisited->key;
        // Scan for bit set in FullOccupancyCyclesTree
        //     N.B. This may span different tree chunks
        NextNonEmptyLevelFullOccupancyCyclesTree = BitScan(FullOccupancyCyclesTree, Level + 1, ExecutionResource);
    }
    
    if (NextNonEmptyLevelAvailableCyclesTree == Level) 
    {
        return NextNonEmptyLevelFullOccupancyCyclesTree;
    }
    else
    {
        if (NextNonEmptyLevelFullOccupancyCyclesTree == Level) 
        {
            return NextNonEmptyLevelAvailableCyclesTree;
        }
        else
        {
            //None of them are equal to Level
            return min(NextNonEmptyLevelAvailableCyclesTree, NextNonEmptyLevelFullOccupancyCyclesTree);
        }
    }
}


uint64_t
DynamicAnalysis::GetLastIssueCycle(unsigned ExecutionResource)
{    
    Tree<uint64_t> * NodeAvailable = NULL;
    unsigned IssueCycleGranularity = IssueCycleGranularities[ExecutionResource];
    uint64_t LastCycle = InstructionsLastIssueCycle[ExecutionResource];
    
    if(ExecutionResource <= nExecutionUnits)
    {    
        AvailableCyclesTree[ExecutionResource] = splay(LastCycle,AvailableCyclesTree[ExecutionResource]);
        NodeAvailable =AvailableCyclesTree[ExecutionResource];
        
        if ( NodeAvailable != NULL && NodeAvailable->key== LastCycle && NodeAvailable->issueOccupancy == 0 ) 
        {
            LastCycle = LastCycle-IssueCycleGranularity;
        }
    }
    return LastCycle;
}


void
DynamicAnalysis::RemoveFromReservationStation(uint64_t Cycle)
{    
    LessThanOrEqualValuePred Predicate = {Cycle};
    ReservationStationIssueCycles.erase(std::remove_if(ReservationStationIssueCycles.begin(), ReservationStationIssueCycles.end(), Predicate), ReservationStationIssueCycles.end());
}

void
DynamicAnalysis::RemoveFromReorderBuffer(uint64_t Cycle)
{    
    while (!ReorderBufferCompletionCycles.empty() && ReorderBufferCompletionCycles.front() <= Cycle)
        ReorderBufferCompletionCycles.pop_front();
}

void
DynamicAnalysis::RemoveFromLoadBuffer(uint64_t Cycle)
{    
    LessThanOrEqualValuePred Predicate = {Cycle};
    LoadBufferCompletionCycles.erase(std::remove_if(LoadBufferCompletionCycles.begin(), LoadBufferCompletionCycles.end(), Predicate), LoadBufferCompletionCycles.end());
}

void
DynamicAnalysis::RemoveFromLoadBufferTree(uint64_t Cycle)
{    
    bool CycleFound = true;
    while (CycleFound == true) 
    {
        if (LoadBufferCompletionCyclesTree != NULL) 
        {
            LoadBufferCompletionCyclesTree = splay(Cycle, LoadBufferCompletionCyclesTree);
            
            if (LoadBufferCompletionCyclesTree->key <= Cycle) 
            { // If Cycle found
                // TODO: assert(LoadBufferCompletionCyclesTree->left->key <= Cycle);
                delete_all(LoadBufferCompletionCyclesTree->left);
                LoadBufferCompletionCyclesTree->left = NULL;
                
                LoadBufferCompletionCyclesTree = delete_node(LoadBufferCompletionCyclesTree->key,LoadBufferCompletionCyclesTree);
                // If we remove the minimum, the resulting tree has as node the
                // successor of the minimum, which is the next minimum -> This is not
                // true after we have splayed and the minimum is in the root.
                if (Cycle >= MinLoadBuffer && LoadBufferCompletionCyclesTree != NULL) 
                {
                    MinLoadBuffer = min(LoadBufferCompletionCyclesTree);
                    // MinLoadBuffer = LoadBufferCompletionCyclesTree->key;
                }
            }
            else
            {
                CycleFound = false;
            }
        }
        else
        {
            CycleFound = false;
        }
    }
}

void
DynamicAnalysis::RemoveFromStoreBuffer(uint64_t Cycle)
{   
    LessThanOrEqualValuePred Predicate = {Cycle};
    StoreBufferCompletionCycles.erase(std::remove_if(StoreBufferCompletionCycles.begin(), StoreBufferCompletionCycles.end(), Predicate), StoreBufferCompletionCycles.end());
}



void
DynamicAnalysis::RemoveFromLineFillBuffer(uint64_t Cycle)
{    
    LessThanOrEqualValuePred Predicate = {Cycle};
    LineFillBufferCompletionCycles.erase(std::remove_if(LineFillBufferCompletionCycles.begin(), LineFillBufferCompletionCycles.end(), Predicate), LineFillBufferCompletionCycles.end());
}



void
DynamicAnalysis::RemoveFromDispatchToLoadBufferQueue(uint64_t Cycle)
{    
    StructMemberLessThanOrEqualThanValuePred Predicate = {Cycle};
    DispatchToLoadBufferQueue.erase(std::remove_if(DispatchToLoadBufferQueue.begin(), DispatchToLoadBufferQueue.end(), Predicate), DispatchToLoadBufferQueue.end());
}



void
DynamicAnalysis::RemoveFromDispatchToStoreBufferQueue(uint64_t Cycle)
{
    StructMemberLessThanOrEqualThanValuePred Predicate = {Cycle};
    DispatchToStoreBufferQueue.erase(std::remove_if(DispatchToStoreBufferQueue.begin(), DispatchToStoreBufferQueue.end(), Predicate), DispatchToStoreBufferQueue.end());
}



void
DynamicAnalysis::RemoveFromDispatchToLineFillBufferQueue(uint64_t Cycle)
{    
    StructMemberLessThanOrEqualThanValuePred Predicate = {Cycle};
    DispatchToLineFillBufferQueue.erase(std::remove_if(DispatchToLineFillBufferQueue.begin(), DispatchToLineFillBufferQueue.end(), Predicate), DispatchToLineFillBufferQueue.end());
}



void
DynamicAnalysis::DispatchToLoadBuffer(uint64_t Cycle)
{
    vector<InstructionDispatchInfo>::iterator it = DispatchToLoadBufferQueue.begin();
    for(; it != DispatchToLoadBufferQueue.end();) 
    {
        if ((*it).IssueCycle == InstructionFetchCycle) 
        {
            //Erase returns the next valid iterator => insert in LoadBuffer before it is removed
            LoadBufferCompletionCycles.push_back((*it).CompletionCycle);
            it = DispatchToLoadBufferQueue.erase(it);
        }
        else
            ++it;
    }
}


void DynamicAnalysis::inOrder(uint64_t i, ComplexTree<uint64_t> * n) 
{
    if (n == NULL) return;
    
    inOrder(i,n->left);
    if (n->IssueCycle <= i && n->IssueCycle != 0) 
    {
        if (node_size(LoadBufferCompletionCyclesTree) == 0) 
        {
            MinLoadBuffer = n->key;
        }
        else
        {
            MinLoadBuffer = min(MinLoadBuffer,n->key);
        }
        
        
        DEBUG(dbgs() << "Inserting into LB node with issue cycle " << n->IssueCycle << " and key " << n->key << "\n");
        LoadBufferCompletionCyclesTree= insert_node(n->key , LoadBufferCompletionCyclesTree);
        PointersToRemove.push_back(n);
    }
    
    inOrder(i,n->right);
}


void
DynamicAnalysis::DispatchToLoadBufferTree(uint64_t Cycle)
{
    inOrder(Cycle, DispatchToLoadBufferQueueTree);
    
    for(size_t i = 0; i< PointersToRemove.size(); ++i)
    {    
        DispatchToLoadBufferQueueTree = delete_node(PointersToRemove.at(i)->key, DispatchToLoadBufferQueueTree);   
    }
    
    PointersToRemove.clear();
}


void
DynamicAnalysis::DispatchToStoreBuffer(uint64_t Cycle)
{
    vector<InstructionDispatchInfo>::iterator it = DispatchToStoreBufferQueue.begin();
    for(; it != DispatchToStoreBufferQueue.end(); ) 
    {
        if ((*it).IssueCycle == InstructionFetchCycle) 
        {
            StoreBufferCompletionCycles.push_back((*it).CompletionCycle);
            it = DispatchToStoreBufferQueue.erase(it);
        }
        else
            ++it;
    }
}



void
DynamicAnalysis::DispatchToLineFillBuffer(uint64_t Cycle)
{
    vector<InstructionDispatchInfo>::iterator it = DispatchToLineFillBufferQueue.begin();
    for(; it != DispatchToLineFillBufferQueue.end();) 
    {    
        if ((*it).IssueCycle == InstructionFetchCycle) 
        {
            LineFillBufferCompletionCycles.push_back((*it).CompletionCycle);
            it = DispatchToLineFillBufferQueue.erase(it);
        }
        else
            ++it;
    }
}



uint64_t
DynamicAnalysis::FindIssueCycleWhenLineFillBufferIsFull()
{    
    size_t BufferSize = DispatchToLineFillBufferQueue.size();
    
    if ( BufferSize== 0) 
    {
        return GetMinCompletionCycleLineFillBuffer();
    }
    else
    {
        if (BufferSize >= (unsigned)LineFillBufferSize) 
        {
            // Iterate from end-LineFillBufferSize
            uint64_t EarliestCompletion = DispatchToLineFillBufferQueue.back().CompletionCycle;
            for(vector<InstructionDispatchInfo>::iterator it = DispatchToLineFillBufferQueue.end()-1;
                    it >= DispatchToLineFillBufferQueue.end()-LineFillBufferSize; --it)
            {
                if ((*it).CompletionCycle < EarliestCompletion) 
                {
                    EarliestCompletion =(*it).CompletionCycle;
                }
            }
            
            return EarliestCompletion;
        }
        else
        {
            sort(LineFillBufferCompletionCycles.begin(), LineFillBufferCompletionCycles.end());
            
            return LineFillBufferCompletionCycles[BufferSize];
        }
    }
}



uint64_t
DynamicAnalysis::FindIssueCycleWhenLoadBufferIsFull()
{    
    size_t BufferSize = DispatchToLoadBufferQueue.size();
    
    if (BufferSize == 0) 
    {
        return GetMinCompletionCycleLoadBuffer();
    }
    else
    {
        
        // Iterate through the DispathToLoadBufferQueue and get the
        // largest dispatch cycle. The new load cannot be dispatched
        // until all previous in Dispatch Queue have been dispatched.
        // At the same time,
        uint64_t EarliestDispatchCycle = 0;
        
        for(vector<InstructionDispatchInfo>::iterator it = DispatchToLoadBufferQueue.begin();
                it != DispatchToLoadBufferQueue.end(); ++it)
        {
            EarliestDispatchCycle = max(EarliestDispatchCycle, (*it).IssueCycle);
        }
        EarliestDispatchCycle = MaxDispatchToLoadBufferQueueTree;
        
        //Traverse LB and count how many elements are there smaller than EarliestDispathCycle
        unsigned counter = 0;
        for(vector<uint64_t>::iterator it = LoadBufferCompletionCycles.begin();
                it != LoadBufferCompletionCycles.end(); ++it)
        {
            if ((*it) <= EarliestDispatchCycle)
                counter++;
        }
        uint64_t IssueCycle = 0;
        // This means that in LB, there are more loads that terminate before or in
        // my dispatch cycle -> IssueCycle is Earliest
        if (counter > BufferSize) 
        {
            IssueCycle = EarliestDispatchCycle;
        }
        else
        {
            if (counter == BufferSize) 
            {
                // Iterate through both, DispatchBufferQueue and LB to determine the smallest
                // completion cycle which is larger than EarliestDispatchCycle.
                // Initialize with the Completion cycle of the last element of the
                // DispatchToLoadBufferQueue
                IssueCycle = DispatchToLoadBufferQueue.back().CompletionCycle;
                for(vector<InstructionDispatchInfo>::iterator it = DispatchToLoadBufferQueue.begin();
                        it != DispatchToLoadBufferQueue.end(); ++it)
                {
                    if ((*it).CompletionCycle > EarliestDispatchCycle)
                        IssueCycle = min(IssueCycle,(*it).CompletionCycle);
                }
                
                // We have to also iterate over the completion cycles of the LB even
                // if there are more elements in the DispatchQueue than the size
                // of the LB. Because it can happen than all the elements of the
                // DispatchQueue are complemente even before than an element in the
                // LB which is waiting very long for a resource.
                // We could, nevertheless, simplify it. We can keep the max and
                // the min completion cycle always. If the max completion cycle
                // is smaller than the EarliestDispatchCycle, then it is not necessary
                // to iterate over the LB.
                for(vector<uint64_t>::iterator it = LoadBufferCompletionCycles.begin();
                        it != LoadBufferCompletionCycles.end(); ++it)
                {
                    if ((*it) > EarliestDispatchCycle+1)
                        IssueCycle = min(IssueCycle,*it);
                }
                
            }
            else
            {
                report_fatal_error("Error in Dispatch to Load Buffer Queue");   
            }
        }
        
        
        // The idea before was that the lower "BufferSize" elements of the sorted
        // LB are associated to the dispatch cycles of the elements in the DispatchQueue.
        // But this sorting is very expensive.

        return IssueCycle;        
    }
    
    // unreachable
}




uint64_t
DynamicAnalysis::FindIssueCycleWhenLoadBufferTreeIsFull()
{
    size_t BufferSize = node_size(DispatchToLoadBufferQueueTree);
    
    if ( BufferSize== 0) 
    {
        return GetMinCompletionCycleLoadBufferTree();
    }
    else
    {
        
        // Iterate through the DispathToLoadBufferQueue and get the
        // largest dispatch cycle. The new load cannot be dispatched
        // untill all previous in Dispatch Queue have been dispatched.
        // At the same time,
        uint64_t EarliestDispatchCycle = 0;
        uint64_t AvailableSlots = 0;
        EarliestDispatchCycle = MaxDispatchToLoadBufferQueueTree;
        
        DEBUG(dbgs() << "EarliestDispatchCycle " << EarliestDispatchCycle << "\n");
        //Traverse LB and count how many elements are there smaller than EarliestDispathCycle
        uint64_t SlotsCompleteBeforeDispatch = 0;
        uint64_t SlotsCompleteAfterDispatch = 0;
        
        LoadBufferCompletionCyclesTree = splay(EarliestDispatchCycle, LoadBufferCompletionCyclesTree);
        DEBUG(dbgs() << "LoadBufferCompletionCyclesTree->key " << LoadBufferCompletionCyclesTree->key << "\n");
        if (LoadBufferCompletionCyclesTree->left != NULL)
        {
            DEBUG(dbgs() << "Left was not null\n");
            SlotsCompleteBeforeDispatch = node_size(LoadBufferCompletionCyclesTree->left);
            if (LoadBufferCompletionCyclesTree->key <= EarliestDispatchCycle) 
            {
                DEBUG(dbgs() << "They key equal to Earliest, so increase counter by 1\n");
                SlotsCompleteBeforeDispatch++;
            }
        }
        else
        {
            DEBUG(dbgs() << "Left was null\n");
            if (LoadBufferCompletionCyclesTree->key == EarliestDispatchCycle) 
            {
                DEBUG(dbgs() << "but they key equal to Earliest, so counter in 1\n");
                SlotsCompleteBeforeDispatch=1;
            }
            else
            {
                SlotsCompleteBeforeDispatch = 0;
            }
        }
        
        
        AvailableSlots = SlotsCompleteBeforeDispatch;
        DEBUG(dbgs() << "AvailableSlots "<<AvailableSlots<<"\n");
        
        // Traverse DispatchToLoadBufferQueuteTree and count how many
        // complete after my EarliestDispatchCycle
        
        
        DispatchToLoadBufferQueueTree = splay(EarliestDispatchCycle, DispatchToLoadBufferQueueTree);
        if(DispatchToLoadBufferQueueTree->key > EarliestDispatchCycle)
        {
            // All complete after
            SlotsCompleteAfterDispatch = node_size(DispatchToLoadBufferQueueTree);
        }
        else
        {
            if (DispatchToLoadBufferQueueTree->right != NULL) 
            {
                SlotsCompleteAfterDispatch = node_size(DispatchToLoadBufferQueueTree->right);
            }
            else
            {
                SlotsCompleteAfterDispatch = 1;
            }
        }
        DEBUG(dbgs() << "SlotsCompleteAfterDispatch "<<SlotsCompleteAfterDispatch<<"\n");
        
        AvailableSlots -= SlotsCompleteAfterDispatch;
        DEBUG(dbgs() << "AvailableSlots "<<AvailableSlots<<"\n");
        
        //Compute how many complete before dispatch from the DispatchQueue
        
        
        uint64_t IssueCycle = 0;
        // This means that in LB, there are more loads that terminate before or in
        // my dispatch cycle -> IssueCycle is Earliest
        if (AvailableSlots > 0) 
        {
            IssueCycle = EarliestDispatchCycle;
        }
        else
        {
            // if (counter == BufferSize) {
            DEBUG(dbgs() << "Counter is <= to BufferSize\n");
            // Iterate thtough both, DispatchBufferQueue and LB to determine the smallest
            // completion cycle which is larger than EarliestDispatchCycle.
            // Initialize with the Completion cycle of the last element of the
            // DispatchToLoadBufferQueue
            //DispatchToLoadBufferQueueTree = splay(EarliestDispatchCycle+1, DispatchToLoadBufferQueueTree);
            
            DEBUG(dbgs() << "Find    in DispatchToLoadBufferQueueTree the largest than or equal to "<< EarliestDispatchCycle+1<<"\n");
            
            ComplexTree<uint64_t>* Node = DispatchToLoadBufferQueueTree;
            
            while (true) 
            {
                // This is the mechanism used in the original algorithm to delete the host
                // node,    decrementing the last_record attribute of the host node, and
                // the size attribute of all parents nodes.
                // Node->size = Node->size-1;
                if (EarliestDispatchCycle+1 < Node->key) 
                {
                    
                    if (Node->left == NULL)
                        break;
                    if (Node->left->key < EarliestDispatchCycle+1) 
                    {
                        break;
                    }
                    Node = Node->left;
                }
                else
                {
                    if (EarliestDispatchCycle+1 > Node-> key) 
                    {
                        if (Node->right == NULL)
                            break;
                        Node = Node->right;
                    }
                    else
                    { // Last = Node->key, i.e., Node is the host node
                        break;
                    }
                }
            }
            
            IssueCycle = Node->key;
            DEBUG(dbgs() << "IssueCycle "<<IssueCycle<<"\n");
            
            // We have to also iterate over the completion cycles of the LB even
            // if there are more elements in the DispatchQueue than the size
            // of the LB. Because it can happen than all the elements of the
            // DispatchQueue are complemente even before than an element in the
            // LB which is waiting very long for a resource.
            // We could, nevertheless, simplify it. We can keep the max and
            // the min completion cycle always. If the max completion cycle
            // is smaller than the EarliestDispatchCycle, then it is not necessary
            // to iterate over the LB.
            // The root has EraliestDispatchCycle+1 if found, or the smallest otherwise.
            
            
            //Get the closest larger than or equal to EarliestaDispatchCycle
            DEBUG(dbgs() << "The same with LB\n");
            
            
            SimpleTree<uint64_t> * TmpNode = LoadBufferCompletionCyclesTree;
            
            while (true) 
            {
                // This is the mechanism used in the original algorithm to delete the host
                // node,    decrementing the last_record attribute of the host node, and
                // the size attribute of all parents nodes.
                // Node->size = Node->size-1;
                if (EarliestDispatchCycle+1 < TmpNode->key) 
                {
                    if (TmpNode->left == NULL)
                        break;
                    if (TmpNode->left->key < EarliestDispatchCycle+1) 
                    {
                        break;
                    }
                    TmpNode = TmpNode->left;
                }
                else
                {
                    if (EarliestDispatchCycle+1 > TmpNode-> key) 
                    {
                        if (TmpNode->right == NULL)
                            break;
                        TmpNode = TmpNode->right;
                    }
                    else
                    { // Last = Node->key, i.e., Node is the host node
                        break;
                    }
                }
            }
            
            if (TmpNode->key > EarliestDispatchCycle+1) 
            {
                IssueCycle = min(TmpNode->key, IssueCycle);
            }
            DEBUG(dbgs() << "IssueCycle "<<IssueCycle<<"\n");
        }
        
        
        
        // The idea before was that the lower "BufferSize" elements of the sorted
        // LB are associated to the dispatch cycles of the elements in the DispatchQueue.
        // But this sorting is very expensive.

        return IssueCycle;
    }
    
    // unreachable
}




uint64_t
DynamicAnalysis::FindIssueCycleWhenStoreBufferIsFull()
{    
    size_t BufferSize = DispatchToStoreBufferQueue.size();
    
    if ( BufferSize== 0) 
    {
        return GetMinCompletionCycleStoreBuffer();
    }
    else
    {
        if (BufferSize >= (unsigned)StoreBufferSize) 
        {
            uint64_t EarliestCompletion = DispatchToStoreBufferQueue.back().CompletionCycle;
            for(vector<InstructionDispatchInfo>::iterator it = DispatchToStoreBufferQueue.end()-1;
                    it >= DispatchToStoreBufferQueue.end()-StoreBufferSize; --it)
            {
                if ((*it).CompletionCycle < EarliestCompletion) {
                    EarliestCompletion =(*it).CompletionCycle;
                }
            }
            return EarliestCompletion;
        }
        else
        {
            sort(StoreBufferCompletionCycles.begin(), StoreBufferCompletionCycles.end());
            return StoreBufferCompletionCycles[BufferSize];
        }
    }
}


void
DynamicAnalysis::IncreaseInstructionFetchCycle(bool EmptyBuffers)
{
    bool OOOBufferFull = false;
    unsigned TreeChunk = 0;
    uint64_t OriginalInstructionFetchCycle = InstructionFetchCycle;
    
    // Remove from Reservation Stations elements issued at fetch cycle
    if (ReservationStationSize > 0)
    {
        RemoveFromReservationStation(InstructionFetchCycle);
    }
    
    if (ReorderBufferSize > 0)
    {
        RemoveFromReorderBuffer(InstructionFetchCycle);
    }

    //Remove from Load, Store and Fill Line Buffers elements completed at issue cycle
    
    //RemoveFromLoadBuffer(InstructionFetchCycle);
    
    RemoveFromLoadBufferTree(InstructionFetchCycle);
    RemoveFromStoreBuffer(InstructionFetchCycle);
    RemoveFromLineFillBuffer(InstructionFetchCycle);
    //RemoveFromDispatchToLoadBufferQueue(InstructionFetchCycle);
    RemoveFromDispatchToStoreBufferQueue(InstructionFetchCycle);
    RemoveFromDispatchToLineFillBufferQueue(InstructionFetchCycle);
    // Insert into LB, SB and LFB the instructions from the dispatch queue.
    //DispatchToLoadBuffer(InstructionFetchCycle);
    DispatchToLoadBufferTree(InstructionFetchCycle);
    DispatchToStoreBuffer(InstructionFetchCycle);
    DispatchToLineFillBuffer(InstructionFetchCycle);

    uint64_t CurrentInstructionFetchCycle = InstructionFetchCycle;
    
    // If Reservation station is full
    if (ReservationStationIssueCycles.size() == ReservationStationSize && ReservationStationSize > 0) 
    {
        // Advance InstructionFetchCyle until min issue cycle
        OOOBufferFull = true;
        InstructionFetchCycle = GetMinIssueCycleReservationStation();
        
        if (InstructionFetchCycle > CurrentInstructionFetchCycle + 1)
        {
            FirstNonEmptyLevel[RS_STALL] = (FirstNonEmptyLevel[RS_STALL]==0)?CurrentInstructionFetchCycle+1:FirstNonEmptyLevel[RS_STALL];
        }
        
        
        for (uint64_t i = CurrentInstructionFetchCycle + 1; i< InstructionFetchCycle; i++) 
        {
            TreeChunk = GetTreeChunk(i);
            FullOccupancyCyclesTree[TreeChunk].insert_node(i, RS_STALL);
            InstructionsCountExtended[RS_STALL]++;
            InstructionsLastIssueCycle[RS_STALL] =i;
        }
    }
    
    if (ReorderBufferCompletionCycles.size() == ReorderBufferSize && ReorderBufferSize > 0) 
    {
        //Advance InstructionFetchCycle to the head of the buffer
        OOOBufferFull = true;
        InstructionFetchCycle = max(InstructionFetchCycle, ReorderBufferCompletionCycles.front());
        
        if (InstructionFetchCycle > CurrentInstructionFetchCycle + 1) 
        {
            FirstNonEmptyLevel[ROB_STALL] = (FirstNonEmptyLevel[ROB_STALL]==0)?CurrentInstructionFetchCycle+1:FirstNonEmptyLevel[ROB_STALL];
        }
        
        for (uint64_t i = CurrentInstructionFetchCycle + 1; i < InstructionFetchCycle; i++) 
        {
            // Get the node, if any, corresponding to this issue cycle.
            TreeChunk = GetTreeChunk(i);
            
            FullOccupancyCyclesTree[TreeChunk].insert_node(i, ROB_STALL);
            InstructionsCountExtended[ROB_STALL]++;
            InstructionsLastIssueCycle[ROB_STALL] =i;
        }
    }
    
    if (OOOBufferFull == true) 
    {
        // Remove from Reservation Stations elements issued at fetch cycle
        
        RemoveFromReservationStation(InstructionFetchCycle);
        RemoveFromReorderBuffer(InstructionFetchCycle);
        
        //Remove from Load, Store and Fill Line Buffers elements completed at issue cycle
        // RemoveFromLoadBuffer(InstructionFetchCycle);
        RemoveFromLoadBufferTree(InstructionFetchCycle);
        RemoveFromStoreBuffer(InstructionFetchCycle);
        RemoveFromLineFillBuffer(InstructionFetchCycle);
        //    RemoveFromDispatchToLoadBufferQueue(InstructionFetchCycle);
        RemoveFromDispatchToStoreBufferQueue(InstructionFetchCycle);
        RemoveFromDispatchToLineFillBufferQueue(InstructionFetchCycle);
        // Insert into LB, SB and LFB the instructions from the dispatch queue.
        //    DispatchToLoadBuffer(InstructionFetchCycle);
        DispatchToLoadBufferTree(InstructionFetchCycle);
        DispatchToStoreBuffer(InstructionFetchCycle);
        DispatchToLineFillBuffer(InstructionFetchCycle);
    }
    
    // When we are at this point, either we have removed from RS or ROB the
    // instructions issued at this cycle, and they left some empty slots
    // so that the buffers are not full any more, or we have advanced
    // InstructionFetchCycle to the cycle at which any of the buffers
    // gets empty. In this case, we also have to set Remaining instructions
    // to fetch to Fetch bandwidth, because we have modified fetch cycle
    // and we start fetching again.
    if (OOOBufferFull == true) 
    {
        RemainingInstructionsFetch = InstructionFetchBandwidth;
    }
    
    if (EmptyBuffers == true) 
    {
        InstructionFetchCycle++;
    }
    else if (RemainingInstructionsFetch == 0 && InstructionFetchBandwidth != INF)
    {
        InstructionFetchCycle++;
        RemainingInstructionsFetch = InstructionFetchBandwidth;
    }
    
    BuffersOccupancy[RS_STALL-RS_STALL] += ReservationStationIssueCycles.size();
    BuffersOccupancy[ROB_STALL-RS_STALL] += ReorderBufferCompletionCycles.size();
    //BuffersOccupancy[LB_STALL-RS_STALL] += LoadBufferCompletionCycles.size();
    BuffersOccupancy[LB_STALL-RS_STALL] += node_size(LoadBufferCompletionCyclesTree);
    BuffersOccupancy[SB_STALL-RS_STALL] += StoreBufferCompletionCycles.size();
    BuffersOccupancy[LFB_STALL-RS_STALL] += LineFillBufferCompletionCycles.size();
    
    
    if (OriginalInstructionFetchCycle != InstructionFetchCycle) 
    {
        uint64_t PrevInstructionFetchCycle = InstructionFetchCycle - 1;
        if (DispatchToLineFillBufferQueue.empty() == false) 
        {
            if (InstructionsCountExtended[LFB_STALL]==0)
                FirstIssue[LFB_STALL] = true;
            if (FirstIssue[LFB_STALL]==true) 
            {
                FirstNonEmptyLevel[LFB_STALL] = PrevInstructionFetchCycle;
                FirstIssue[LFB_STALL] = false;
            }
            
            //FirstNonEmptyLevel[LFB_STALL] = (FirstNonEmptyLevel[LFB_STALL]==0)?InstructionFetchCycle:FirstNonEmptyLevel[LFB_STALL];
            InstructionsLastIssueCycle[LFB_STALL] =PrevInstructionFetchCycle;
            //FullOccupancyCyclesTree[PrevInstructionFetchCycle/SplitTreeRange] = insert_node(PrevInstructionFetchCycle, LFB_STALL, FullOccupancyCyclesTree[PrevInstructionFetchCycle/SplitTreeRange]);
            FullOccupancyCyclesTree[(PrevInstructionFetchCycle/SplitTreeRange)].insert_node(PrevInstructionFetchCycle, LFB_STALL);
            // We do it when an instruction is inserted. Otherwise, SourceCodeLine has the value
            // of the last instruction analyzed from the instruction fetch window, which
            // might not be the instruction that was stalled.
            
            InstructionsCountExtended[LFB_STALL]++;
        }
        
        if (node_size(DispatchToLoadBufferQueueTree) != 0) {
            //if (DispatchToLoadBufferQueue.empty() == false) {
            if (InstructionsCountExtended[LB_STALL]==0)
                FirstIssue[LB_STALL] = true;
            if (FirstIssue[LB_STALL]==true) {
                FirstNonEmptyLevel[LB_STALL] = PrevInstructionFetchCycle;
                FirstIssue[LB_STALL] = false;
            }
            //FirstNonEmptyLevel[LB_STALL] = (FirstNonEmptyLevel[LB_STALL]==0)?InstructionFetchCycle:FirstNonEmptyLevel[LB_STALL];
            InstructionsLastIssueCycle[LB_STALL] =PrevInstructionFetchCycle;
            //FullOccupancyCyclesTree[PrevInstructionFetchCycle/SplitTreeRange] = insert_node(PrevInstructionFetchCycle, LB_STALL, FullOccupancyCyclesTree[PrevInstructionFetchCycle/SplitTreeRange]);
            FullOccupancyCyclesTree[(PrevInstructionFetchCycle/SplitTreeRange)].insert_node(PrevInstructionFetchCycle, LB_STALL);
            InstructionsCountExtended[LB_STALL]++;
        }
        
        if (DispatchToStoreBufferQueue.empty() == false) 
        {
            if (InstructionsCountExtended[SB_STALL]==0)
                FirstIssue[SB_STALL] = true;
            if (FirstIssue[SB_STALL]==true) 
            {
                FirstNonEmptyLevel[SB_STALL] = PrevInstructionFetchCycle;
                FirstIssue[SB_STALL] = false;
            }
            
            //FirstNonEmptyLevel[SB_STALL] = (FirstNonEmptyLevel[SB_STALL]==0)?InstructionFetchCycle:FirstNonEmptyLevel[SB_STALL];
            InstructionsLastIssueCycle[SB_STALL] = PrevInstructionFetchCycle;
            //FullOccupancyCyclesTree[PrevInstructionFetchCycle/SplitTreeRange] = insert_node(PrevInstructionFetchCycle, SB_STALL,FullOccupancyCyclesTree[PrevInstructionFetchCycle/SplitTreeRange]);
            FullOccupancyCyclesTree[(PrevInstructionFetchCycle/SplitTreeRange)].insert_node(PrevInstructionFetchCycle, SB_STALL);
            
            InstructionsCountExtended[SB_STALL]++;
        }
    }
}

void
DynamicAnalysis::processNonPhiNode(Instruction& UseI, map<unsigned, uint64_t>* useDegree)
{
	auto _InstructionType = getInstructionType(UseI);
    auto _opcode = UseI.getOpcode();
	if (_InstructionType >= 0)
	{
	    auto _ExtendedInstructionType = GetExtendedInstructionType(_opcode); 
	    auto _ExecutionResource = ExecutionUnit[_ExtendedInstructionType];
	
	    // Reuse distance unknown for the use of the future 
	    //  All memops are hence colapsed
	    if (_opcode == Instruction::Load)
	    {
	        _ExecutionResource = MEM_LOAD_CHANNEL;
	    }
	    else if (_opcode == Instruction::Store)
	    {
	        _ExecutionResource = MEM_STORE_CHANNEL;
	    }
	
	    ++((*useDegree)[_ExecutionResource]);
    }
}

// Look at all uses of the phiNode
//  Ex: a = 5/3; pa = phi(a, b); d = pa + c; 
//      We need to count the use of 'a' as a '+'
void
DynamicAnalysis::processPhiNode(Instruction& UseI, map<unsigned, uint64_t>* useDegree)
{
    for(Value::use_iterator i = UseI.use_begin(), ie = UseI.use_end(); i != ie; ++i)
    {
        User* U = *i;
        if (Instruction* UseUseI = dyn_cast<Instruction>(U))
        {
            if (!dyn_cast<PHINode>(U))
            {
                processNonPhiNode(*UseUseI, useDegree);
            }
            else
            {
                processPhiNode(*UseUseI, useDegree);
            }
        }
    }
}

#if 0
// Conservatively update all possible (i.e., all who eventually use PHI directly) resources' contribution (to histogram)
void
DynamicAnalysis::processPhiNode(PHINode& PN, map<unsigned, uint64_t>* useDegree)
{
    for (unsigned i = 0; i < PN.getNumIncomingValues(); ++i)
    {
        Value* use = PN.getIncomingValue(i);
        if (Instruction* PhiUse = dyn_cast<Instruction>(use))
        {
            if (!dyn_cast<PHINode>(use))
            {
                errs() << PN << ":\t" << *PhiUse << "\n";
                processNonPhiNode(*PhiUse, useDegree);
            }
            else
            {
                PHINode* UPN = dyn_cast<PHINode>(use);
                processPhiNode(*UPN, useDegree);
            }
        }
    }
}
#endif

#if 0
void
getDepth(User& UU, uint64_t* depth)
{
    errs() << UU.getName() << "\t" << UU << "\n";
    uint64_t tmp = 0;
    for (Value::use_iterator UUI = UU.use_begin(), UUE = UU.use_end(); UUI != UUE; ++UUI)
    {
        User *U = *UUI;
        bool isUUuseOfU = false;
        for (Value::use_iterator UUJ = U->use_begin(), UUJE = U->use_end(); UUJ != UUJE; ++UUJ)
        {
            if ((*UUJ)->getName() == UU.getName())
            {
                isUUuseOfU = true;
                break;
            }
            if (*depth != 0 || tmp != 0)
                errs() << **UUJ << " != " << UU << "\n";
        }
        if (!isUUuseOfU)
        {
            getDepth(*U, &tmp);
        }
    }
    *depth += tmp;
    if (*depth != 0)
       errs() << UU << "\t" << *depth << "\n";
}
#endif


//===----------------------------------------------------------------------===//
//                    Main method for analysis of the instruction properties
//                                        (dependences, reuse, etc.)
//===----------------------------------------------------------------------===//


// Handling instructions dependences with def-use chains.
// Whenever there is a def, and we know the issue cycle (IssueCycle )of the def,
// update all the uses of that definition with IssueCycle+1.
// The issue cycle of an instruction is hence the max of the issue cycles of its
// operands, but the issue cycle of its operands does not have to be determined,
// already contains the right value because they are uses of a previous definition.

uint64_t
DynamicAnalysis::analyzeInstruction(Instruction &I, uint64_t addr)
{
    int k = 0;
    int Distance = 0;
    int InstructionType = getInstructionType(I);
    uint64_t CacheLine = 0;
    uint64_t MemoryAddress = 0;
    uint64_t LoadCacheLine = 0;
    uint64_t StoreCacheLine = 0;
    uint64_t InstructionIssueCycle = 0, OriginalInstructionIssueCycle = 0, LastAccess = 0;
    uint64_t InstructionIssueFetchCycle = 0, InstructionIssueLoadBufferAvailable = 0,
    InstructionIssueLineFillBufferAvailable = 0, 
    InstructionIssueDataDeps = 0, InstructionIssueCacheLineAvailable = 0,
    InstructionIssueMemoryModel = 0, InstructionIssueThroughputAvailable = 0;
    uint Latency = 0;
    unsigned NumArgs;
    unsigned ExtendedInstructionType = InstructionType;
    unsigned ExecutionResource = 0;
    
    TotalInstructions++;
    
    // Determine instruction width
    int NumOperands = I.getNumOperands();
    
    //================= Update Fetch Cycle, remove insts from buffers =========//
    // EVERY INSTRUCTION IN THE RESERVATION STATION IS ALSO IN THE REORDER BUFFER
 
    DEBUG(dbgs()<<    I<< " ("<< &I <<")\n");
    if (InstructionType >= 0) 
    {
        //
        // This is currently a while loop, yet the call can advance fetch in a single step to a sufficient value rather than
        //   taking this incremental approach.  In effect, the loop will be executed only once.
        // Note also, the loop will trigger often from the RemainingInstructionsFetch clause.
        //
        while (RemainingInstructionsFetch == 0 || 
            (ReorderBufferCompletionCycles.size() == (unsigned)ReorderBufferSize && ReorderBufferSize != 0) || 
            (ReservationStationIssueCycles.size() == (unsigned)ReservationStationSize && ReservationStationSize != 0))
        {
            IncreaseInstructionFetchCycle();
        }
    }
    
    //==================== Handle special cases ===============================//
    switch (I.getOpcode()) 
    {
            // Dependences through PHI nodes
        case Instruction::Br:
        case Instruction::IndirectBr:
        case Instruction::Switch:
        {
            InstructionIssueCycle = max(max(InstructionFetchCycle,BasicBlockBarrier),getInstructionValueIssueCycle(&I)); // This is the branch instrucion
            
            //Iterate over the uses of the generated value
            for(Value::use_iterator i = I.use_begin(), ie = I.use_end(); i!=ie; ++i)
            {
                insertInstructionValueIssueCycle(*i, InstructionIssueCycle+1/*???*/);
            }
            break;
        }
            
        case Instruction::PHI:
        {
            InstructionIssueCycle = max(max(InstructionFetchCycle,BasicBlockBarrier),getInstructionValueIssueCycle(&I)); // This is the branch instrucion

            // Iterate through the uses of the PHI node
            //for (User *U : I.users()) {
            for(Value::use_iterator i = I.use_begin(), ie = I.use_end(); i!=ie; ++i)
            {
                User *U = *i;
                if (dyn_cast<PHINode>(U)) 
                {
                    insertInstructionValueIssueCycle(U, InstructionIssueCycle, true);
                }
                else
                {
                    insertInstructionValueIssueCycle(U, InstructionIssueCycle);
                }
                
                
            }
            break;
        }
            // Dependences through the arguments of a method call
        case Instruction::Call:
        {
            //Aux vars for handling arguments of a call instruction
            CallSite CS = CallSite(&I);
            std::vector<Value *> ArgVals;
            
            // Loop over the arguments of the called function --- From Execution.cpp
            NumArgs = CS.arg_size();
            ArgVals.reserve(NumArgs);
            for (CallSite::arg_iterator i = CS.arg_begin(),
                     e = CS.arg_end(); i != e; ++i) {
                Value *V = *i;
                ArgVals.push_back(V);
            }
            InstructionIssueCycle = max(max(InstructionFetchCycle, BasicBlockBarrier), getInstructionValueIssueCycle(&I));
            
            break;
        }
            
            //-------------------- Memory Dependences -------------------------------//
        case Instruction::Load:
            if (InstructionType >= 0) 
            {
                MemoryAddress = addr;
                CacheLine = MemoryAddress >> BitsPerCacheLine;
                
                LoadCacheLine = CacheLine;
                
                CacheLineInfo Info = getCacheLineInfo(LoadCacheLine);
                
                //Code for reuse calculation
                Distance = ReuseDistance(Info.LastAccess, TotalInstructions + InstOffset, CacheLine);
                
                // Get the new instruction type depending on the reuse distance
                ExtendedInstructionType = GetExtendedInstructionType(Instruction::Load, Distance);
                
                if (ExtendedInstructionType == L1_LOAD_NODE) 
                {
                    Q[0] += AccessGranularities[nCompExecutionUnits + 0];
                    //    fprintf(stderr, "\tL1\n");
                } 
                else if (ExtendedInstructionType == L2_LOAD_NODE) 
                {
                    Q[2] += AccessGranularities[nCompExecutionUnits + 2];
                    //    fprintf(stderr, "\tL2\n");
                } 
                else if (ExtendedInstructionType == L3_LOAD_NODE) 
                {
                    Q[3] += AccessGranularities[nCompExecutionUnits + 3];
                    //    fprintf(stderr, "\tL3\n");
                } 
                else if (ExtendedInstructionType == MEM_LOAD_NODE) 
                {
                    Q[4] += AccessGranularities[nCompExecutionUnits + 4];
                    //    fprintf(stderr, "\tMem\n");
                } 
                else 
                {
                    report_fatal_error("Load Mem op has nowhere to live");
                }
                
                ExecutionResource = ExecutionUnit[ExtendedInstructionType];
                Latency = ExecutionUnitsLatency[ExecutionResource];
                // UpdateInstructionCount(InstructionType,ExtendedInstructionType, NElementsVector, IsVectorInstruction);
                InstructionsCount[InstructionType]++;
                
                InstructionIssueFetchCycle = InstructionFetchCycle;
                
                if (ExtendedInstructionType >= L1_LOAD_NODE)
                {
                    InstructionIssueCacheLineAvailable = Info.IssueCycle;
#ifdef DEBUG_GENERIC
                    dbgs() << "IICLA: " << InstructionIssueCacheLineAvailable << "\n";
                    dbgs() << "CycleOFfset: " << CycleOffset << "\n";
                    dbgs() << "TotalInstructions: " << TotalInstructions << "\n";
                    dbgs() << "InstOffset: " << InstOffset << "\n";
#endif    
                    if (InstructionIssueCacheLineAvailable < CycleOffset)
                    {
                        InstructionIssueCacheLineAvailable = 0;
                    }
                    else
                    {
                        InstructionIssueCacheLineAvailable -= CycleOffset;
                    }
                }
                
                insertCacheLineLastAccess(LoadCacheLine, TotalInstructions + InstOffset);
                
                //Calculate issue cycle depending on buffer Occupancy.
                if (LoadBufferSize > 0) 
                {
                    if (node_size(LoadBufferCompletionCyclesTree) == LoadBufferSize) 
                    {
                        InstructionIssueLoadBufferAvailable = FindIssueCycleWhenLoadBufferTreeIsFull();
                        
                        // If, moreover, the instruction has to go to the LineFillBuffer...
                        if (ExtendedInstructionType >= L2_LOAD_NODE && LineFillBufferSize > 0) 
                        {
                            if (LineFillBufferCompletionCycles.size() == (unsigned)LineFillBufferSize) 
                            {
                                InstructionIssueLineFillBufferAvailable = FindIssueCycleWhenLineFillBufferIsFull();
                            }
                        }
                    }
                    else
                    { // If the Load Buffer is not full...
                        if (ExtendedInstructionType >= L2_LOAD_NODE && LineFillBufferSize > 0) 
                        { // If it has to go to the LFS...
                            
                            if (LineFillBufferCompletionCycles.size() == LineFillBufferSize || !DispatchToLineFillBufferQueue.empty() ) 
                            {
                                InstructionIssueLineFillBufferAvailable = FindIssueCycleWhenLineFillBufferIsFull();
                            }
                            else
                            { // There is space on both
                                // Do nothing -> Instruction Issue Cycle is not affected by LB or LFB Occupancy
                                //Later, insert in both
                            }
                        }
                        else
                        { // It does not have to go to LFB...
                            //Do nothing (insert into LoadBuffer later, afte knowing IssueCycle
                            // depending on BW availability. Right not IssueCycle is not affected)
                        }
                    }
                }
                // If there is Load buffer, the instruction can be dispatched as soon as
                // the buffer is available. Otherwise, both the AGU and the execution resource
                // must be available
                InstructionIssueDataDeps = getMemoryAddressIssueCycle(MemoryAddress) ;//- CycleOffset;
                if (InstructionIssueDataDeps < CycleOffset)
                {
                        InstructionIssueDataDeps = 0;
                }
                else
                {
                    InstructionIssueDataDeps -= CycleOffset;
                }
                
                // New for memory model
                
                InstructionIssueMemoryModel = LastLoadIssueCycle;
                
                InstructionIssueCycle = max(max(max(InstructionIssueFetchCycle, InstructionIssueLoadBufferAvailable),
                                                max(InstructionIssueLineFillBufferAvailable,InstructionIssueDataDeps)), 
                                            max(InstructionIssueCacheLineAvailable, InstructionIssueMemoryModel));
                updateReuseDistanceDistribution(Distance, InstructionIssueCycle + CycleOffset);
                
#ifdef DEBUG_GENERIC
                dbgs() << "IIC: " << InstructionIssueCycle << "\n";
                dbgs() << "IIFC: " << InstructionIssueFetchCycle << "\n";
                dbgs() << "IILBA: " << InstructionIssueLoadBufferAvailable << "\n";
                dbgs() << "IILFBA: " << InstructionIssueLineFillBufferAvailable << "\n";
                dbgs() << "IIDP: " << InstructionIssueDataDeps << "\n";
                dbgs() << "IICLA: " << InstructionIssueCacheLineAvailable << "\n";
                dbgs() << "InstructionIssueMemoryModel: " << InstructionIssueMemoryModel << "\n";
#endif
                
                if (LoadBufferSize > 0) 
                {
                    InstructionIssueThroughputAvailable = FindNextAvailableIssueCycle(InstructionIssueCycle, ExecutionResource);
#ifdef DEBUG_GENERIC
                    dbgs() << "IIC: " << InstructionIssueCycle << "\n";
                    dbgs() << "IITA :" << InstructionIssueThroughputAvailable << "\n"; 
#endif
                    
                    InsertNextAvailableIssueCycle(InstructionIssueThroughputAvailable, ExecutionResource);
                }
                else
                {
                    InstructionIssueThroughputAvailable = FindNextAvailableIssueCycle(InstructionIssueCycle,ExecutionResource);
                        
                    InsertNextAvailableIssueCycle(InstructionIssueThroughputAvailable, ExecutionResource);
                }
                
                InstructionIssueCycle = max(InstructionIssueCycle, InstructionIssueThroughputAvailable);
            }
            
            break;
            // The Store can execute as soon as the value being stored is calculated
        case Instruction::Store:
            if (InstructionType >= 0) 
            {
                uint64_t InstructionIssueStoreBufferAvailable = 0;
                MemoryAddress = addr;
                CacheLine = MemoryAddress >> BitsPerCacheLine;
                
                StoreCacheLine = CacheLine;
                LastAccess = getCacheLineLastAccess(StoreCacheLine);
                
                CacheLineInfo Info = getCacheLineInfo(StoreCacheLine);
                Distance = ReuseDistance(LastAccess, TotalInstructions + InstOffset, CacheLine);
                ExtendedInstructionType = GetExtendedInstructionType(Instruction::Store, Distance);
                
                if (ExtendedInstructionType == L1_STORE_NODE) 
                {
                    Q[1] += AccessGranularities[nCompExecutionUnits + 1];
                    //fprintf(stderr, "\tL1\n");
                } 
                else if (ExtendedInstructionType == L2_STORE_NODE) 
                {
                    Q[2] += AccessGranularities[nCompExecutionUnits + 2];
                    //fprintf(stderr, "\tL2\n");
                } 
                else if (ExtendedInstructionType == L3_STORE_NODE) 
                {
                    Q[3] += AccessGranularities[nCompExecutionUnits + 3];
                    //fprintf(stderr, "\tL3\n");
                } 
                else if (ExtendedInstructionType == MEM_STORE_NODE) 
                {
                    Q[4] += AccessGranularities[nCompExecutionUnits + 4];
                    //fprintf(stderr, "\tMem\n");
                } 
                else 
                {
                    report_fatal_error("Store Mem op has nowhere to live");
                }
                
                ExecutionResource = ExecutionUnit[ExtendedInstructionType];
                Latency = ExecutionUnitsLatency[ExecutionResource];
                // Do not update any more, update in InsertNextAvailableIssueCycle so that
                // ports are also updated. But need to update the InstructionsCount
                // UpdateInstructionCount(InstructionType, ExtendedInstructionType, NElementsVector, IsVectorInstruction);
                InstructionsCount[InstructionType]++;
                
                InstructionIssueFetchCycle = InstructionFetchCycle;
                
                if (ExtendedInstructionType >= L1_STORE_NODE)
                {
                    InstructionIssueCacheLineAvailable = Info.IssueCycle;// - CycleOffset;
#ifdef DEBUG_GENERIC
                    dbgs() << "IICLA: " << InstructionIssueCacheLineAvailable << "\n";
                    dbgs() << "CycleOFfset: " << CycleOffset << "\n";
                    dbgs() << "TotalInstructions: " << TotalInstructions << "\n";
                    dbgs() << "InstOffset: " << InstOffset << "\n";
#endif    
                    if (InstructionIssueCacheLineAvailable < CycleOffset)
                    {
                            InstructionIssueCacheLineAvailable = 0;
                    }
                    else
                    {
                            InstructionIssueCacheLineAvailable -= CycleOffset;
                    }
                }
                
                InstructionIssueDataDeps = getInstructionValueIssueCycle(&I);
                
                //Calculate issue cycle depending on buffer Occupancy.
                if (StoreBufferSize > 0) 
                {
                    if (StoreBufferCompletionCycles.size() == StoreBufferSize) 
                    { // If the store buffer is full
                        InstructionIssueStoreBufferAvailable = FindIssueCycleWhenStoreBufferIsFull();
                        // If, moreover, the instruction has to go to the LineFillBuffer...
                        if (ExtendedInstructionType >= L2_LOAD_NODE && LineFillBufferSize > 0) 
                        {
                            if (LineFillBufferCompletionCycles.size() == (unsigned)LineFillBufferSize) 
                            {
                                InstructionIssueLineFillBufferAvailable = FindIssueCycleWhenLineFillBufferIsFull();
                            }
                        }
                        
                    }
                    else
                    { // If the Store Buffer is not fulll...
                        if (ExtendedInstructionType >= L2_LOAD_NODE && LineFillBufferSize > 0) 
                        { // If it has to go to the LFS...
                            
                            if (LineFillBufferCompletionCycles.size() == LineFillBufferSize || !DispatchToLineFillBufferQueue.empty() )
                            {
                                InstructionIssueLineFillBufferAvailable = FindIssueCycleWhenLineFillBufferIsFull();
                            }
                            else
                            { // There is space on both
                                // Do nothing -> Instruction Issue Cycle is not affected by LB or LFB Occupancy
                                //Later, insert in both
                            }
                        }
                        else
                        { // It does not have to go to LFB...
                            //Do nothing (insert into LoadBuffer later, afte knowing IssueCycle
                            // depending on BW availability. Right not IssueCycle is not affected)
                        }
                    }
                }
                
                // Writes are not reordered with other writes
                InstructionIssueMemoryModel = LastStoreIssueCycle;
                // Writes are not reordered with earlier reads
                // The memory-ordering model ensures that a store by a processor may not occur before a previous load by the same processor.
                InstructionIssueMemoryModel = max(InstructionIssueMemoryModel, LastLoadIssueCycle);
                
                InstructionIssueCycle = max(max(max(InstructionIssueFetchCycle, InstructionIssueStoreBufferAvailable), 
                                                    max(InstructionIssueDataDeps, InstructionIssueCacheLineAvailable)),
                                                InstructionIssueMemoryModel);
                
                // When a cache line is written does not impact when in can be loaded again.
                updateReuseDistanceDistribution(Distance, InstructionIssueCycle + CycleOffset);
                //insertCacheLineInfo(StoreCacheLine, Info);
                insertCacheLineLastAccess(StoreCacheLine, TotalInstructions +InstOffset);
                
                // If there is a store buffer, the dispatch cycle might be different from
                // the issue (execution) cycle.
                if (StoreBufferSize > 0) 
                {
                    InstructionIssueThroughputAvailable = FindNextAvailableIssueCycle(InstructionIssueCycle, ExecutionResource);
                    
                    InsertNextAvailableIssueCycle(InstructionIssueThroughputAvailable, ExecutionResource);
                }
                else
                {
                    InstructionIssueThroughputAvailable= FindNextAvailableIssueCycle(InstructionIssueCycle,ExecutionResource);
                    
                    InsertNextAvailableIssueCycle(InstructionIssueThroughputAvailable, ExecutionResource);
                }
                
                InstructionIssueCycle = max(InstructionIssueCycle, InstructionIssueThroughputAvailable);
            }
            break;
            
        case    Instruction::Ret:
        {
            // Determine the uses of the returned value outside the function
            //(i.e., the uses of the calling function)
            // Check http://llvm.org/docs/ProgrammersManual.html for a lot
            // of info about how iterate through functions, bbs, etc.
            Function *F = I.getParent()->getParent();
            InstructionIssueCycle = max(max(InstructionFetchCycle,BasicBlockBarrier),getInstructionValueIssueCycle(&I));
            for (Value::use_iterator IT = F->use_begin(), ET = F->use_end(); IT != ET; ++IT) 
            {
                // Iterate over the users of the uses of the function
                for (Value::use_iterator it = (*IT)->use_begin(), ite = (*IT)->use_end(); it != ite; ++it)
                {
                    insertInstructionValueIssueCycle(*it, InstructionIssueCycle);
                }
            }
            break;
        }
            
            //-------------------------General case------------------------------//
        default:
        {
            if (InstructionType == 0) 
            {
                OriginalInstructionIssueCycle = getInstructionValueIssueCycle(&I);
                InstructionIssueCycle = max(max(InstructionFetchCycle,BasicBlockBarrier),OriginalInstructionIssueCycle);
                
                ExtendedInstructionType = GetExtendedInstructionType(I.getOpcode());
                // UpdateInstructionCount(InstructionType, ExtendedInstructionType,    NElementsVector, IsVectorInstruction);
                InstructionsCount[InstructionType]++;
                
                Latency = ExecutionUnitsLatency[ExtendedInstructionType];
                
                if (InstructionIssueCycle > OriginalInstructionIssueCycle) 
                {
                    NInstructionsStalled[ExtendedInstructionType]++;
                }
                InstructionIssueThroughputAvailable = FindNextAvailableIssueCyclePortAndThroughtput(InstructionIssueCycle, ExtendedInstructionType);
                
                InstructionIssueCycle = max(InstructionIssueCycle, InstructionIssueThroughputAvailable);
            } 
            else 
            {
                OriginalInstructionIssueCycle = getInstructionValueIssueCycle(&I);
                InstructionIssueCycle = max(max(InstructionFetchCycle,BasicBlockBarrier),OriginalInstructionIssueCycle);
                #ifdef DEBUG_OP_COVERAGE
                if (!ignoredOps.count(I.getOpcode())) {
                    dbgs() << "Not modeled:";
                    dbgs() << "\t" << I << "\n";
                    printOpName(I.getOpcode());
                    ignoredOps.insert(I.getOpcode());    
                }
                #endif 
            }
            break;
        }
    }
    
    if (InstructionType >= 0) 
    {
        uint64_t NewInstructionIssueCycle = InstructionIssueCycle;
        
        if (I.getOpcode() == Instruction::Load) 
        {
            LastLoadIssueCycle = NewInstructionIssueCycle;
        }
        else if (I.getOpcode() == Instruction::Store)
        {
            LastStoreIssueCycle = NewInstructionIssueCycle;
        }
        
        // A load can execute as soon as all its operands are available, i.e., all
        // the values that are being loaded. If the same value is loaded again, without
        // having been used in between (Read After Read dependence), then the next load
        // can only be issued after the first one has finished.
        // This only applies to memory accesses > L1. If we access a cache line at cycle
        // X which is in L3, e.g., it has a latency of 30. The next    time this cache line
        // is accessed it is in L1, but it is inconsistent to assume that it can be
        // loaded also at cycle X and have a latency of 4 cycles.
        
        ExecutionResource = ExecutionUnit[ExtendedInstructionType];
        
        if (I.getOpcode() == Instruction::Load && 
            ExtendedInstructionType > L1_LOAD_NODE &&
            ExecutionUnitsLatency[ExecutionResource] > ExecutionUnitsLatency[L1_LOAD_CHANNEL])
        {
            
            CacheLineInfo Info = getCacheLineInfo(LoadCacheLine);
            Info.IssueCycle = NewInstructionIssueCycle+Latency + CycleOffset;
            insertCacheLineInfo(LoadCacheLine, Info);
            insertMemoryAddressIssueCycle(MemoryAddress, NewInstructionIssueCycle+Latency + CycleOffset);
        }
        
        if (I.getOpcode() == Instruction::Store && 
            ExtendedInstructionType > L1_STORE_NODE &&
            ExecutionUnitsLatency[ExecutionResource] > ExecutionUnitsLatency[L1_LOAD_CHANNEL]) 
        {
            CacheLineInfo Info = getCacheLineInfo(StoreCacheLine);
            Info.IssueCycle = NewInstructionIssueCycle+Latency + CycleOffset;
            insertCacheLineInfo(StoreCacheLine, Info);
            insertMemoryAddressIssueCycle(MemoryAddress, NewInstructionIssueCycle+Latency+ CycleOffset);
        }
        
        //Iterate over the uses of the generated value (except for GetElementPtr)
        if (I.getOpcode() != Instruction::GetElementPtr)
        {
            //auto& useDegree = nUsesHist[ExecutionResource];
            //uint64_t& depth = ddgDepth[ExecutionResource];
            for(Value::use_iterator i = I.use_begin(), ie = I.use_end(); i!=ie; ++i)
            {
                User *U = *i;

                #if 0
                if (Instruction* UseI = dyn_cast<Instruction>(U))
                {   
                    if (!dyn_cast<PHINode>(U))
                    {
                        #if 0
	                    if (ExecutionResource == INT_DIVIDER && UseI->getOpcode() != Instruction::ICmp)
                        {
	                        errs() << I << ":\t" << *UseI << "\t|||\t" << UseI->getOpcodeName() << "\n"; 
                        }
                        #endif
                        processNonPhiNode(*UseI, &useDegree);
                    }
                    else 
                    {
                        #if 0
                        PHINode* PN = dyn_cast<PHINode>(U);
                        processPhiNode(*PN, &useDegree);
                        #endif
                        processPhiNode(*UseI, &useDegree);
                    }
                }   
                #endif

                #if 0
                bool isIuseOfU = false;
                for (Value::use_iterator i2 = U->use_begin(), i2e = U->use_end(); i2 != i2e; ++i2)
                {
                    if ((*i2)->getName() == I.getName())
                    {
                        isIuseOfU = true;
                        break;
                    }
                }
                if (!isIuseOfU)
                {   
                    uint64_t tmp = 0;
                    getDepth(*U, &tmp);
                    depth += tmp; 
                }
                #endif

                if (dyn_cast<PHINode>(U)) 
                {
                    insertInstructionValueIssueCycle(U, NewInstructionIssueCycle+Latency, true);
                }
                else
                {
                    insertInstructionValueIssueCycle(U, NewInstructionIssueCycle+Latency);
                }
                
                if (dyn_cast<CallInst>(U))
                {
                    CallSite CS = CallSite(U);
                    Function *F = CS.getCalledFunction();
                    std::vector<Value *> ArgVals;
                    
                    // Loop over the arguments of the called function --- From Execution.cpp
                    NumArgs = CS.arg_size();
                    ArgVals.reserve(NumArgs);
                    for (CallSite::arg_iterator j = CS.arg_begin(),
                             e = CS.arg_end(); j != e; ++j) {
                        Value *V = *j;
                        ArgVals.push_back(V);
                    }
                    
                    // Make sure it is an LLVM-well-defined funciton
                    if (static_cast<Function*>(F)) 
                    {
                        for (Function::arg_iterator AI = F->arg_begin(), E = F->arg_end();
                                 AI != E; ++AI, ++k)
                        {
                            if (ArgVals[k] == &I)
                            {
                                for(Value::use_iterator vi = (*AI).use_begin(), vie = (*AI).use_end(); vi!=vie; ++vi)
                                {
                                    insertInstructionValueIssueCycle(*vi,NewInstructionIssueCycle+Latency);
                                }
                            }
                        }
                    }
                }
            }
        }
        
        
        //---------------------- End of Update Parallelism Distribution--------------------//
        
        // ------------ Work with limited instruction issue window ----------------//
        
        //When InstructionFetchBandwidth is INF, remaining instructions to fetch
        // is -1, but still load and stores must be inserted into the OOO buffers
        if (RemainingInstructionsFetch > 0 || RemainingInstructionsFetch == INF ) 
        {
            if (RemainingInstructionsFetch > 0)
                RemainingInstructionsFetch--;
            
            uint64_t CycleInsertReservationStation =0 ;
            if (I.getOpcode() ==Instruction::Load) 
            {
                // If LB is not full, they go directly to the LB and to the RS
                // If LB is INF, this comparison is false. But still
                // we need to check wether RS is INF
                
                if (node_size(LoadBufferCompletionCyclesTree) == LoadBufferSize && LoadBufferSize > 0)
                {
                    // if(LoadBufferCompletionCycles.size() == LoadBufferSize && LoadBufferSize > 0){
                    
                    // Put in the reservation station, but only if RS exists
                    //     CycleInsertReservationStation = FindIssueCycleWhenLoadBufferIsFull();
                    CycleInsertReservationStation = FindIssueCycleWhenLoadBufferTreeIsFull();
                    ReservationStationIssueCycles.push_back(CycleInsertReservationStation);
                    
                    //Put in the DispatchToLoadBufferQueue
                    if (DispatchToLoadBufferQueueTree == NULL) 
                    {
                        MaxDispatchToLoadBufferQueueTree = CycleInsertReservationStation;
                    }
                    else
                    {
                        MaxDispatchToLoadBufferQueueTree = max(MaxDispatchToLoadBufferQueueTree,CycleInsertReservationStation );
                        
                    }
                    
                    DispatchToLoadBufferQueueTree = insert_node(NewInstructionIssueCycle+Latency,CycleInsertReservationStation, DispatchToLoadBufferQueueTree);
                    
                    
                    
                    // If, moreover, the instruction has to go to the LineFillBuffer...
                    if (ExtendedInstructionType >= L2_LOAD_NODE && LineFillBufferSize > 0) 
                    {
                        if (LineFillBufferCompletionCycles.size() == LineFillBufferSize || !DispatchToLineFillBufferQueue.empty()) 
                        {
                            InstructionDispatchInfo DispathInfo;
                            DispathInfo.IssueCycle = NewInstructionIssueCycle; //FindIssueCycleWhenLineFillBufferIsFull();
                            DispathInfo.CompletionCycle = NewInstructionIssueCycle+Latency;
                            DispatchToLineFillBufferQueue.push_back(DispathInfo);  
                        }
                        else
                        { // There is space on both
                            LineFillBufferCompletionCycles.push_back(NewInstructionIssueCycle+Latency);
                        }
                    }
                }
                else
                {
                    //If LB is not full
                    if (node_size(LoadBufferCompletionCyclesTree) != LoadBufferSize && LoadBufferSize > 0) 
                    {
                        //Insert into LB
                        // LoadBufferCompletionCycles.push_back(NewInstructionIssueCycle+Latency);
                        if (node_size(LoadBufferCompletionCyclesTree) == 0) 
                        {
                            MinLoadBuffer = NewInstructionIssueCycle+Latency;
                        }
                        else
                        {
                            MinLoadBuffer = min(MinLoadBuffer,NewInstructionIssueCycle+Latency);
                        }
                        LoadBufferCompletionCyclesTree = insert_node(NewInstructionIssueCycle+Latency, LoadBufferCompletionCyclesTree);
                        if (ExtendedInstructionType >= L2_LOAD_NODE && LineFillBufferSize != 0) 
                        { // If it has to go to the LFS...
                            if (LineFillBufferCompletionCycles.size() == LineFillBufferSize || !DispatchToLineFillBufferQueue.empty()) 
                            {
                                InstructionDispatchInfo DispathInfo;
                                DispathInfo.IssueCycle = NewInstructionIssueCycle; //FindIssueCycleWhenLineFillBufferIsFull();
                                DispathInfo.CompletionCycle = NewInstructionIssueCycle+Latency;
                                DispatchToLineFillBufferQueue.push_back(DispathInfo);
                            }
                            else
                            { // There is space on both
                                LineFillBufferCompletionCycles.push_back(NewInstructionIssueCycle+Latency);
                            }
                        }
                        else
                        { // It does not have to go to LFB...
                            // Insert into LoadBuffer, what we have already done.
                        }
                    }
                    else
                    {
                        //If LB is zero.... Insert into into RS, if it exists
                        if (ReservationStationSize > 0) 
                        {
                            CycleInsertReservationStation = NewInstructionIssueCycle;
                            ReservationStationIssueCycles.push_back(CycleInsertReservationStation);
                        }
                    }
                }
            }
            else
            {
                if (I.getOpcode() == Instruction::Store)
                {
                    if (StoreBufferCompletionCycles.size() == StoreBufferSize && StoreBufferSize > 0 ) 
                    {
                        CycleInsertReservationStation = FindIssueCycleWhenStoreBufferIsFull();
                        ReservationStationIssueCycles.push_back(CycleInsertReservationStation);
                        InstructionDispatchInfo DispathInfo;
                        DispathInfo.IssueCycle = NewInstructionIssueCycle;
                        //TODO: Isn't it CycleInsertReservationStation?
                        //DispathInfo.IssueCycle = FindIssueCycleWhenStoreBufferIsFull();
                        DispathInfo.CompletionCycle = NewInstructionIssueCycle+Latency;
                        DispatchToStoreBufferQueue.push_back(DispathInfo);
                    }
                    else
                    { // If it is not full
                        if (StoreBufferCompletionCycles.size()!=StoreBufferSize    && StoreBufferSize > 0 ) 
                        {
                            StoreBufferCompletionCycles.push_back(NewInstructionIssueCycle+Latency);
                        }
                        else
                        {
                            // If StoreBufferSize == 0, insert into RS if it exists
                            if (ReservationStationSize > 0) 
                            {
                                CycleInsertReservationStation = NewInstructionIssueCycle;
                                ReservationStationIssueCycles.push_back(CycleInsertReservationStation);
                            }
                        }
                    }
                }
                else
                {
                    // Not load nor store -> Insert into RS if its size is > -1
                    if (ReservationStationSize > 0) 
                    {
                        CycleInsertReservationStation = NewInstructionIssueCycle;
                        ReservationStationIssueCycles.push_back(CycleInsertReservationStation);
                    }
                }
            }
            
            if (ReorderBufferSize > 0) 
            {
                ReorderBufferCompletionCycles.push_back(NewInstructionIssueCycle+Latency);
            }
        }
    }
    else
    {
        // This instruction is not implemented
        //   We will preserve the dependency graph
        for (Value::use_iterator i = I.use_begin(), ie = I.use_end(); i != ie; ++i)
        {
            User *U = *i;
            
            insertInstructionValueIssueCycle(U, InstructionIssueCycle);
        }
    }
    
    return InstructionIssueCycle;
}
