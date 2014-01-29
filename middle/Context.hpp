#ifndef CT_CONTECH_HPP
#define CT_CONTECH_HPP

#include <deque>
#include "../common/eventLib/ct_event.h"
#include "../common/taskLib/Task.hpp"

namespace contech {

class Context
{

public:

    Task* activeTask();
    Task* createBasicBlockContinuation();
    Task* createContinuation(task_type eventType, ct_tsc_t startTime, ct_tsc_t endTime);
    bool removeTask(Task*);
    TaskId getCreator(ContextId);

    // Queue of tasks that are running in this contech but have not been written to file yet. These tasks may have incomplete data.
    // The front of the queue represents more recent tasks.
    deque<Task*> tasks;

    // Map of ContextId -> TaskId, which task created which context
    map<ContextId, TaskId> creatorMap;
    
    // Has this contech started running?
    bool hasStarted = false;

    // The absolute time when this contech was created.
    // -For contech 0, this must be 0 (the start of the first contech marks the beginning of absolute time)
    // -For other contechs, this must be set to a non-zero value before any events occur in this contech, so that we can calculate the timeOffset
    ct_tsc_t startTime = 0;

    // The absolute time when this contech ended
    // Really just a storage place so we can capture the end time of join events
    ct_tsc_t endTime = 0;

    // Time offset between absolute time and relative time for this contech
    ct_tsc_t timeOffset = 0;
};

} // end namespace contech

#endif
