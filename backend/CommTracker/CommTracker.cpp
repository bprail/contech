#include "CommTracker.hpp"
using namespace contech;

bool sameContech(TaskId a, TaskId b)
{
    return a.getContextId() == b.getContextId();
}


void CommTracker::addFree(uint64_t base, TaskId task, uint bbId)
{
    // Look up the size of the allocation
    unsigned long size = allocation[base];

    // Clear the entry
    for (uint i = 0; i < size; i++)
    {
        uint64_t byteaddr = base + i;
        addrTable.erase(byteaddr);
    }

    // Free the memory
    allocation.erase(base);
}


void CommTracker::addAllocate(uint64_t base, unsigned long size, TaskId task, uint bbId)
{
    // Remember the size of the allocation
    allocation[base] = size;

    // Record a write to all addresses in the allocation
    for (uint i = 0; i < size; i++)
    {
        uint64_t byteaddr = base + i;
        addWrite(byteaddr, 1, task, bbId, 0);
    }
}

void CommTracker::addRead(uint64_t addr, unsigned char size, TaskId task, uint bbId, short pos)
{
    bool hit = false;
    for (uint i = 0; i < size; i++)
    {
        uint64_t byteaddr = addr + i;
        addrEntry& a = addrTable[byteaddr];

        // Don't record communication within a single contech
        if (sameContech(task, a.ownerTask)) continue;

        // Record a read if this contech is not already a sharer
        ct_sharers_t mask = getSharersMask(task);
        if (!(a.sharers & mask))
        {
            records.push_back(CommRecord(a.ownerTask, byteaddr, task, a.ownerBlock,a.ownerPos, bbId, pos));
            a.sharers |= mask;
            a.lastReaderTask = task;
            a.lastReaderBlock = bbId;
            a.lastReaderPos = pos;
        }
        else
        {
            // If any byte accesses hit, assume they all hit
            hit = true;
        }
    }
    if (hit)
    {
        memOpStats& m = blockTable[bbId][pos];
        m.numHits += 1;
    }
}

void CommTracker::addWrite(uint64_t addr, unsigned char size, TaskId task, uint bbId, short pos)
{
    bool hit = false, migratory = false;
    memOpStats* lastReaderOp;
    // Set this task as owner and invalidate sharers
    for (uint i = 0; i < size; i++)
    {
        uint64_t byteaddr = addr + i;
        addrEntry& a = addrTable[byteaddr];

        // "Hit"
        if (sameContech(a.ownerTask, task))
        {
            hit = true;
        }

        // Migratory data
        else if
        (
            !sameContech(a.ownerTask, task)             && // I'm not the owner of the data
            (getSharersMask(task) & a.sharers)          && // But I previously read the data
            __builtin_popcount(a.sharers) == 2             // And the owner is the only other sharer
        )
        {
            lastReaderOp = &blockTable[a.lastReaderBlock][a.lastReaderPos];
            migratory = true;
        }

        a.ownerTask = task;
        a.ownerBlock = bbId;
        a.ownerPos = pos;
        a.sharers = getSharersMask(task);
    }
    memOpStats& m = blockTable[bbId][pos];
    if (hit)
    {
        m.numHits += 1;
    }
    else if (migratory)
    {
        m.numMigratory += 1;
        // Mark the op where this contech first read the data as migratory
        lastReaderOp->numMigratory += 1;
    }

}

void CommTracker::countBlock(uint bbId) { blockTable[bbId].runCount += 1; }

vector<CommRecord>& CommTracker::getRecords() { return records; }
map<uint, bbStats>& CommTracker::getBbStats() { return blockTable; }

ct_sharers_t CommTracker::getSharersMask(TaskId id)
{
    return 1 << (uint)id.getContextId();
}

char* CommTracker::getSharersString(ct_sharers_t m)
{
    int s = 256;
    char* r;
    int i = 0;
    int pos;

redo:
    pos = 0;
    r = (char*) malloc(sizeof(char) * s);
    pos += snprintf(r, s, "(");

    for (i = 0; i < sizeof(unsigned long long) * 8; i++)
    {
        if (m & (((unsigned long long) 1) << i))
        {
            if (pos > 1)
            {
                pos += snprintf(r + pos, s - pos, ",%d", i);
            }
            else
            {
                pos += snprintf(r + pos, s - pos, "%d", i);
            }
        }
        if (pos >= s)
        {
            s *= 2;
            free(r);
            goto redo; // RUUUN! THE VELOCIRAPTORS ARE COMING!!!
        }
    }

    snprintf(r + pos, s - pos, ")");

    return r;
}

std::ostream& operator<<(std::ostream &out, const CommTracker &rhs)
{
    std::string q = "\"";
    bool first = true;
    out
    << "{" << std::endl
    << q << "records" << q << ":" << std::endl
    << "[" << std::endl;
    for (auto& record : rhs.records)
    {
        if (!first) { out << "," << std::endl; }
        else { first = false; }
        out << "\t" << record;
    }
    out << std::endl
    << "]}" << std::endl;
    return out;
}


CommTracker* CommTracker::fromFile(ct_file* taskGraphIn)
{
    CommTracker* tracker = new CommTracker();

    while(Task* currentTask = Task::readContechTask(taskGraphIn))
    {
        TaskId uid = currentTask->getTaskId();

        // Iterate through every basic block
        for (auto f = currentTask->getBasicBlockActions().begin(), e = currentTask->getBasicBlockActions().end(); f != e; f++)
        {
            BasicBlockAction a = *f;
            uint bbId = a.basic_block_id;
            tracker->countBlock(bbId);
            short pos = 0;

            for (auto ff = f.getMemoryActions().begin(), ee = f.getMemoryActions().end(); ff != ee; ff++)
            {
                MemoryAction mem = *ff;
                switch (mem.type)
                {
                    case action_type_mem_write:
                    {
                        tracker->addWrite(mem.addr, 1 << mem.pow_size, uid, bbId, pos++);
                        break;
                    }

                    case action_type_mem_read:
                    {
                        tracker->addRead(mem.addr, 1 << mem.pow_size, uid, bbId, pos++);
                        break;
                    }

                    case action_type_free:
                    {
                        tracker->addFree(mem.addr, uid, bbId);
                        break;
                    }

                    case action_type_malloc:
                    {
                        // Size of allocation is stored in next memop
                        MemoryAction s = *(++ff);
                        if (s.type != action_type_size) { cerr << "Invalid data in memory action stream: Missing size of allocation." << endl; for (auto& z : f.getMemoryActions()) {cerr << z.toString();} cerr << endl;exit(1); }
                        tracker->addAllocate(mem.addr, s.addr, uid, bbId);
                        break;
                    }

                    default:
                    {
                        cerr << "Unexpected memory action type: type=" << mem.type << endl; exit(1);
                        break;
                    }
                }
            }
        }

        delete currentTask;
    }
    return tracker;
}

