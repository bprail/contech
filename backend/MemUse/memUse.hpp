#ifndef MEM_USE_HPP
#define MEM_USE_HPP

#include <stdio.h>
#include <stdint.h>
#include <map>
#include "../../common/taskLib/TaskGraph.hpp"
#include "../../common/taskLib/Backend.hpp"

class BackendMemUse : public contech::Backend {

private:
struct malloc_stats {
    uint32_t alloc_count;
    uint32_t free_count;
};

struct track_stats {
    uint32_t size;
    uint32_t hits;
};
    uint32_t seqAllocCurrent, seqAllocMax;
    uint32_t seqFreeCurrent, seqFreeMax;
    uint64_t totalAllocSize, effAllocSize;
    uint64_t maxWorkSet, currWorkSet;
    uint64_t stackSize;
    std::map<uint32_t, malloc_stats> sizeStats;
    std::map<uint64_t, uint32_t> sizeOfAlloc;
    std::map<uint64_t, track_stats> sizeOfAllocNoErase;
    std::map<uint64_t, uint32_t> stackAlloc;
    std::map<uint64_t, int> refCountPlus; // If more than 1 ref to address exist

public:
    void resetBackend();
    void updateBackend(contech::Task*);
    void completeBackend(FILE*, contech::TaskGraphInfo*);
    BackendMemUse();
};

#endif