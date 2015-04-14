#include "Context.hpp"

using namespace contech;

Context::Context()
{
    tasks.clear();
}

// Returns the currently active task
Task* Context::activeTask() { return this->tasks.front(); }

//
// Which of this context's tasks created cid
//
TaskId Context::getCreator(ContextId cid)
{
    TaskId tid;
    auto it = creatorMap.find(cid);
    if (it == creatorMap.end())
    {
        fprintf(stderr, "Failed to find creator for %d in %d\n", (unsigned int) cid, (unsigned int)activeTask()->getContextId());
        tid = activeTask()->getTaskId();
        assert(it != creatorMap.end());
    }
    else
    {
        tid = it->second;
        creatorMap.erase(it);
    }
    
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
    
    // Is it safe that r is equal to the last element in the list?
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
}

bool Context::isCompleteJoin(TaskId tid)
{
    auto it = joinCountMap.find(tid);
    
    if (it == joinCountMap.end()) return true;
    if (it->second > 0) return false;
        
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

Task* Context::createContinuation(task_type type, ct_tsc_t tStartTime, ct_tsc_t tEndTime)
{
    assert(type != task_type_basic_blocks);
    //assert(activeTask()->getType() == task_type_basic_blocks);
    if (activeTask()->getType() != task_type_basic_blocks)
    {
        createBasicBlockContinuation();
    }
    
    // This context has not exited
    assert(endTime == 0);
    
    //assert(startTime >= currentTime &&
    //       endTime >= startTime);
    // With OpenMP, the runtime puts in create events on behalf of other contexts
    //   this can result in reversed timestamps.  Stitch them up here.
    if (tStartTime < currentTime)
    {
        ct_tsc_t delta = currentTime - tStartTime;
        tStartTime += delta;
        tEndTime += delta;
    }
    currentTime = tEndTime;
    
    // Set the end time of the previous basic block task
    activeTask()->setEndTime(tStartTime);

    // Create the continuation task
    Task* continuation = new Task(activeTask()->getTaskId().getNext(), type);
    continuation->setStartTime(tStartTime);
    continuation->setEndTime(tEndTime);

    // Set as continuation of the active task
    activeTask()->addSuccessor(continuation->getTaskId());
    continuation->addPredecessor(activeTask()->getTaskId());

    // Make the continuation active
    tasks.push_front(continuation);

    return continuation;
}
