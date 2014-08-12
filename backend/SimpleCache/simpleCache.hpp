#ifndef SIMPLECACHE_HPP
#define SIMPLECACHE_HPP

#include <Backend.hpp>
#include <vector>
#include <deque>
#include <map>

struct cache_stats_t {
    uint64_t accesses;
    uint64_t reads;
    uint64_t read_misses;
    uint64_t read_misses_combined;
    uint64_t writes;
    uint64_t write_misses;
    uint64_t write_misses_combined;
    uint64_t misses;
    uint64_t hit_time;
    uint64_t miss_penalty;
    double   miss_rate;
    double   avg_access_time;
    uint64_t storage_overhead;
    double   storage_overhead_ratio;
};

class SimpleCache
{
struct cache_line
{
    uint64_t tag;
    uint64_t lastAccess;
    bool dirty;
    char valid_bits;
};

    uint64_t read_misses;
    uint64_t write_misses;
    uint64_t accesses;

    std::vector< std::deque<cache_line> > cacheBlocks;

    bool updateCacheLine(uint64_t idx, uint64_t tag, uint64_t offset, uint64_t num, bool);
    void printIndex(uint64_t idx);

public:    
    SimpleCache();
    double getMissRate();
    void updateCache(bool rw, char numOfBytes, uint64_t address, cache_stats_t* p_stats);
};

class SimpleCacheBackend  : public contech::Backend
{
    std::map <contech::ContextId, SimpleCache> contextCacheState;
    cache_stats_t* p_stats;

public:
    virtual void resetBackend();
    virtual void updateBackend(contech::Task*);
    virtual void completeBackend(FILE*, contech::TaskGraphInfo*);
    
    SimpleCacheBackend(uint64_t c, uint64_t s);
};

#endif
