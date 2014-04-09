#include "../../common/taskLib/TaskGraph.hpp"

#include <string.h>

#include <set>
#include <unordered_map>
#include <unordered_set>
#include <queue>
using namespace contech;


void hbRaceDetector(ct_file* taskGraphIn);

void addTaskToGraphMap(unordered_map<TaskId,vector<TaskId>>& graphMap, Task* t);
bool hbPathExists(unordered_map<TaskId,vector<TaskId>>& graphMap, TaskId start, TaskId end);
void reportRace(MemoryAction existingMop,MemoryAction newMop,TaskId existingCTID,TaskId newCTID,BasicBlockAction bb,int idx);

//Simple container class to bind a memory action and an CTID
class Heltech_memory_op {
public:
    Heltech_memory_op(MemoryAction mop,TaskId ctid);
    Heltech_memory_op();
    MemoryAction  mop;
    TaskId ctid;
};
