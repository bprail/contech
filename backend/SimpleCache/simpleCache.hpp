#ifndef SIMPLECACHE_HPP
#define SIMPLECACHE_HPP

#include <Backend.hpp>
#include <vector>
#include <deque>
#include <map>

struct cache_stats_t {
    uint64_t accesses;
    uint64_t misses;
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
    bool updateCache(bool rw, char numOfBytes, uint64_t address, cache_stats_t* p_stats);
};

struct mallocStats
{
    uint32_t bbid;
    uint32_t size;
    uint32_t misses;
};

class SimpleCacheBackend  : public contech::Backend
{
    std::map <contech::ContextId, SimpleCache> contextCacheState;
    std::map <uint64_t, unsigned int> basicBlockMisses;
    std::map <uint64_t, mallocStats> allocBlocks;
    cache_stats_t* p_stats;
    bool printMissLines;

public:
    virtual void resetBackend();
    virtual void updateBackend(contech::Task*);
    virtual void completeBackend(FILE*, contech::TaskGraphInfo*);
    
    SimpleCacheBackend(uint64_t c, uint64_t s, int printMissLoc);
};

#endif
