#include "middle.hpp"
#include <sys/timeb.h>
#include <pthread.h>

using namespace std;
using namespace contech;

unsigned int currentQueuedCount = 0;
unsigned int maxQueuedCount = 0;

bool noMoreTasks = false;
pthread_mutex_t taskQueueLock;
pthread_cond_t taskQueueCond;
deque<Task*>* taskQueue;

void* backgroundTaskWriter(void*);

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
    unsigned int qSize;
    pthread_mutex_lock(&taskQueueLock);
    qSize = taskQueue->size();
    taskQueue->push_back(t);
    if (qSize == QUEUE_SIGNAL_THRESHOLD) {pthread_cond_signal(&taskQueueCond);}
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
    
    for (Task* t : tgt.tasks)
    {
        last = t;
        printf("%llx  ", t->getTaskId());
    }
    if (last != NULL)
    {
        cout << last->toString() << endl;
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
    Task* t = c.tasks.back();
    bool exited = (c.endTime != 0);
    
    // TODO: Should we 'cache' getType() or can the compiler do this?
    //   task_type tType ...
    
    if (exited == false)
    {
        while (t != c.activeTask() &&
               (( t->getType() == task_type_basic_blocks &&
               (t->getPredecessorTasks().size() > 0 || t->getTaskId() == TaskId(0))) ||
               (t->getType() == task_type_create)))
              
        {
            c.tasks.pop_back();
            backgroundQueueTask(t);
            t = c.tasks.back();
        }
    }
    else
    {
        // If the context has exited, then even the active task can go
        if (c.tasks.empty()) return;
        while (( t->getType() == task_type_basic_blocks &&
               (t->getPredecessorTasks().size() > 0 || t->getTaskId() == TaskId(0)) &&
               (t->getSuccessorTasks().size() > 0)) ||
               t->getType() == task_type_create)
              
        {
            c.tasks.pop_back();
            backgroundQueueTask(t);
            t = c.tasks.back();
            if (c.tasks.empty()) return;
        }
    }
}

int main(int argc, char* argv[])
{
    // Open input file
    // Use command line argument or stdin
    ct_file* in;
    bool parallelMiddle = false;
    pthread_t backgroundT;
    
    // First attempt middle layer in parallel, if there is an error,
    //   then restart in serial mode.
    //   TODO: Implement restart / reset, or a flag for running serially.
reset_middle:
    if (argc > 1)
    {
        //in = fopen(argv[1], "rb");
        in = create_ct_file_r(argv[1]);
        assert(in != NULL && "Could not open input file");
    }
    else
    {
        //in = stdin;
        in = create_ct_file_from_handle(stdin);
    }
    
    // Open output file
    // Use command line argument or stdout
    //FILE* out;
    ct_file* out;
    if (argc > 2)
    {
        //out = fopen(argv[2], "wb");
        out = create_ct_file_w(argv[2],true);
        assert(out != NULL && "Could not open output file");
    }
    else
    {
        out = create_ct_file_from_handle(stdout);
    }
    
    pthread_mutex_init(&taskQueueLock, NULL);
    pthread_cond_init(&taskQueueCond, NULL);
    taskQueue = new deque<Task*>;
    int r = pthread_create(&backgroundT, NULL, backgroundTaskWriter, &out);
    if (r != 0) parallelMiddle = false;
    
    // Print debug statements?
    bool DEBUG = false;
    if (argc > 3)
    {
        if (!strcmp(argv[3], "-d")) DEBUG = true;
        printf("Debug mode enabled.\n");
    }
    
    // Track the owners of sync primitives
    map<ct_addr_t, Task*> ownerList;
    
    // Track the barrier task for each address
    map<ct_addr_t, BarrierWrapper> barrierList;

    // Declare each context
    map<ContextId, Context> context;

    // Context 0 is special, since it is uncreated
    context[0].hasStarted = true;
    context[0].tasks.push_front(new Task(0, task_type_basic_blocks));

    // Count the number of events processed
    uint64 eventCount = 0;

    {
        struct timeb tp;
        ftime(&tp);
        printf("MIDDLE_START: %d.%03d\n", (unsigned int)tp.time, tp.millitm);
    }
    
    // Scan through the file for the first real event
    bool seenFirstEvent = false;
    while (ct_event* event = createContechEvent(in))
    {
        if (event->contech_id == 0
        &&  event->event_type == ct_event_task_create
        &&  event->tc.other_id == 0)
        {
            // Use the timestamp of the first event in context 0 as t=0 in absolute time
            context[0].timeOffset = event->tc.start_time;
            if (DEBUG) printf("Global offset is %llu\n", context[0].timeOffset);
            seenFirstEvent = true;
        }
        deleteContechEvent(event);
        if (seenFirstEvent) break;
    }
    assert(seenFirstEvent);

    // Main loop: Process the events from the file in order
    while (ct_event* event = getNextContechEvent(in))
    {
        ++eventCount;

        // Whitelist of event types that we handle. Others are informational/debug info and can be skipped
        // Be sure to add event types to this list if you handle new ones
        switch (event->event_type)
        {
            case ct_event_task_create:
            case ct_event_sync:
            case ct_event_barrier:
            case ct_event_task_join:
            case ct_event_basic_block:
            case ct_event_memory:
                break;
            default:
                deleteContechEvent(event);
                continue;
        }

        // New context ids should only appear on task create events
        // Seeing an invalid context id is a good sign that the trace is corrupt
        if (event->event_type != ct_event_task_create && !context.count(event->contech_id))
        {
            cerr << "ERROR: Saw an event from a new context before seeing a create event for that context." << endl;
            cerr << "Either the trace is corrupt or the trace file is missing a create event for the new context. " << endl;
            displayContechEventDebugInfo();
            exit(1);
        }
        
        // The context in which this event occurred
        Context& activeContech = context[event->contech_id];

        // Coalesce start/end times into a single field
        ct_tsc_t startTime, endTime;
        bool hasTime = true;
        switch (event->event_type)
        {
            case ct_event_sync:
                startTime = event->sy.start_time;
                endTime = event->sy.end_time;
                break;
            case ct_event_barrier:
                startTime = event->bar.start_time;
                endTime = event->bar.end_time;
                break;
            case ct_event_task_create:
                startTime = event->tc.start_time;
                endTime = event->tc.end_time;
                break;
            case ct_event_task_join:
                startTime = event->tj.start_time;
                endTime = event->tj.end_time;
                break;
            default:
                hasTime = false;
                break;
        }
        
        // Apply timestamp offsets
        if (hasTime)
        {
            // Note that this does nothing if the context has just started. In this case we apply the offset right after it is calculated, later
            startTime = startTime - activeContech.timeOffset;
            endTime = endTime - activeContech.timeOffset;
        }
        
        // Basic blocks: Record basic block ID and memOp's
        if (event->event_type == ct_event_basic_block)
        {
            Task* activeT = activeContech.activeTask();
            //
            // If transitioning into a basic block task, perhaps the older tasks
            //   are complete and can be queued to the background thread.
            //
            if (activeT->getType() != task_type_basic_blocks &&
                parallelMiddle)
            {
                activeContech.createBasicBlockContinuation();
                
                // Is the current active task a complete join?
                if (activeT->getType() == task_type_join &&
                    activeContech.isCompleteJoin(activeT->getTaskId()))
                {
                    activeContech.removeTask(activeT);
                    backgroundQueueTask(activeT);
                }
                
                updateContextTaskList(activeContech);
                
                activeT = activeContech.activeTask();
            }
            
            // Record that this task executed this basic block
            activeT->recordBasicBlockAction(event->bb.basic_block_id);

            // Examine memory operations
            for (uint i = 0; i < event->bb.len; i++)
            {
                ct_memory_op memOp = event->bb.mem_op_array[i];
                activeT->recordMemOpAction(memOp.is_write, memOp.pow_size, memOp.addr);
            }
        }

        // Task create: Create and initialize child task/context
        else if (event->event_type == ct_event_task_create)
        {
            // Approx skew is defined as 0 for the creator context
            if (event->tc.approx_skew == 0)
            {
                // Assign an ID for the new context
                TaskId childTaskId(event->tc.other_id, 0);

                // Make a "task create" task
                //   N.B. This create task may be a combination of several create events.
                Task* taskCreate;
                if (activeContech.activeTask()->getType() != task_type_create)
                {
                    taskCreate = activeContech.createContinuation(task_type_create, startTime, endTime);
                }
                else
                {
                    taskCreate = activeContech.activeTask();
                    if (taskCreate->getStartTime() > startTime) taskCreate->setStartTime(startTime);
                    if (taskCreate->getEndTime() < endTime) taskCreate->setEndTime(endTime);
                }

                // Assign the new task as a child
                taskCreate->addSuccessor(childTaskId);
                
                // Add the information so that the created task knows its creator.
                if (context[event->tc.other_id].hasStarted == true)
                {
                    Task* childTask = context[event->tc.other_id].getTask(childTaskId);
                    assert (childTask != NULL);
                    childTask->addPredecessor(taskCreate->getTaskId());
                }
                else
                {
                    activeContech.creatorMap[event->tc.other_id] = taskCreate->getTaskId();
                }
                
                if (DEBUG) eventDebugPrint(activeContech.activeTask()->getTaskId(), "created", childTaskId, startTime, endTime);
            
            // If this context was not already running, then it was just created
            } else {
                TaskId newContechTaskId(event->contech_id, 0);

                // Compute time offset for new context
                activeContech.timeOffset = context[event->tc.other_id].timeOffset + event->tc.approx_skew;
                startTime = startTime - activeContech.timeOffset;
                endTime = endTime - activeContech.timeOffset;

                // Start the first task for the new context
                activeContech.hasStarted = true;
                activeContech.tasks.push_front(new Task(newContechTaskId, task_type_basic_blocks));
                activeContech.activeTask()->setStartTime(endTime);

                // Record parent of this task
                if (context[event->tc.other_id].hasStarted == true)
                {
                    TaskId creatorId = context[event->tc.other_id].getCreator(event->contech_id);
                    if (creatorId != TaskId(0))
                    {
                        activeContech.activeTask()->addPredecessor(creatorId);
                    }
                }
                
                if (DEBUG) eventDebugPrint(activeContech.activeTask()->getTaskId(), "started by", TaskId(event->tc.other_id,0), startTime, endTime);
                if (DEBUG) cerr << activeContech.activeTask()->getContextId() << ": skew = " << event->tc.approx_skew << endl;
            }
        }
        
        // Sync events
        else if (event->event_type == ct_event_sync)
        {
            // Create a sync task
            Task* sync = activeContech.createContinuation(task_type_sync, startTime, endTime);
            
            // Record the address in this sync task as an action
            activeContech.activeTask()->recordMemOpAction(true, 8, event->sy.sync_addr);

            // Create a continuation
            activeContech.createBasicBlockContinuation();
            
            // Make the sync dependent on whoever accessed the sync primitive last         
            auto it = ownerList.find(event->sy.sync_addr);
            if (it != ownerList.end() &&
                event->sy.sync_type != ct_cond_wait&&
                parallelMiddle)
            {
                Task* owner = it->second;
                ContextId cid = owner->getContextId();
                owner->addSuccessor(sync->getTaskId());
                sync->addPredecessor(owner->getTaskId());
                
                // Owner can now be background queued
                //  N.B. owner cannot be the active context
                bool wasRem = context[cid].removeTask(owner);
                assert(wasRem == true);
                backgroundQueueTask(owner);
                
                updateContextTaskList(context[cid]);
            }

            // Make the sync task the new owner of the sync primitive
            if (event->sy.sync_type != ct_cond_wait) 
            {
                ownerList[event->sy.sync_addr] = sync;
            }
                
            if (event->sy.sync_type == ct_sync_release ||
                event->sy.sync_type == ct_sync_acquire)
                sync->setSyncType(sync_type_lock);
            else if (event->sy.sync_type == ct_cond_wait ||
                event->sy.sync_type == ct_cond_sig)
                sync->setSyncType(sync_type_condition_variable);
            else
                sync->setSyncType(sync_type_user_defined);
        }

        // Task joins
        else if (event->event_type == ct_event_task_join)
        {
            //Task& otherTask = *context[event->tj.other_id].activeTask();

            // I exited
            if (event->tj.isExit)
            {
                Task* otherTask = NULL;
                TaskId myId = activeContech.activeTask()->getTaskId();
                Context otherContext = context[event->tj.other_id];
                
                activeContech.activeTask()->setEndTime(startTime);
                activeContech.endTime = startTime;
                
                otherTask = otherContext.childExits(myId);
                if (otherTask != NULL)
                {
                    TaskId otherTaskId = otherTask->getTaskId();
                    activeContech.activeTask()->addSuccessor(otherTaskId);
                    otherTask->addPredecessor(myId);
                    
                    // Is otherTaskId complete?
                    //   If it is active, then it may still have further joins to merge
                    //      and recording its successor(s)
                    //   If it is not active, then only joins remain to update this task
                    if (otherContext.activeTask() != otherTask &&
                        otherContext.isCompleteJoin(otherTaskId)&&
                        parallelMiddle)
                    {
                        bool rem = otherContext.removeTask(otherTask);
                        
                        assert(rem == true);
                        
                        backgroundQueueTask(otherTask);
                    }
                }
                
                if (DEBUG) eventDebugPrint(activeContech.activeTask()->getTaskId(), "exited", otherContext.activeTask()->getTaskId(), startTime, endTime);

            // I joined with another task
            } 
            else 
            {
                Context otherContext = context[event->tj.other_id];
                if (otherContext.endTime != 0)
                {
                    // Create a join task
                    Task* taskJoin;
                    Task* otherTask = otherContext.activeTask();
                    
                    if (activeContech.activeTask()->getType() != task_type_join)
                    {
                        taskJoin = activeContech.createContinuation(task_type_join, startTime, endTime);
                    }
                    else
                    {
                        taskJoin = activeContech.activeTask();
                        if (taskJoin->getStartTime() > startTime) taskJoin->setStartTime(startTime);
                        if (taskJoin->getEndTime() < endTime) taskJoin->setEndTime(endTime);
                    }
                    // Set the other task's continuation to the join
                    otherTask->addSuccessor(taskJoin->getTaskId());
                    taskJoin->addPredecessor(otherTask->getTaskId());
                    
                    // The join task starts when both tasks have executed the join, and ends when the parent finishes the join
                    if (DEBUG) eventDebugPrint(activeContech.activeTask()->getTaskId(), "joined with", otherTask->getTaskId(), startTime, endTime);
                }
                else
                {
                    // Child has not exited yet
                    
                    // Create a join task
                    Task* taskJoin;
                    if (activeContech.activeTask()->getType() != task_type_join)
                    {
                        taskJoin = activeContech.createContinuation(task_type_join, startTime, endTime);
                    }
                    else
                    {
                        taskJoin = activeContech.activeTask();
                        if (taskJoin->getStartTime() > startTime) taskJoin->setStartTime(startTime);
                        if (taskJoin->getEndTime() < endTime) taskJoin->setEndTime(endTime);
                    }
                    activeContech.getChildJoin(ContextId(event->tj.other_id), taskJoin);
                }
            }
        }

        // Barriers
        else if (event->event_type == ct_event_barrier)
        {
            // Entering a barrier
            if (event->bar.onEnter)
            {
                // Look up the barrier task for this barrier and record arrival
                Task* barrierTask = barrierList[event->bar.sync_addr].onEnter(*activeContech.activeTask(), startTime);
                if (DEBUG) eventDebugPrint(activeContech.activeTask()->getTaskId(), "arrived at barrier", barrierTask->getTaskId(), startTime, endTime);
            }

            // Leaving a barrier
            else
            {
                // Record my exit from the barrier, and get the associated barrier task
                bool isFinished = false;
                Task* barrierTask = barrierList[event->bar.sync_addr].onExit(endTime, &isFinished);
                if (DEBUG) eventDebugPrint(activeContech.activeTask()->getTaskId(), "leaving barrier", barrierTask->getTaskId(), startTime, endTime);

                // If I own the barrier, my continuation's ID has to come after it. Otherwise just use the next ID.
                bool myBarrier = barrierTask->getContextId() == event->contech_id;

                Task* continuation;
                if (myBarrier)
                {
                    // Create the continuation task (this is a special case, since this is a continuation of the barrier, which is not the active task)
                    // TODO This copies a lot of code from Context::createBasicBlockContinuation()
                    continuation = new Task(barrierTask->getTaskId().getNext(), task_type_basic_blocks);
                    activeContech.activeTask()->addSuccessor(continuation->getTaskId());
                    continuation->addPredecessor(activeContech.activeTask()->getTaskId());
                    // Barrier owner is responsible for making sure the barrier task gets added to the output file
                    activeContech.tasks.push_front(barrierTask);
                    // Make it the active task for this context
                    activeContech.tasks.push_front(continuation);
                }
                else
                {
                    continuation = activeContech.createBasicBlockContinuation();
                }

                // Set continuation as successor of the barrier
                continuation->setStartTime(endTime);
                barrierTask->addSuccessor(continuation->getTaskId());
                continuation->addPredecessor(barrierTask->getTaskId());
                
                // The last of the contexts has exited the barrier, so the barrier is complete
                if (isFinished && parallelMiddle)
                {
                    ContextId cid = barrierTask->getContextId();
                    bool remove = context[cid].removeTask(barrierTask);
                    assert(remove);
                    
                    backgroundQueueTask(barrierTask);
                    
                    updateContextTaskList(context[cid]);
                }
            }
        }

        // Memory allocations
        else if (event->event_type == ct_event_memory)
        {
            if (event->mem.isAllocate)
            {
                activeContech.activeTask()->recordMallocAction(event->mem.alloc_addr, event->mem.size);
            } else {
                activeContech.activeTask()->recordFreeAction(event->mem.alloc_addr);
            }

        } // End switch block on event type

        // Free memory for the processed event
        deleteContechEvent(event);
    }
    close_ct_file(in);
    //displayContechEventDiagInfo();

    if (DEBUG) printf("Processed %llu events.\n", eventCount);
    // Write out all tasks that are ready to be written
    
    if (parallelMiddle == false)
    {
        
        // Put all the tasks in a map so we can look them up by ID
        map<TaskId, pair<Task*, int> > tasks;
        uint64 taskCount = 0;
        for (auto& p : context)
        {
            Context& c = p.second;
            for (Task* t : c.tasks)
            {
                tasks[t->getTaskId()] = make_pair(t, t->getPredecessorTasks().size());
                taskCount += 1;
            }
        }

        {
            struct timeb tp;
            ftime(&tp);
            printf("MIDDLE_WRITE: %d.%03d\n", (unsigned int)tp.time, tp.millitm);
        }
        
        // Write out all tasks in breadth-first order, starting with task 0
        priority_queue<pair<ct_tsc_t, TaskId>, vector<pair<ct_tsc_t, TaskId> >, first_compare > workList;
        workList.push(make_pair(tasks[0].first->getStartTime(), 0));
        uint64 bytesWritten = 0;
        while (!workList.empty())
        {
            TaskId id = workList.top().second;
            Task* t = tasks[id].first;
            workList.pop();
            // Task will be null if it has already been handled
            if (t != NULL)
            {
                // Have all my predecessors have been written out?
                bool ready = true;

                if (!ready)
                {
                    // Push to the back of the list
                    assert(0);
                }
                else
                {
                    // Write out the task
                    t->setFileOffset(bytesWritten);
                    bytesWritten += Task::writeContechTask(*t, out);
                    
                    // Add successors to the work list
                    for (TaskId succ : t->getSuccessorTasks())
                    {
                        tasks[succ].second --;
                        if (tasks[succ].second == 0)
                            workList.push(make_pair(tasks[succ].first->getStartTime(), succ));
                    }
                    
                    // Delete the task
                    delete t;
                    tasks[id].first = NULL;
                }
            }
        }

        if (DEBUG) printf("Wrote %llu tasks to file.\n", taskCount);
    }
    else
    {
        char* d = NULL;
        
        for (auto& p : context)
        {
            Context& c = p.second;
            for (Task* t : c.tasks)
            {
                backgroundQueueTask(t);
            }
        }
        
        pthread_mutex_lock(&taskQueueLock);
        noMoreTasks = true;
        pthread_cond_signal(&taskQueueCond);
        pthread_mutex_unlock(&taskQueueLock);
        
        pthread_join(backgroundT, (void**) &d);
    }

    printf("Max Queued Event Count: %u\n", maxQueuedCount);
    
    {
        struct timeb tp;
        ftime(&tp);
        printf("MIDDLE_END: %d.%03d\n", (unsigned int)tp.time, tp.millitm);
    }
    
    close_ct_file(out);
    
    return 0;
}

void eventDebugPrint(TaskId first, string verb, TaskId second, ct_tsc_t start, ct_tsc_t end)
{
    cerr << start << " - " << end << ": ";
    cerr << first << " " << verb << " " << second << endl;
}

unsigned long long ticketNum = 0;
unsigned long long minQueuedTicket = 0;
bool resetMinTicket = false;
map <unsigned int, deque <pct_event> > queuedEvents;
map <unsigned int, deque <pct_event> >::iterator eventQueueCurrent;
pct_event getNextContechEvent(ct_file* inFile)
{
    bool nextEvent = false;
    pct_event event = NULL;
    
    //
    // This loop checks if any of the queues of events can provide the next event.
    //   Each queue is either blocked on a ticketed event, or is unblocked.
    //
    unsigned long long currMinTicket = ~0;
    while (!queuedEvents.empty())
    {
        // Fast check whether a queued event may be removed.
        if (ticketNum < minQueuedTicket) break;
        if (eventQueueCurrent->second.empty())
        {
            auto t = eventQueueCurrent;
            ++eventQueueCurrent;
            queuedEvents.erase(t);
            // While this loops, the main loop guarentees that there will be at least one
            //   queue with events.
            if (eventQueueCurrent == queuedEvents.end())
            {
                eventQueueCurrent = queuedEvents.begin();
            }
            
            continue;
        }
        event = eventQueueCurrent->second.front();
        //
        // Currently, only syncs are blocking in the event queues, so any other type of
        //   event is clear to be returned.  If the event is a sync, then it is only
        //   clear when it is the next ticket number.
        //
        if (event->event_type != ct_event_sync)
        {
            eventQueueCurrent->second.pop_front();
            currentQueuedCount--;
            return event;
        }
        else if (event->sy.ticketNum == ticketNum)
        {
            // This is the next ticket
            ticketNum++;
            eventQueueCurrent->second.pop_front();
            eventQueueCurrent = queuedEvents.begin();
            currentQueuedCount--;
            return event;
        }
        else
        {
            // No valid events at this queue position
            ++eventQueueCurrent;
            
            // Is this the lowest ticket we've seen so far
            if (event->sy.ticketNum < currMinTicket) currMinTicket = event->sy.ticketNum;
            
            // End of the queue, next request should start over
            if (eventQueueCurrent == queuedEvents.end())
            {
                // If true, then this was likely a single loop through each queue
                //   to find the new minimum ticket number.
                if (resetMinTicket == true)
                {
                    resetMinTicket = false;
                    minQueuedTicket = currMinTicket;
                }
                else
                {
                    resetMinTicket = true;
                    minQueuedTicket = 0;
                }
                eventQueueCurrent = queuedEvents.begin();
                break;
            }
        }
    }
    
    //
    // Get events from the file
    //
    //   Look for one that is not blocked.
    //
    while (!nextEvent)
    {
        event = createContechEvent(inFile);
        if (event == NULL) return NULL;
        if (queuedEvents.find(event->contech_id) != queuedEvents.end())
        {
            queuedEvents[event->contech_id].push_back(event);
            currentQueuedCount++;
            if (currentQueuedCount > maxQueuedCount) maxQueuedCount = currentQueuedCount;
            continue;
        }
        nextEvent = true;
    }
    
    // Leave the switch in case other types need to be checked in the future
    //   We assume that the compiler can convert this into an if / else
    switch (event->event_type)
    {
        case ct_event_sync:
        {
            if (event->sy.ticketNum > ticketNum)
            {
                //printf("Delay :%llu %d %d\n", event->sy.ticketNum, event->contech_id, queuedEvents.size());
                
                queuedEvents[event->contech_id].push_back(event);
                eventQueueCurrent = queuedEvents.begin();
                resetMinTicket = true;
                minQueuedTicket = 0;
                currentQueuedCount++;
                if (currentQueuedCount > maxQueuedCount) maxQueuedCount = currentQueuedCount;
                // Yes, recursion
                //   This should only happen a limited number of times
                //   At most N-1, where N is the number of contexts and the next N-2 events
                //   are all ticketed events that must be queued.
                event = getNextContechEvent(inFile);
            }
            else {
                //printf("Ticket:%llu %d\n", event->sy.ticketNum, queuedEvents.size());
                ticketNum ++;
            }
            break;
        }
        default:
            break;
    }

    return event;
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
        while (!workList.empty())
        {
            TaskId id = workList.top().second;
            Task* t = tasks[id].first;
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
                    bytesWritten += Task::writeContechTask(*t, out);
                    taskWriteCount += 1;
                    
                    // Add successors to the work list
                    for (TaskId succ : t->getSuccessorTasks())
                    {
                        auto it = tasks.find(succ);
                        
                        // If the successor has not been received, then store that one its
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
                    tasks[id].first = NULL;
                    tasks.erase(id);
                }
            }
        }
        taskLastWriteCount = taskWriteCount;
    }
    
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