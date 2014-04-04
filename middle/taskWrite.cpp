#include "taskWrite.hpp"
#include "middle.hpp"
#include "../common/taskLib/TaskGraph.hpp"
#include <sys/timeb.h>
#include <map>

using namespace std;
using namespace contech;

//
// Support routine to determine what is the blocking task
// 
//   Provided for being invoked by the debugger.  It scans the
//   blocked tasks in the background writing thread and finds
//   the oldest task.  And then reports what tasks have not been
//   sent to the background, such that this task cannot yet be written.
//
void debugBackground(map<TaskId, pair<Task*, int> > &tasks)
{
    Task* minT = NULL;
    ct_tsc_t minTsc = ~0;
    int predWait = 0;
    
    for (auto it = tasks.begin(), et = tasks.end(); it != et; ++it)
    {
        if (it->second.first->getStartTime() < minTsc)
        {
            minTsc = it->second.first->getStartTime();
            minT = it->second.first;
            predWait = it->second.second;
        }
    }
    
    if (minT != NULL)
    {
        cout << "Minimum Task is: " << endl;
        cout << minT->toString() << endl;
        printf("Waiting on: %d tasks\n", predWait);
        for (TaskId p : minT->getPredecessorTasks())
        {
            if (tasks.find(p) == tasks.end())
            {
                printf("Predecessor: %llx - Not Present\n", p);
            }
            else
            {
                printf("Predecessor: %llx - Present\n", p);
            }
        }
    }
}

void* backgroundTaskWriter(void* v)
{
    ct_file* out = *(ct_file**)v;

    // Put all the tasks in a map so we can look them up by ID
    map<TaskId, pair<Task*, int> > tasks;
    map<TaskId, int> predDelayCount;
    uint64 taskCount = 0, taskWriteCount = 0;
    
    // Write out all tasks in breadth-first order, starting with task 0
    priority_queue<pair<ct_tsc_t, TaskId>, vector<pair<ct_tsc_t, TaskId> >, first_compare > workList;
    
    priority_queue<pair<ct_tsc_t, pair<TaskId, uint64> >, vector<pair<ct_tsc_t, pair<TaskId, uint64> > >, first_compare > taskIndex;
    
    uint64 bytesWritten = 0;
    bool firstTime = true;
    struct timeb timest;
    unsigned int sec = 0, msec = 0, taskLastWriteCount = 0;
    
    //
    // noMoreTasks is a flag from the foreground thread
    //   And if there are no more, then there is the worklist of ready tasks
    //   And finally, there could still be tasks queued from the foreground
    // When all of those are clear, everything has been written.
    //
    while (!noMoreTasks ||
           (!workList.empty() || (taskQueue != NULL && !taskQueue->empty())))
    {
        deque<Task*>* taskChunk = NULL;
        
        //
        // Get tasks from the foreground
        //
        pthread_mutex_lock(&taskQueueLock);
        while (!noMoreTasks && taskQueue->empty())
        {
            pthread_cond_wait(&taskQueueCond, &taskQueueLock);
        }
        if (!noMoreTasks)
        {
            taskChunk = taskQueue;
            taskQueue = new deque<Task*>;
        }
        else
        {
            taskChunk = taskQueue;
            taskQueue = NULL;
        }
        pthread_mutex_unlock(&taskQueueLock);
    
        // Have a chunk of tasks from the foreground
        if (taskChunk != NULL)
        {
            for (Task* t : *taskChunk)
            {
                TaskId tid = t->getTaskId();
                int adjust = 0;
                
                // The predecessor of this task may have already been written out.
                //   If so, then this task does not have to wait on it.
                auto it = predDelayCount.find(tid);
                if (it != predDelayCount.end())
                {
                    adjust = it->second;
                    predDelayCount.erase(it);
                }
                
                // Adjust the count of tasks preceding this one by already written tasks
                adjust = t->getPredecessorTasks().size() - adjust;
                
                // If none, then this task is ready to be written out.
                if (adjust == 0)
                {
                    workList.push(make_pair(t->getStartTime(), tid));
                    tasks[tid] = make_pair(t, 0);
                    firstTime = false;
                    ftime(&timest);
                    sec = timest.time;
                    msec = timest.millitm;
                }
                else
                {
                    tasks[tid] = make_pair(t, adjust);
                }
                taskCount += 1;
            }
            delete taskChunk;
        }
        
        //
        // If this is the first time, make sure that we received task 0:0 
        //
        if (firstTime)
        {
            if (tasks.find(0) == tasks.end()) continue;
            workList.push(make_pair(tasks[0].first->getStartTime(), 0));
            firstTime = false;
        }
        
        // This is for debugging
        bool printHead = false;
        ftime(&timest);
        if ((unsigned int)timest.time > (sec + 1)) printHead = true;
        if (taskWriteCount > taskLastWriteCount)
        {
            sec = timest.time;
            msec = timest.millitm;
        }
        
        //
        // It would be better if we could ensure the broadest set of tasks in the worklist,
        //   so that the oldest BFS task is written.
        //
        if (!noMoreTasks) continue;
        while (!workList.empty())
        {
            ct_timestamp startTime = workList.top().first;
            TaskId id = workList.top().second;
            Task* t = tasks[id].first;
            
            assert(tasks.find(id) != tasks.end());
            
            workList.pop();
            
            // Task will be null if it has already been handled
            if (t != NULL)
            {
                // Have all my predecessors have been written out?
                bool ready = true;

                if (!ready)
                {
                    break;
                }
                else
                {
                    // Write out the task
                    t->setFileOffset(bytesWritten);
                    taskIndex.push(make_pair(startTime, make_pair(id, bytesWritten)));
                    bytesWritten += Task::writeContechTask(*t, out);
                    taskWriteCount += 1;
                    
                    // Add successors to the work list
                    for (TaskId succ : t->getSuccessorTasks())
                    {
                        auto it = tasks.find(succ);
                        
                        // If the successor has not been received, then store that once its
                        //   predecessors has already been written
                        if (it == tasks.end())
                        {
                            predDelayCount[succ] ++;
                            continue;
                        }
                        
                        it->second.second --;
                        if (it->second.second == 0)
                            workList.push(make_pair(it->second.first->getStartTime(), succ));
                    }
                    
                    // Delete the task
                    delete t;
                    //tasks[id].first = NULL;
                    tasks.erase(id);
                }
            }
        }
        taskLastWriteCount = taskWriteCount;
    }
    
    // Write how many entries are in the index
    //   The write each index entry pair
    ct_write(&taskWriteCount, sizeof(taskWriteCount), out);
    //for (auto it = taskIndex.begin(), et = taskIndex.end(); it != et; ++it)
    while (!taskIndex.empty())
    {
        TaskId tid = taskIndex.top().second.first;
        uint64 offset = taskIndex.top().second.second;
        
        ct_write(&tid, sizeof(TaskId), out);
        ct_write(&offset, sizeof(uint64), out);
        
        taskIndex.pop();
    }
    
    // Now write the position of the index
    ct_seek(out, 4);
    ct_write(&bytesWritten, sizeof(bytesWritten), out);
    
    //
    // Stats for the background thread.
    //  TaskCount should equal taskWriteCount
    //  And there should be no tasks remaining.
    //
    printf("Tasks Received: %ld\n", taskCount);
    printf("Tasks Written: %ld\n", taskWriteCount);
    if (taskQueue != NULL)
        printf("Tasks Left: %ld\n", taskQueue->size());
    printf("Tasks Remaining: %u\n", workList.size());
    
    auto it = tasks.begin();
    if (it != tasks.end())
    {
        cout << it->second.first->toString();
        printf("%d - %d\n", it->first, it->second.second);
    }
        
    return NULL;
}