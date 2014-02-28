#include "Context.hpp"

using namespace contech;

// Returns the currently active task
Task* Context::activeTask() { return this->tasks.front(); }

//
// Which of this context's tasks created cid
//
TaskId Context::getCreator(ContextId cid)
{
    TaskId tid;
    auto it = creatorMap.find(cid);
    assert(it != creatorMap.end());
    tid = it->second;
    creatorMap.erase(it);
    
    return tid;
}

//
// Remove the specified task from the list.
//
bool Context::removeTask(Task* t)
{
    // Probably better to use rbegin, but erase only takes iterators and not reverse iterators
    for (auto it = tasks.begin(), et = tasks.end(); it != et; ++it)
    {
        if (*it == t) {tasks.erase(it); return true;}
    }
    
    return false;
}

Task* Context::getTask(TaskId tid)
{
    Task* r = NULL;
    
    // Probably better to use rbegin, but erase only takes iterators and not reverse iterators
    for (auto it = tasks.begin(), et = tasks.end(); it != et; ++it)
    {
        r = *it;
        if (r->getTaskId() == tid) {return r;}
    }
    
    return r;
}

Task* Context::childExits(TaskId childId)
{
    ContextId ctid = childId.getContextId();
    auto it = joinMap.find(ctid);
    
    // Parent hasn't created the join task yet, record a 'cookie' for parent
    if (it == joinMap.end())
    {
        //joinMap[ctid] = childId;
        // Parent finds child via context[ctid].activeTask
        return NULL;
    }
    else
    {
        Task* r = it->second;
        joinMap.erase(it);
        joinCountMap[r->getTaskId()] --;
        return r;
    }
}

void Context::getChildJoin(ContextId ctid, Task* tj)
{
    joinMap[ctid] = tj;
    joinCountMap[tj->getTaskId()] ++;
    //return TaskId(0);
    //auto it = joinMap.find(ctid);
    
    // child hasn't exited, record a 'cookie' for the child
    /*if (it == joinMap.end())
    {
        joinMap[ctid] = tid;
        
        joinCountMap[tid] ++;
    
        return TaskId(0);
    }
    else
    {
        TaskId r = it->second;
        joinMap.erase(it);
        return r;
    }*/
}

bool Context::isCompleteJoin(TaskId tid)
{
    auto it = joinCountMap.find(tid);
    
    if (it == joinCountMap.end() ||
        it->second > 0) return false;
        
    joinCountMap.erase(it);
        
    return true;
}

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
    //assert(activeTask()->getType() == task_type_basic_blocks);
    if (activeTask()->getType() != task_type_basic_blocks)
    {
        createBasicBlockContinuation();
    }
    
    assert(startTime >= currentTime &&
           endTime >= startTime);
    currentTime = endTime;
    
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
