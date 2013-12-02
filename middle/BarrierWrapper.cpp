#include "BarrierWrapper.hpp"

using namespace contech;

BarrierWrapper::BarrierWrapper()
{
    entryCount = 0;
    exitCount = 0;
    entryBarrierTask = NULL;
    exitBarrierTask = NULL;
}

Task* BarrierWrapper::onEnter(Task& arrivingTask, ct_tsc_t arrivalTime)
{
    // If this is the first entry to the barrier, create a new barrier task
    if (entryBarrierTask == NULL)
    {
        entryBarrierTask = new Task(arrivingTask.getTaskId().getNext(), task_type_barrier);
    }

    // Start time of the barrier is the time when the latest task arrived
    entryBarrierTask->setStartTime(std::max((ct_timestamp)arrivalTime, entryBarrierTask->getStartTime()));

    // Set the barrier task as the arriving task's child
    arrivingTask.addSuccessor(entryBarrierTask->getTaskId());
    arrivingTask.setEndTime(arrivalTime);
    // Set the arriving task as a parent of the barrier
    entryBarrierTask->addPredecessor(arrivingTask.getTaskId());

    // Record entry to the barrier
    entryCount++;
    return entryBarrierTask;
}

Task* BarrierWrapper::onExit(ct_tsc_t exitTime)
{
    // If this is the first exit from the barrier, move it to the exit slot to allow threads to arrive at the start again
    if (exitBarrierTask == NULL)
    {
        assert(entryBarrierTask != NULL); assert(entryCount != 0);
        exitBarrierTask = entryBarrierTask;
        exitCount = entryCount;
        // End time of barrier is the time when the first task departs
        exitBarrierTask->setEndTime(exitTime);

        entryBarrierTask = NULL;
        entryCount = 0;
    }

    // Save the barrier task to return to the caller
    Task* temp = exitBarrierTask;
    // Record that this thread arrived
    exitCount--;

    // If this was the last thread to arrive, clear out
    if (exitCount == 0)
    {
        assert(exitBarrierTask != NULL);
        exitBarrierTask = NULL;
    }

    return temp;
}
