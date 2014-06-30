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
    uint32_t seqAllocCurrent, seqAllocMax;
    uint32_t seqFreeCurrent, seqFreeMax;
    std::map<uint32_t, malloc_stats> sizeStats;
    std::map<uint64_t, uint32_t> sizeOfAlloc;

public:
    void resetBackend();
    void updateBackend(contech::Task*);
    void completeBackend(FILE*, contech::TaskGraphInfo*);
    BackendMemUse();
};

#endif