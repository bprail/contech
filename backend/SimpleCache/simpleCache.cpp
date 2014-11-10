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
    //printf("Cache created: %d of %d\n", cacheBlocks.size(), 0x1 << global_s); 
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
    uint64_t tAccess = ~0x0;
    
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
    }
    
    // The block is not in the cache
    //   First check if there is space to place it in the cache
    if (cacheBlocks[idx].size() < (0x1<<global_s))
    {
        cache_line t;
        
        t.tag = tag;
        t.dirty = write;
        t.lastAccess = num;
        
        cacheBlocks[idx].push_back(t);
        return false;
    }
    
    // No space for the block, something needs to be evicted
    bool writeBack = oldest->dirty;
    assert(oldest != cacheBlocks[idx].end());
    if (global_r == LRU)
    {
        cacheBlocks[idx].erase(oldest);
        assert(cacheBlocks[idx].size() < (0x1 << global_s));
    }
    
    {
        cache_line t;
        
        t.tag = tag;
        t.dirty = write;
        t.lastAccess = num;
        
        cacheBlocks[idx].push_back(t);
    }
    
    return false;
}

bool SimpleCache::updateCache(bool write, char numOfBytes, uint64_t address, cache_stats_t* p_stats)
{
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
    }
    
    if (bbMissCount > 0)
    {
        if (write) {write_misses++;}
        else {read_misses++;}
        p_stats->misses ++;
        
        return false;
    }
    
    return true;
}

double SimpleCache::getMissRate()
{
    return (double)(read_misses + write_misses) / (double)(accesses);
}

SimpleCacheBackend::SimpleCacheBackend(uint64_t c, uint64_t s, int printMissLoc) {
    global_c = c;
    global_s = s;
    assert(global_c >= (global_s + global_b));
    // zero out p_stats
    p_stats = new cache_stats_t;
    p_stats->accesses = 0;
    p_stats->misses = 0;
    
    if (printMissLoc == 1)
        printMissLines = true;
    else
        printMissLines = false;
}

void SimpleCacheBackend::updateBackend(Task* currentTask)
{
    //auto memOps = currentTask->getMemOps();
    ContextId ctid = currentTask->getContextId();
    bool rw = false;
    ctid = 0;
    
    uint64_t lastBBID = 0;
    auto bbOps = currentTask->getBasicBlockActions();
    for (auto iBBs = bbOps.begin(), eBBs = bbOps.end(); iBBs != eBBs; ++iBBs)
    {
        BasicBlockAction bba = *iBBs;
        lastBBID = bba.basic_block_id;
        auto memOps = iBBs.getMemoryActions();
        unsigned int memOpPos = 0;
        for (auto iReq = memOps.begin(), eReq = memOps.end(); iReq != eReq; ++iReq, memOpPos++)
        {
            MemoryAction ma = *iReq;

            if (ma.type == action_type_malloc || ma.type == action_type_free) {continue;}
            if (ma.type == action_type_memcpy)
            {
                uint64_t dstAddress = ma.addr;
                uint64_t srcAddress = 0;
                uint64_t bytesToAccess = 0;
                
                ++iReq;
                ma = *iReq;
                if (ma.type == action_type_memcpy)
                {
                    srcAddress = ma.addr;
                    ++iReq;
                    ma = *iReq;
                }
                
                assert(ma.type == action_type_size);
                bytesToAccess = ma.addr;
                
                char accessSize = 0;
                
                do {
                    accessSize = (bytesToAccess > 8)?8:bytesToAccess;
                    bytesToAccess -= accessSize;
                    if (srcAddress != 0)
                    {
                        contextCacheState[ctid].updateCache(false, accessSize, srcAddress, p_stats);
                        srcAddress += accessSize;
                        p_stats->accesses ++;
                        p_stats->reads++;
                    }
                    
                    contextCacheState[ctid].updateCache(true, accessSize, dstAddress, p_stats);
                    dstAddress += accessSize;
                    p_stats->accesses ++;
                    p_stats->writes++;
                } while (bytesToAccess > 0);
                continue;
            }
            
            char numOfBytes = (0x1 << ma.pow_size);
            uint64_t address = ma.addr;
            char accessBytes = 0;
            
            do {
                // Reduce the memory accesses into 8 byte requests
                accessBytes = (numOfBytes > 8)?8:numOfBytes;
                numOfBytes -= accessBytes;
                p_stats->accesses++;
                
                if (ma.type == action_type_mem_write)
                {
                    p_stats->writes++;
                    rw = true;
                }
                else if (ma.type == action_type_mem_read)
                {
                    p_stats->reads++;
                    rw = false;
                }
                
                if (!contextCacheState[ctid].updateCache(rw, accessBytes, address, p_stats))
                {
                    basicBlockMisses[(lastBBID << 32) + memOpPos] ++;
                }
                address += accessBytes;
            } while (numOfBytes > 0);
        }
    }
}

void SimpleCacheBackend::resetBackend()
{
    contextCacheState.clear();
    basicBlockMisses.clear();
    // TODO: clear p_stats
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
    
    unsigned int maxMissCount = 0;
    uint64_t maxBBMissor = 0;
    for (auto it = basicBlockMisses.begin(), et = basicBlockMisses.end(); it != et; ++it)
    {
        if (it->second > maxMissCount)
        {
            maxMissCount = it->second;
            maxBBMissor = it->first;
        }
    }
    
    std::nth_element(missRates.begin(), missRates.begin() + (missRates.size() / 2), missRates.end());
    double median = *(missRates.begin() + (missRates.size() / 2));
    
    fprintf(f, "%lf, %lf, %lf, %lf, %lf\n", sum / (missRates.size()), 
                                       *min_element(missRates.begin(), missRates.end()), 
                                       median,
                                       *max_element(missRates.begin(), missRates.end()),
                                       (double)(p_stats->misses) / (double)(p_stats->accesses));
    if (printMissLines == false) return;
    
    if (maxMissCount > 0)
    {
        fprintf(f, "Max Misses, Percent Total Misses, BBID, MEMOP, Function, File:line\n");
        for (auto it = basicBlockMisses.begin(), et = basicBlockMisses.end(); it != et; ++it)
        {
            unsigned int bbid = ((it->first) >> 32);
            auto bbi = tgi->getBasicBlockInfo(bbid);
            fprintf(f, "%u, %lf, %u, %u, %s, %s:%u\n", it->second, 
                                        (((it->second) / (double)p_stats->misses) * 100.0),
                                         bbid, 
                                         (unsigned int)(it->first), 
                                         bbi.functionName.c_str(), 
                                         bbi.fileName.c_str(),
                                         bbi.lineNumber);
        }
    }
}
