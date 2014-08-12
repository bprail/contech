#include "simpleCache.hpp"
#include <vector>
#include <algorithm>

using namespace std;
using namespace contech;

enum {BLOCKING, SUBBLOCKING};
enum {LRU, NMRU_FIFO};

// This would be better stored as static fields in the class
static uint64_t global_c; 
static const uint64_t global_b = 6; 
static uint64_t global_s; 
static const char global_st = BLOCKING; 
static const char global_r = LRU;

SimpleCache::SimpleCache()
{
    //cacheBlocks.resize(0x1 << (SC_CACHE_SIZE - (SC_CACHE_ASSOC + SC_CACHE_LINE)));
    cacheBlocks.resize(0x1 << (global_c - (global_s + global_b)));
    read_misses = 0;
    write_misses = 0;
    accesses = 0;
}

void SimpleCache::printIndex(uint64_t idx)
{
    for (auto it = cacheBlocks[idx].begin(), et = cacheBlocks[idx].end(); it != et; ++it)
    {
        printf("%llx (%d)\t", it->tag, it->lastAccess);
    }
    printf("\n");
}

bool SimpleCache::updateCacheLine(uint64_t idx, uint64_t tag, uint64_t offset, uint64_t num, bool write)
{
    deque<cache_line>::iterator oldest;
    uint64_t tAccess = ~0x0, mAccess = 0;
    
    char subBlock = 0;
    
    assert(tag != 0);
    
    // Offset greater than first subblock's last byte offset
    if (offset > ((0x1 << (global_b - 1)) - 1)) subBlock = 1;
    subBlock = 0x1 << subBlock;
    
    if (idx >= cacheBlocks.size()) idx -= cacheBlocks.size();
    assert(idx < cacheBlocks.size());
    oldest = cacheBlocks[idx].end();
    
    for (auto it = cacheBlocks[idx].begin(), et = cacheBlocks[idx].end(); it != et; ++it)
    {
        if (it->tag == tag)
        {
            it->dirty = (it->dirty || write);
            it->lastAccess = num;
            
            return true;
        }
        // Is this the LRU block?
        if (it->lastAccess < tAccess)
        {
            tAccess = it->lastAccess;
            oldest = it;
        }
        // Is this the MRU block?
        if (it->lastAccess > mAccess)
        {
            mAccess = it->lastAccess;
        }
    }
    
    // The block is not in the cache
    //   First check if there is space to place it in the cache
    if (cacheBlocks[idx].size() < (0x1<<global_s))
    {
        cache_line t;
        
        t.tag = tag;
        t.dirty = write;
        t.lastAccess = num;
        t.valid_bits = subBlock;
        
        cacheBlocks[idx].push_back(t);
        return false;
    }
    
    // No space for the block, something needs to be evicted
    bool writeBack = oldest->dirty;
    assert(oldest != cacheBlocks[idx].end());
    if (global_r == LRU)
    {
        cacheBlocks[idx].erase(oldest);
    }
    
    {
        cache_line t;
        
        t.tag = tag;
        t.dirty = write;
        t.lastAccess = num;
        t.valid_bits = subBlock;
        
        cacheBlocks[idx].push_back(t);
    }
    
    return false;
}

void SimpleCache::updateCache(bool write, char numOfBytes, uint64_t address, cache_stats_t* p_stats)
{
    bool split = false;
    unsigned int bbMissCount = 0;
    uint64_t cacheIdx = address >> global_b;
    uint64_t offset = address & ((0x1<<global_b) - 1);
    uint64_t size = numOfBytes;
    uint64_t tag = address >> ((global_c - global_s));
    uint64_t accessCount = p_stats->accesses;

    assert(offset < (0x1 << global_b));
    
    cacheIdx &= ((0x1 << (global_c - (global_b + global_s))) - 1);
    
    assert (cacheIdx < cacheBlocks.size());
    accesses++;
    
    if ((offset + size - 1) > ((0x1 << global_b) - 1))
    {
        bbMissCount += !updateCacheLine(cacheIdx, tag, offset, accessCount, write);
        
        // split block access
        uint64_t idx = cacheIdx + 1;
        int64_t sz = offset + size - (0x1 << global_b) - 1;
        while (sz >= 0)
        {
            if (idx >= cacheBlocks.size()) {idx -= cacheBlocks.size(); tag++;}
            bbMissCount += !updateCacheLine(idx, tag, 0, accessCount, write);
            idx++;
            sz -= (0x1 << global_b);
        }
        split = true;
        p_stats->hit_time += 2;
    }
    else
    {
        bbMissCount += !updateCacheLine(cacheIdx, tag, offset, accessCount, write);
        p_stats->hit_time += 1;
    }
    
    if (bbMissCount > 0)
    {
        if (write) {p_stats->write_misses_combined ++; write_misses++;
           // printf("%d: %p (%d)\n", accessCount, address, size);
        }
        else {p_stats->read_misses_combined ++; read_misses++;}
        p_stats->misses ++;
        
        
        
        p_stats->miss_penalty += bbMissCount;
    }
}

double SimpleCache::getMissRate()
{
    return (double)(read_misses + write_misses) / (double)(accesses);
}

SimpleCacheBackend::SimpleCacheBackend(uint64_t c, uint64_t s) {
    global_c = c;
    global_s = s;
    assert(global_c >= (global_s + global_b));
    p_stats = new cache_stats_t;
    // zero out p_stats
}

void SimpleCacheBackend::updateBackend(Task* currentTask)
{
    auto memOps = currentTask->getMemOps();
    ContextId ctid = currentTask->getContextId();
    bool rw = false;
    for (auto iReq = memOps.begin(), eReq = memOps.end(); iReq != eReq; ++iReq)
    {
        MemoryAction ma = *iReq;

        char numOfBytes = (0x1 << ma.pow_size);
        uint64_t address = ma.addr;
    
        p_stats->accesses++;
        
        if (ma.type == action_type_mem_write)
        {
            p_stats->writes++;
            rw = true;
        }
        else
        {
            p_stats->reads++;
            rw = false;
        }
        
        contextCacheState[ctid].updateCache(rw, numOfBytes, address, p_stats);
    }
}

void SimpleCacheBackend::resetBackend()
{
    contextCacheState.clear();
}

void SimpleCacheBackend::completeBackend(FILE* f, contech::TaskGraphInfo* tgi)
{
    vector<double> missRates;
    double sum = 0.0;
    
    for (auto it = contextCacheState.begin(), et = contextCacheState.end(); it != et; ++it)
    {
        double mr = it->second.getMissRate();
        missRates.push_back(mr);
        sum += mr;
    }
    
    std::nth_element(missRates.begin(), missRates.begin() + (missRates.size() / 2), missRates.end());
    double median = *(missRates.begin() + (missRates.size() / 2));
    
    fprintf(f, "%lf, %lf, %lf, %lf\n", sum / (missRates.size()), 
                                       *min_element(missRates.begin(), missRates.end()), 
                                       median,
                                       *max_element(missRates.begin(), missRates.end()));
}
