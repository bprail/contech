#ifndef CT_BARRIER_WRAPPER_HPP
#define CT_BARRIER_WRAPPER_HPP

#include "../common/taskLib/Task.hpp"
#include "../common/eventLib/ct_event.h"
#include <assert.h>
#include <list>

namespace contech {

class BarrierWrapper
{
public:
    BarrierWrapper();
    Task* onEnter(Task& arrivingTask, ct_tsc_t arrivalTime);
    Task* onExit(ct_tsc_t exitTime);
    bool isFinished();
private:
    unsigned int entryCount;
    unsigned int exitCount;
    Task* entryBarrierTask;
    Task* exitBarrierTask;
    list<Task*> predecessors;
    list<Task*> successors;
};

} // end namespace contech

#endif
