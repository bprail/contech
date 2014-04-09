#ifndef COMM_TRACKER_H
#define COMM_TRACKER_H


#include "CommRecord.hpp"
#include "../../common/taskLib/TaskGraph.hpp"
#include <iostream>
#include <map>
#include <vector>

// Type for encoding a set of threads as a bit vector
typedef unsigned long long ct_sharers_t;

// Per byte address
struct addrEntry
{
    // Last writer task
    contech::TaskId ownerTask;
    // Last reader task
    contech::TaskId lastReaderTask;
    // Last writer block
    uint ownerBlock;
    // Last reader block
    uint lastReaderBlock;
    // Bit vector of sharer threads
    ct_sharers_t sharers = 0;
    // Position of last writer instruction within the block
    short ownerPos;
    // Position of last read instruction within the block
    short lastReaderPos;
};

struct memOpStats
{
    // Number of times this memOp did not encounter a coherence miss
    uint numHits = 0;
    // Number of times this memOp behaved in a migratory way
    uint numMigratory = 0;
};

// Per basic block
struct bbStats
{
    // Number of times this block ran
    uint runCount = 0;
    // Stats for memOps inside this block
    // TODO More efficient way to represent these. Most blocks are very small, but some have dozens of memOps
    map<uint,memOpStats> memOps;
    memOpStats& operator[](uint i) { return memOps[i]; }
};


class CommTracker
{
public:

    void addFree(uint64_t base, contech::TaskId task, uint bbId);
    void addAllocate(uint64_t base, unsigned long size, contech::TaskId task, uint bbId);
    void addRead (uint64_t addr, unsigned char size, contech::TaskId task, uint bbId, short pos);
    void addWrite(uint64_t addr, unsigned char size, contech::TaskId task, uint bbId, short pos);
    void countBlock(uint bbId);
    vector<CommRecord>& getRecords();
    map<uint, bbStats>& getBbStats();

    static char* getSharersString(ct_sharers_t m);
    static ct_sharers_t getSharersMask(contech::TaskId id);

    static CommTracker* fromFile(ct_file* taskGraphIn);
    friend std::ostream& operator<<(std::ostream &out, const CommTracker &rhs);

private:
    // Maintains an entry for each address
    map<uint64_t, addrEntry> addrTable;

    // Remembers size of allocations
    map<uint64_t, unsigned long> allocation;

    // The complete set of communications that happened in this benchmark
    vector<CommRecord> records;

    // Maintains communication stats per basic block
    map<uint, bbStats> blockTable;

};




#endif
