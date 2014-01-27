#include "middle.hpp"

using namespace std;
using namespace contech;

unsigned int currentQueuedCount = 0;
unsigned int maxQueuedCount = 0;

int main(int argc, char* argv[])
{
    // Open input file
    // Use command line argument or stdin
    ct_file* in;
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
            // Record that this task executed this basic block
            activeContech.activeTask()->recordBasicBlockAction(event->bb.basic_block_id);

            // Examine memory operations
            for (uint i = 0; i < event->bb.len; i++)
            {
                ct_memory_op memOp = event->bb.mem_op_array[i];
                activeContech.activeTask()->recordMemOpAction(memOp.is_write, memOp.pow_size, memOp.addr);
            }
        }

        // Task create: Create and initialize child task/context
        else if (event->event_type == ct_event_task_create)
        {
            // If this context is already running, then it created a new thread
            if (activeContech.hasStarted == true)
            {
                // Assign an ID for the new context
                TaskId childTaskId(event->tc.other_id, 0);

                // Make a "task create" task
                Task* taskCreate = activeContech.createContinuation(task_type_create, startTime, endTime);

                // Assign the new task as a child
                taskCreate->addSuccessor(childTaskId);

                // Create a continuation
                activeContech.createBasicBlockContinuation();

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
                activeContech.activeTask()->addPredecessor(event->tc.other_id);

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

            // Make the sync dependent on whoever accessed the sync primitive last
            if (ownerList.count(event->sy.sync_addr) > 0)
            {
                Task* owner = ownerList[event->sy.sync_addr];
                owner->addSuccessor(sync->getTaskId());
                sync->addPredecessor(owner->getTaskId());
            }

            // Make the sync task the new owner of the sync primitive
            if (event->sy.sync_type != ct_cond_wait)
                ownerList[event->sy.sync_addr] = sync;
                
            if (event->sy.sync_type == ct_sync_release ||
                event->sy.sync_type == ct_sync_acquire)
                sync->setSyncType(sync_type_lock);
            else if (event->sy.sync_type == ct_cond_wait ||
                event->sy.sync_type == ct_cond_sig)
                sync->setSyncType(sync_type_condition_variable);
            else
                sync->setSyncType(sync_type_user_defined);

            // Create a continuation
            activeContech.createBasicBlockContinuation();
        }

        // Task joins
        else if (event->event_type == ct_event_task_join)
        {
            Task& otherTask = *context[event->tj.other_id].activeTask();

            // I exited
            if (event->tj.isExit){
                activeContech.activeTask()->setEndTime(startTime);
                activeContech.endTime = startTime;
                if (DEBUG) eventDebugPrint(activeContech.activeTask()->getTaskId(), "exited", otherTask.getTaskId(), startTime, endTime);

            // I joined with another task
            } else {
                // Create a join task
                Task* join = activeContech.createContinuation(task_type_join, startTime, endTime);
                // Set the other task's continuation to the join
                otherTask.addSuccessor(join->getTaskId());
                join->addPredecessor(otherTask.getTaskId());
                // Front end guarantees that we will see the other task exit before we see the join
                assert(context[event->tj.other_id].endTime != 0);
                // The join task starts when both tasks have executed the join, and ends when the parent finishes the join
                join->setStartTime(max(context[event->tj.other_id].endTime, startTime));
                if (DEBUG) eventDebugPrint(activeContech.activeTask()->getTaskId(), "joined with", otherTask.getTaskId(), startTime, endTime);

                //Create a continuation
                activeContech.createBasicBlockContinuation();
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
                Task* barrierTask = barrierList[event->bar.sync_addr].onExit(endTime);
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
    // TODO Write out tasks as soon as they are ready and remove from the list
    
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

    printf("Max Queued Event Count: %u\n", maxQueuedCount);
    
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
    
    unsigned long long currMinTicket = ~0;
    while (!queuedEvents.empty())
    {
        if (ticketNum < minQueuedTicket) break;
        if (eventQueueCurrent->second.empty())
        {
            auto t = eventQueueCurrent;
            ++eventQueueCurrent;
            queuedEvents.erase(t);
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
            ticketNum++;
            eventQueueCurrent->second.pop_front();
            eventQueueCurrent = queuedEvents.begin();
            currentQueuedCount--;
            return event;
        }
        else
        {
            ++eventQueueCurrent;
            if (event->sy.ticketNum < currMinTicket) currMinTicket = event->sy.ticketNum;
            if (eventQueueCurrent == queuedEvents.end())
            {
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