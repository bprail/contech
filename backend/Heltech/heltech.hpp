#ifndef HELTECH_HPP
#define HELTECH_HPP

#include "../../common/taskLib/TaskGraph.hpp"

#include <string.h>

#include <set>
#include <unordered_map>
#include <unordered_set>
#include <queue>


void hbRaceDetector(ct_file* taskGraphIn);

void addTaskToGraphMap(unordered_map<contech::TaskId,vector<contech::TaskId> >& graphMap, contech::Task* t);
bool hbPathExists(unordered_map<contech::TaskId, vector<contech::TaskId> >& graphMap, contech::TaskId start, contech::TaskId end);
void reportRace(contech::MemoryAction existingMop, 
                contech::MemoryAction newMop,
                contech::TaskId existingTID,
                contech::TaskId newTID,
                contech::BasicBlockAction bb,
                unsigned int, 
                int idx, 
                contech::TaskGraphInfo*);

//Simple container class to bind a memory action and an CTID
class Heltech_memory_op {
public:
    Heltech_memory_op(contech::MemoryAction mop, unsigned int, contech::TaskId tid);
    Heltech_memory_op();
    contech::MemoryAction  mop;
    unsigned int bbid;
    contech::TaskId tid;
};

#endif
