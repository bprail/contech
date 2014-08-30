#include "memUse.hpp"
#include "../../common/taskLib/Backend.hpp"

using namespace std;
using namespace contech;

BackendMemUse::BackendMemUse()
{
    resetBackend();
}

void BackendMemUse::resetBackend()
{
    seqAllocCurrent = 0; seqAllocMax = 0;
    seqFreeCurrent = 0; seqFreeMax = 0;
    totalAllocSize = 0; effAllocSize = 0;
    maxWorkSet = 0; currWorkSet = 0;
    stackSize = 0;
    sizeStats.clear();
    sizeOfAlloc.clear();
    stackAlloc.clear();
    refCountPlus.clear();
    sizeOfAllocNoErase.clear();
    sizeOfAlloc[~(0x0)] = 0;
    stackAlloc[~(0x0)] = 0;
}

void BackendMemUse::updateBackend(contech::Task* t)
{
    if (t->getType() == task_type_basic_blocks)
    {
        auto MemAct = t->getMemoryActions();
        for (auto it = MemAct.begin(), et = MemAct.end(); it != et; ++it)
        {
            auto ty = (*it).getType();
            if (ty == action_type_malloc)
            {
                uint64_t addr = ((MemoryAction)(*it)).addr;
                ++it;
                assert((*it).getType() == action_type_size);
                uint32_t allocSize = ((MemoryAction)(*it)).addr;
                sizeStats[allocSize].alloc_count++;
                
                if (sizeOfAlloc.find(addr) != sizeOfAlloc.end())
                {
                    // This means that malloc has ordered the alloc / free calls
                    //   in a way beyond what we track.
                    
                    // Do free() on addr
                    uint32_t freeSize = sizeOfAlloc[addr];
                    sizeStats[freeSize].free_count++;
            
                    if (seqFreeCurrent == 0)
                    {
                        if (seqAllocCurrent > seqAllocMax) seqAllocMax = seqAllocCurrent;
                        seqAllocCurrent = 0;
                    }
                    currWorkSet -= freeSize;
                    seqFreeCurrent ++;
                    
                    refCountPlus[addr] += 1;
                }
                sizeOfAlloc[addr] = allocSize;
                sizeOfAllocNoErase[addr] = allocSize;
                
                if (seqAllocCurrent == 0)
                {
                    if (seqFreeCurrent > seqFreeMax) seqFreeMax = seqFreeCurrent;
                    seqFreeCurrent = 0;
                }
                seqAllocCurrent ++;
                totalAllocSize += allocSize;
                currWorkSet += allocSize;
                if (currWorkSet > maxWorkSet)
                    maxWorkSet = currWorkSet;
                if (seqFreeMax == 0)
                    effAllocSize += allocSize;
            }
            else if (ty == action_type_free)
            {
                uint64_t addr = ((MemoryAction)(*it)).addr;
                uint32_t allocSize = sizeOfAlloc[addr];
                
                if (allocSize == 0)
                {
                    allocSize = sizeOfAllocNoErase[addr];
                }
                
                auto rfc = refCountPlus.find(addr);
                if (rfc != refCountPlus.end())
                {
                    if (rfc->second == 1)
                    {
                        refCountPlus.erase(rfc);
                    }
                    else
                    {
                        int val = rfc->second - 1;
                        refCountPlus[addr] = val;
                    }
                    continue;
                }
                
                sizeOfAlloc.erase(addr);
                
                sizeStats[allocSize].free_count++;
            
                if (seqFreeCurrent == 0)
                {
                    if (seqAllocCurrent > seqAllocMax) seqAllocMax = seqAllocCurrent;
                    seqAllocCurrent = 0;
                }
                currWorkSet -= allocSize;
                seqFreeCurrent ++;
            }
            else
            {
                // Read or write
                uint64_t addr = ((MemoryAction)(*it)).addr;
                
                uint64_t size = 0x1 << ((MemoryAction)(*it)).pow_size;

                // Is addr in a known allocation
                //   Does it overlap with 
                auto elem = sizeOfAlloc.upper_bound(addr);
                --elem;
                    
                if (addr >= elem->first && (addr + size) <= (elem->first + elem->second))
                {
                    // HEAP
                }
                else
                {
                    if (!stackAlloc.empty())
                    {
                        elem = stackAlloc.upper_bound(addr);
                        --elem;
                        if (addr >= elem->first && (addr + size) <= (elem->first + elem->second))
                        {
                            // Known stack
                            continue;
                        }
                        
                        // Otherwise we also need to check for accesses that should "extend" the stack
                        if (addr >= elem->first && addr <= (elem->first + elem->second))
                        {
                            uint64_t oldEnd = (elem->first + elem->second);
                            uint64_t newEnd = addr + size;
                            uint64_t delta = newEnd - oldEnd;
                            
                            stackAlloc[elem->first] += delta;
                            stackSize += delta;
                            
                            // Did this access span two stack ranges?
                            auto baseElem = elem;
                            ++elem;
                            if (addr >= (elem )->first)
                            {
                                uint64_t overlap = (baseElem->first + baseElem->second) - elem->first;
                                
                                stackAlloc[baseElem->first] += elem->second - overlap;
                                stackSize += elem->second - overlap;
                                stackAlloc.erase(elem);
                            }
                            continue;
                        }
                        
                        // This access is past the previous element, perhaps it comes before the next
                        ++elem;
                        if (addr < elem->first && (addr + size) >= elem->first)
                        {
                            uint64_t overlap = (addr + size) - elem->first;
                            
                            stackAlloc[addr] = size + elem->second - overlap;
                            stackSize += size - overlap;
                            stackAlloc.erase(elem);
                            continue;
                        }
                    }
                    
                    stackAlloc[addr] = size;
                    stackSize += size;
                }
            }
        }
    }
    else
    {
        // Not basic block task
    }

}

void BackendMemUse::completeBackend(FILE* f, contech::TaskGraphInfo*)
{
    if (seqAllocCurrent > seqAllocMax) seqAllocMax = seqAllocCurrent;
    if (seqFreeCurrent > seqFreeMax) seqFreeMax = seqFreeCurrent;

    fprintf(f, "Memory Allocation Usage Stats, %llu, %llu, %llu, %lf\n", 
                totalAllocSize, maxWorkSet, stackSize, (double)maxWorkSet / (double)(stackSize + maxWorkSet));
    fprintf(f, "Longest Alloc Sequence, %d\n", seqAllocMax);
    fprintf(f, "Longest Free Sequence, %d\n", seqFreeMax);
    fprintf(f, "Total Allocation, %llu\n", totalAllocSize);
    fprintf(f, "Effective Allocation, %llu\n", effAllocSize);
    fprintf(f, "Maximum Heap Alloc, %llu\n", maxWorkSet);
    fprintf(f, "Stack usage, %llu\n", stackSize);
    fprintf(f, "\%Heap in Working Set, %lf\n", (double)maxWorkSet / (double)(stackSize + maxWorkSet));
    for (auto it = sizeStats.begin(), et = sizeStats.end(); it != et; ++it)
    {
        fprintf(f, "%d, %d, %d\n", it->first, it->second.alloc_count, it->second.free_count);
    }
}
