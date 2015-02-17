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
    Task* onEnter(Task& arrivingTask, ct_tsc_t arrivalTime, ct_addr_t addr);
    Task* onExit(Task*, ct_tsc_t exitTime, bool*);
private:
    Task* entryBarrierTask;
    list<Task*> exitBarrierTasks;
};

} // end namespace contech

#endif
