#include "Context.hpp"

using namespace contech;

// Returns the currently active task
Task* Context::activeTask() { return this->tasks.front(); }

Task* Context::createBasicBlockContinuation()
{
    Task* continuation = new Task(activeTask()->getTaskId().getNext(), task_type_basic_blocks);
    continuation->setStartTime(activeTask()->getEndTime());
    // End time will be set by the next continuation

    // Set as continuation of the active task
    activeTask()->addSuccessor(continuation->getTaskId());
    continuation->addPredecessor(activeTask()->getTaskId());

    // Make the continuation active
    tasks.push_front(continuation);

    return continuation;
}

Task* Context::createContinuation(task_type type, ct_tsc_t startTime, ct_tsc_t endTime)
{
    assert(type != task_type_basic_blocks);
    assert(activeTask()->getType() == task_type_basic_blocks);

    // Set the end time of the previous basic block task
    activeTask()->setEndTime(startTime);

    // Create the continuation task
    Task* continuation = new Task(activeTask()->getTaskId().getNext(), type);
    continuation->setStartTime(startTime);
    continuation->setEndTime(endTime);

    // Set as continuation of the active task
    activeTask()->addSuccessor(continuation->getTaskId());
    continuation->addPredecessor(activeTask()->getTaskId());

    // Make the continuation active
    tasks.push_front(continuation);

    return continuation;
}
