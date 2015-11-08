#include "taskWrite.hpp"
#include "middle.hpp"
#include "../common/taskLib/TaskGraph.hpp"
#include <sys/timeb.h>
#include <sys/sysinfo.h>
#include <map>

using namespace std;
using namespace contech;

bool noMoreTasks = false;
pthread_mutex_t taskQueueLock;
pthread_cond_t taskQueueCond;
deque<Task*>* taskQueue;

TaskId roiStart = 0;
TaskId roiEnd = 0;

void setROIStart(TaskId t)
{
    roiStart = t;
}

void setROIEnd(TaskId t)
{
    roiEnd = t;
}

//
// Queue a task for the background thread to write out
//
//   This releases ownership of the task.  It is expected that there should
//   be no further changes to a task so queued.  It may be deleted at any time
//   by the background thread.
//
#define QUEUE_SIGNAL_THRESHOLD 16
void backgroundQueueTask(Task* t)
{
    unsigned int qSize = 0;
    pthread_mutex_lock(&taskQueueLock);
    qSize = taskQueue->size();
    taskQueue->push_back(t);
    //assert(t->getTaskId() != TaskId(17,3));
    // Signal if there are enough tasks, or a "maximal" sized task is queued
    if (qSize == QUEUE_SIGNAL_THRESHOLD || t->getBBCount() > (MAX_BLOCK_THRESHOLD - 1)) {pthread_cond_signal(&taskQueueCond);}
    pthread_mutex_unlock(&taskQueueLock);
}

//
// Debug routine
//
//   This routine has no call, instead it is invoked in the debugger to display
//   the tasks currently queued at a context.  And to display the details of the
//   oldest task.
//
void displayContextTasks(map<ContextId, Context> &context, int id)
{
    Context tgt = context[id];
    Task* last;
    
    //for (Task* t : tgt.tasks)
    for (auto it : tgt.tasks)
    {
        Task* t = it.second;
        last = t;
        printf("%llx  ", t->getTaskId());
    }
    if (last != NULL)
    {
        cout << last->toString() << endl;
        
        if (last->getType() == task_type_join)
        {
            auto jc = tgt.joinCountMap.find(last->getTaskId());
            if (jc == tgt.joinCountMap.end())
            {
                cout << "Join is not waiting on any tasks, should be queued.\n";
            }
            else
            {
                cout << "Waiting on: " << jc->second << " tasks to join" << endl;
                for (TaskId s : last->getSuccessorTasks())
                {
                    if (tgt.joinMap.find(s.getContextId()) != tgt.joinMap.end())
                    {
                        cout << "\tWaiting on: " << s.toString() << endl;
                    }
                }
            }
        }
    }
}

//
// Debug routine
//
void identifyMaxTaskPerContext(map<ContextId, Context> &context)
{
    for (auto it = context.begin(), et = context.end(); it != et; ++it)
    {
        Context tgt = it->second;
        int countSyn = 0, countBB = 0, countC = 0, countJ = 0, countBar = 0;
        uint64_t maxBBCount = 0;
        Task* maxBBTask = NULL;
        
        //for (Task* t : tgt.tasks)
        for (auto it : tgt.tasks)
        {
            Task* t = it.second;
            switch(t->getType())
            {
                case task_type_basic_blocks:
                {
                    uint64_t bbCount = t->getBBCount();
                    countBB++;
                    if (bbCount > maxBBCount)
                    {
                        maxBBCount = bbCount;
                        maxBBTask = t;
                    }
                }
                break;
                case task_type_create:
                {
                    countC++;
                }
                break;
                case task_type_join:
                {
                    countJ++;
                }
                break;
                case task_type_sync:
                {
                    countSyn++;
                }
                break;
                case task_type_barrier:
                {
                    countBar++;
                }
                break;
            }
        }
        cout << maxBBTask->getTaskId().toString() << " - " << maxBBCount << endl;
        cout << it->first << " C: " << countC << " J: " << countJ << " S: " << countSyn;
        cout << " B: " << countBar << " BB: " << countBB << endl;
    }
}

void updateContextTaskList(Context &c)
{
    // Non basic block tasks are handled by their creation logic
    //   basic block tasks can be queued, if they are not the newest task
    //   and they have a predecessor (i.e., have been created by another context).
    //   Task(0:0) has no predecessor and is a special case here.
    //
    // Creates must be complete when they are not the active task.  The child is
    //   known at creation time, so no update is required of this task.
    
    bool exited = (c.endTime != 0);
    
    return;
#if 0    
    // TODO: Should we 'cache' getType() or can the compiler do this?
    //   task_type tType ...
    
    if (exited == false)
    {
        for (auto it = c.tasks.begin(), et = c.tasks.end(); it != et; ++it)
        {
            Task* t = *it;
            if (t == c.activeTask()) continue;
            if (( t->getType() == task_type_basic_blocks &&
                (t->getPredecessorTasks().size() > 0 || t->getTaskId() == TaskId(0))) ||
               (t->getType() == task_type_create) ||
               (t->getType() == task_type_join && c.isCompleteJoin(t->getTaskId())))
            {
                // This is not guaranteed to remove every task in one pass;
                //   however, the routine will be invoked many times and it
                //   will correctly remove tasks.
                it = c.tasks.erase(it);
                backgroundQueueTask(t);
            }
        }
        
        /*while (t != c.activeTask() &&
               (( t->getType() == task_type_basic_blocks &&
                (t->getPredecessorTasks().size() > 0 || t->getTaskId() == TaskId(0))) ||
               (t->getType() == task_type_create) ||
               (t->getType() == task_type_join && c.isCompleteJoin(t->getTaskId()))))
              
        {
            c.tasks.pop_back();
            backgroundQueueTask(t);
            t = c.tasks.back();
        }*/
    }
    else
    {
        // If the context has exited, then even the active task can go
        if (c.tasks.empty()) return;
        Task* t = c.tasks.back();
        while (( t->getType() == task_type_basic_blocks &&
               (t->getPredecessorTasks().size() > 0 || t->getTaskId() == TaskId(0)) &&
               (t->getSuccessorTasks().size() > 0)) ||
               t->getType() == task_type_create)
        {
            c.tasks.pop_back();
            backgroundQueueTask(t);
            if (c.tasks.empty()) return;
            t = c.tasks.back();   
        }
    }
    #endif
}

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
        cout << "Minimum Task is: ";
        cout << minT->getTaskId().toString() << " of type - ";
        cout << minT->getType() << endl;
        printf("Waiting on: %d tasks\n", predWait);
        for (TaskId p : minT->getPredecessorTasks())
        {
            if (tasks.find(p) == tasks.end())
            {
                printf("Predecessor: %s - Not Present\n", p.toString().c_str());
            }
            else
            {
                printf("Predecessor: %s - Present\n", p.toString().c_str());
            }
        }
    }
}

unsigned long long getCurrentFreeMemory()
{
    struct sysinfo t_info;
    unsigned long long mem_size = 0;
            
    if (0 == sysinfo(&t_info))
    {
        mem_size = (unsigned long long)t_info.freeram * (unsigned long long)t_info.mem_unit;
        return mem_size;
    }
    
    return 0;
}

struct TaskWrapper
{
    TaskId      self;   // probably redundant in a map
    int         p;      // number of predecessor tasks remaining
    vector<TaskId> s;   // tasks that follow this task
    ct_tsc_t    start;  // start time for this task
    long        writePos; // where was the task written
    task_type   t;
};

void findPredInTaskMap(map<TaskId, TaskWrapper> &writeTaskMap, int c, int s)
{
    TaskId tid = TaskId(c, s);
    
    for (auto it = writeTaskMap.begin(), et = writeTaskMap.end(); it != et; ++it)
    {
        for (TaskId succ : it->second.s)
        {
            if (succ == tid)
            {
                printf("Found in %u:%u that is at %u\n", it->first.getContextId(),
                                                         it->first.getSeqId(),
                                                         it->second.p);
                return;
            }
        }
    }
}

void* backgroundTaskWriter(void* v)
{
    ct_file* out = *(ct_file**)v;

    deque<Task*> writeTaskQueue;
    map<TaskId, TaskWrapper> writeTaskMap;
    uint64 taskCount = 0, taskWriteCount = 0;
    
    uint64 bytesWritten = ct_tell(out);
    long pos;
    bool firstTime = true;
    unsigned int sec = 0, msec = 0, taskLastWriteCount = 0;
    
    //
    // noMoreTasks is a flag from the foreground thread
    //   And if there are no more, then there is the worklist of ready tasks
    //   And finally, there could still be tasks queued from the foreground
    // When all of those are clear, everything has been written.
    //
    while (!noMoreTasks ||
           (!writeTaskQueue.empty() || (taskQueue != NULL && !taskQueue->empty())))
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
            {
                struct timeb tp;
                ftime(&tp);
                printf("MIDDLE_DEQUE: %d.%03d\t%u\n", (unsigned int)tp.time, tp.millitm, taskWriteCount);
            }
        }
        pthread_mutex_unlock(&taskQueueLock);
    
        // Have a chunk of tasks from the foreground
        if (taskChunk != NULL)
        {
            writeTaskQueue.insert(writeTaskQueue.end(), taskChunk->begin(), taskChunk->end());
            taskCount += taskChunk->size();
            delete taskChunk;
        }
        
        
        while (!writeTaskQueue.empty())
        {
            Task* t = writeTaskQueue.front();
            TaskId id = t->getTaskId();
            
            writeTaskQueue.pop_front();
            
            // Task will be null if it has already been handled
            assert(t != NULL);
            // Write out the task
            pos = ct_tell(out);
            
            // TaskIndex is a graph, then use the graph to
            //   determine the bfs order, this way tasks can be written out
            //   immediately
            {
                TaskWrapper tw;
                
                tw.self = id;
                tw.start = t->getStartTime();
                tw.p = t->getPredecessorTasks().size();
                tw.s = t->getSuccessorTasks();
                tw.t = t->getType();
                tw.writePos = pos;
                assert(writeTaskMap.find(id) == writeTaskMap.end());
                writeTaskMap[id] = tw;
                
                /* Debugging code for bug where TaskId(x:0) had multiple predecessors
                if (id.getSeqId() == 0)
                {
                    printf("%u:%u -- ", id.getContextId(), id.getSeqId());
                    auto pr = t->getPredecessorTasks();
                    for (auto it = pr.begin(), et = pr.end(); it != et; ++it)
                    {
                        printf("%s\t", it->toString().c_str());
                    }
                    printf("\n");
                }*/
                //printf("%s", t->toSummaryString().c_str());
            }
            
            bytesWritten += Task::writeContechTask(*t, out);
            taskWriteCount += 1;
                
            // Delete the task
            delete t;
        }
        taskLastWriteCount = taskWriteCount;
    }
    
    // Write how many entries are in the index
    //   The write each index entry pair
    pos = ct_tell(out);
    if (pos == -1)
    {
        //int esav = errno;
        perror("Cannot identify index position");
    }
    {
        struct timeb tp;
        ftime(&tp);
        printf("MIDDLE_TASK: %d.%03d\n", (unsigned int)tp.time, tp.millitm);
    }
    printf("Writing index for %d at %lld\n", taskWriteCount, pos);
    size_t t = ct_write(&taskWriteCount, sizeof(taskWriteCount), out);
    
    priority_queue<pair<ct_tsc_t, pair<TaskId, uint64> >, vector<pair<ct_tsc_t, pair<TaskId, uint64> > >, first_compare > taskSort;
    
    {
        TaskWrapper tw = writeTaskMap.find(0)->second;
        taskSort.push(make_pair(tw.start, make_pair(tw.self, tw.writePos)));
    }
    
    //
    // This reproduces the BFS algorithm that had been used for writing tasks
    //   It is much faster to sort the tasks on just the graph information than
    //   to indefinitely delay writing a task until the entire graph is available.
    //
    // taskSort is a priority queue, the top element is the oldest task that has all
    //   its prior tasks in the index.
    //
    unsigned int indexWriteCount = 0;
    TaskId lastTid = 0;
    while (!taskSort.empty())
    {
        TaskId tid = taskSort.top().second.first;
        uint64 offset = taskSort.top().second.second;
        
        lastTid = tid;
        
        ct_write(&tid, sizeof(TaskId), out);
        ct_write(&offset, sizeof(uint64), out);
        //printf("%d:%d @ %llx\t", tid.getContextId(), tid.getSeqId(), offset);
        
        taskSort.pop();
        
        auto twit = writeTaskMap.find(tid);
        assert(twit != writeTaskMap.end());
        TaskWrapper tw = twit->second;
        
        assert(twit->first == tw.self);
        
        for (TaskId succ : tw.s)
        {
            TaskWrapper &suTW = writeTaskMap.find(succ)->second;
            
            //printf("%d:%d (%d)\t", succ.getContextId(), succ.getSeqId(), suTW.p);
            
            suTW.p--;
            if (suTW.p == 0)
            {
                taskSort.push(make_pair(suTW.start, make_pair(suTW.self, suTW.writePos)));
            }
        }
        //printf("\n");
        
        //  Can erase tid, but we don't need the memory, will it speed up?
        writeTaskMap.erase(twit);
        indexWriteCount ++;
    }
    printf("Wrote %u tasks to index\n", indexWriteCount);
    
    if (indexWriteCount != taskWriteCount)
    {
        for (auto it = writeTaskMap.begin(), et = writeTaskMap.end(); it != et; ++it)
        {
            printf("%s (type:%d) (pred:%d)\t", it->first.toString().c_str(), it->second.t, it->second.p);
            for (TaskId succ : it->second.s)
            {
                printf("%s\t", succ.toString().c_str());
            }
            printf("\n");
        }
    }
    
    // Failing this assert indicates that the graph either has cycles or is disjoint
    //   Both case are bad
    assert(indexWriteCount == taskWriteCount);
    
    // Now write the position of the index
    ct_seek(out, 4);
    
    // With ftell, we use long, rather than the uint64 type which we track positions
    ct_write(&pos, sizeof(pos), out);
    
    // Write the ROI start and end after index
    ct_write(&roiStart, sizeof(TaskId), out);
    if (roiEnd == 0)
    {
        roiEnd = lastTid;
    }
    ct_write(&roiEnd, sizeof(TaskId), out);
    
    //
    // Stats for the background thread.
    //  TaskCount should equal taskWriteCount
    //  And there should be no tasks remaining.
    //
    printf("Tasks Received: %ld\n", taskCount);
    printf("Tasks Written: %ld\n", taskWriteCount);
    if (taskQueue != NULL)
        printf("Tasks Left: %ld\n", taskQueue->size());
    printf("Tasks Remaining: %u\n", writeTaskQueue.size());
        
    return NULL;
}