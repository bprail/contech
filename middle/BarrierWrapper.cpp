#include "BarrierWrapper.hpp"

using namespace contech;

BarrierWrapper::BarrierWrapper()
{
    entryCount = 0;
    exitCount = 0;
    entryBarrierTask = NULL;
    exitBarrierTasks.clear();
}

Task* BarrierWrapper::onEnter(Task& arrivingTask, ct_tsc_t arrivalTime, ct_addr_t addr)
{
    // If there are exits, first can the arriving task be part of an exit?
    if (!exitBarrierTasks.empty())
    {
        ContextId cid = arrivingTask.getContextId();
        bool hasExitedBarrier = false;
        Task* ebt = NULL;
        
        // For each exit barrier, is arriving task present?
        for (auto ebtt = exitBarrierTasks.begin(), ebtet = exitBarrierTasks.end(); ebtt != ebtet; ++ebtt)
        {
            ebt = *ebtt;
            auto exitPred = ebt->getPredecessorTasks();
            hasExitedBarrier = false;
            for (auto it = exitPred.begin(), et = exitPred.end(); it != et; ++it)
            {
                if (it->getContextId() == cid)
                {
                    hasExitedBarrier = true;
                    break;
                }
            }
            
            // Arriving task was not present in exit barrier
            if (hasExitedBarrier == false) break;
        }
        
        // If this task's context has not entered the "exit" barrier
        //   attach it to the exit barrier, rather than a new enter barrier
        if (hasExitedBarrier == false)
        {
            arrivingTask.addSuccessor(ebt->getTaskId());
            ebt->addPredecessor(arrivingTask.getTaskId());
            arrivingTask.setEndTime(arrivalTime);
            return ebt;
        }
    }
    
    // If this is the first entry to the barrier, create a new barrier task
    if (entryBarrierTask == NULL)
    {
        entryBarrierTask = new Task(arrivingTask.getTaskId().getNext(), task_type_barrier);
        entryBarrierTask->recordMemOpAction(true, 8, addr);
    }

    // Start time of the barrier is the time when the latest task arrived
    entryBarrierTask->setStartTime(std::max((ct_timestamp)arrivalTime, entryBarrierTask->getStartTime()));

    // Set the barrier task as the arriving task's child
    arrivingTask.addSuccessor(entryBarrierTask->getTaskId());
    arrivingTask.setEndTime(arrivalTime);
    
    // Set the arriving task as a parent of the barrier
    entryBarrierTask->addPredecessor(arrivingTask.getTaskId());

    // Record entry to the barrier
    return entryBarrierTask;
}

Task* BarrierWrapper::onExit(Task* departingTask, ct_tsc_t exitTime, bool* finished)
{
    Task* exitT = NULL;
    
    // Find the oldest exit barrier and add depart to the barrier
    if (!exitBarrierTasks.empty())
    {
        ContextId cid = departingTask->getContextId();
        bool hasArrivedBarrier = false;
        bool hasExitedBarrier = false;
        
        // For each exit barrier, is departing task present?
        for (auto ebtt = exitBarrierTasks.begin(), ebtet = exitBarrierTasks.end(); ebtt != ebtet; ++ebtt)
        {
            exitT = *ebtt;
            auto exitPred = exitT->getPredecessorTasks();
            hasArrivedBarrier = false;
            for (auto it = exitPred.begin(), et = exitPred.end(); it != et; ++it)
            {
                if (it->getContextId() == cid)
                {
                    hasArrivedBarrier = true;
                    break;
                }
            }
            
            // Task has not arrived at this barrier
            if (hasArrivedBarrier == false) continue;
            
            auto exitSucc = exitT->getSuccessorTasks();
            hasExitedBarrier = false;
            for (auto it = exitSucc.begin(), et = exitSucc.end(); it != et; ++it)
            {
                if (it->getContextId() == cid)
                {
                    hasExitedBarrier = true;
                    break;
                }
            }
            
            // Departing task arrived but did not exit barrier
            if (hasExitedBarrier == false) break;
        }
        
        // If this task's context has arrived but not exited the barrier
        //   then this is the exit barrier
        // Otherwise, the entry barrier might be a candidate
        if (hasArrivedBarrier != true ||
            hasExitedBarrier != false)
        {
            exitT = NULL;
        }
    }
    
    if (exitT == NULL)
    {
        // There should be an entry barrier to add to the exit list
        assert(entryBarrierTask != NULL);
        
        auto entryPred = entryBarrierTask->getPredecessorTasks();
        bool hasEntered = false;
        ContextId cid = departingTask->getContextId();
        for (auto it = entryPred.begin(), et = entryPred.end(); it != et; ++it)
        {
            if (it->getContextId() == cid)
            {
                hasEntered = true;
                break;
            }
        }
        assert(hasEntered == true);
        
        // End time of barrier is the time when the first task departs
        entryBarrierTask->setEndTime(exitTime);
        
        exitT = entryBarrierTask;
        
        exitBarrierTasks.push_back(entryBarrierTask);
        
        entryBarrierTask = NULL;
    }
    
    // Record that this thread arrived
    *finished = false;

    // If this was the last thread to arrive, clear out
    if (exitT->getPredecessorTasks().size() == exitT->getSuccessorTasks().size())
    {
        exitBarrierTasks.remove(exitT);
     
        // This barrier has finished, let the caller know
        *finished = true;
    }

    return exitT;
}
