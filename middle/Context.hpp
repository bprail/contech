#ifndef CT_CONTECH_HPP
#define CT_CONTECH_HPP

#include <deque>
#include <map>
#include <list>
#include "../common/eventLib/ct_event.h"
#include "../common/taskLib/Task.hpp"

namespace contech {

class Context
{

public:

    Context();

    Task* activeTask();
    Task* createBasicBlockContinuation();
    Task* createContinuation(task_type eventType, ct_tsc_t startTime, ct_tsc_t endTime);
    bool removeTask(Task*);
    Task* getTask(TaskId);
    TaskId getCreator(ContextId);
    void getChildJoin(ContextId, Task*);
    Task* childExits(TaskId);
    bool isCompleteJoin(TaskId);

    // Queue of tasks that are running in this contech but have not been written to file yet. These tasks may have incomplete data.
    // The front of the queue represents more recent tasks.
    list<Task*> tasks;

    // Map of ContextId -> TaskId, which task created which context
    map<ContextId, TaskId> creatorMap;
    // Map of child Context -> (childId -or- joinId)
    map<ContextId, Task*> joinMap;
    // How many joins are pending for this task, if 0 and not active then clear
    map<TaskId, int> joinCountMap;
    
    // Has this contech started running?
    bool hasStarted = false;
    bool hasExited = false;

    // The absolute time when this contech was created.
    // -For contech 0, this must be 0 (the start of the first contech marks the beginning of absolute time)
    // -For other contechs, this must be set to a non-zero value before any events occur in this contech, so that we can calculate the timeOffset
    ct_tsc_t startTime = 0;

    // The absolute time when this contech ended
    // Really just a storage place so we can capture the end time of join events
    ct_tsc_t endTime = 0;

    // Time offset between absolute time and relative time for this contech
    ct_tsc_t timeOffset = 0;
    
    ct_tsc_t currentTime = 0;
};

} // end namespace contech

#endif
