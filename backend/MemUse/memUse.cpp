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
    sizeStats.clear();
    sizeOfAlloc.clear();
}

void BackendMemUse::updateBackend(contech::Task* t)
{
    if (t->getType() == task_type_basic_blocks)
    {
        auto MemAct = t->getMemoryActions();
        for (auto it = MemAct.begin(), et = MemAct.end(); it != et; ++it)
        {
            if ((*it).getType() == action_type_malloc)
            {
                uint64_t addr = ((MemoryAction)(*it)).addr;
                ++it;
                assert((*it).getType() == action_type_size);
                uint32_t allocSize = ((MemoryAction)(*it)).addr;
                sizeStats[allocSize].alloc_count++;
                sizeOfAlloc[addr] = allocSize;
                
                if (seqAllocCurrent == 0)
                {
                    if (seqFreeCurrent > seqFreeMax) seqFreeMax = seqFreeCurrent;
                    seqFreeCurrent = 0;
                }
                seqAllocCurrent ++;
            }
            else if ((*it).getType() == action_type_free)
            {
                uint64_t addr = ((MemoryAction)(*it)).addr;
                uint32_t allocSize = sizeOfAlloc[addr];
                
                sizeStats[allocSize].free_count++;
            
                if (seqFreeCurrent == 0)
                {
                    if (seqAllocCurrent > seqAllocMax) seqAllocMax = seqAllocCurrent;
                    seqAllocCurrent = 0;
                }
                seqFreeCurrent ++;
            }
        }
    }
    else
    {
    
    }

}

void BackendMemUse::completeBackend(FILE* f, contech::TaskGraphInfo*)
{
    if (seqAllocCurrent > seqAllocMax) seqAllocMax = seqAllocCurrent;
    if (seqFreeCurrent > seqFreeMax) seqFreeMax = seqFreeCurrent;

    fprintf(f, "Memory Allocation Usage Stats\n");
    fprintf(f, "Longest Alloc Sequence: %d\n", seqAllocMax);
    fprintf(f, "Longest Free Sequence: %d\n", seqFreeMax);
    for (auto it = sizeStats.begin(), et = sizeStats.end(); it != et; ++it)
    {
        fprintf(f, "%d, %d, %d\n", it->first, it->second.alloc_count, it->second.free_count);
    }
}
