#include "middle.hpp"
#include "taskWrite.hpp"
#include <sys/timeb.h>
#include <pthread.h>

using namespace std;
using namespace contech;

void* backgroundTaskWriter(void*);

int main(int argc, char* argv[])
{
    // Open input file
    // Use command line argument or stdin
    ct_file* in;
    bool parallelMiddle = true;
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
        fprintf(stderr, "Missing positional argument event trace\n");
        fprintf(stderr, "%s <event trace> <taskgraph>\n", argv[0]);
        return 1;
    }
    
    // Open output file
    // Use command line argument or stdout
    //FILE* out;
    ct_file* out;
    if (argc > 2)
    {
        //out = fopen(argv[2], "wb");
        out = create_ct_file_w(argv[2],false);
        assert(out != NULL && "Could not open output file");
    }
    else
    {
        fprintf(stderr, "Missing positional argument taskgraph\n");
        fprintf(stderr, "%s <event trace> <taskgraph>\n", argv[0]);
        return 1;
    }
    
    int taskGraphVersion = TASK_GRAPH_VERSION;
    unsigned long long space = 0;
    
    // Init TaskGraphFile
    ct_write(&taskGraphVersion, sizeof(int), out);
    ct_write(&space, sizeof(unsigned long long), out);
    
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
    TaskGraphInfo *tgi = new TaskGraphInfo();
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
        else if (event->event_type == ct_event_basic_block_info)
        {
            string functionName, fileName;
            if (event->bbi.fun_name != NULL) functionName.assign(event->bbi.fun_name);
            if (event->bbi.file_name != NULL) fileName.assign(event->bbi.file_name);
            //printf("%d - %d at %d\t", event->bbi.basic_block_id, event->bbi.num_mem_ops, event->bbi.line_num);
            //printf("in %s() of %s\n", event->bbi.fun_name, event->bbi.file_name);
            //printf("%d <> %d\n", event->bbi.crit_path_len, event->bbi.num_ops);
            tgi->addRawBasicBlockInfo(event->bbi.basic_block_id, 
                                     event->bbi.line_num, 
                                     event->bbi.num_mem_ops,
                                     event->bbi.num_ops,
                                     event->bbi.crit_path_len,
                                     functionName,
                                     fileName);
        }
        deleteContechEvent(event);
        if (seenFirstEvent) break;
    }
    assert(seenFirstEvent);
    
    tgi->writeTaskGraphInfo(out);
    delete tgi;

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
                
                if (DEBUG) {fprintf(stderr, "%s -> %s via Basic Block\n", 
                                            activeT->getTaskId().toString().c_str(),
                                            activeContech.activeTask()->getTaskId().toString().c_str());}
                
                // Is the current active task a complete join?
                if (activeT->getType() == task_type_join &&
                    activeContech.isCompleteJoin(activeT->getTaskId()))
                {
                    assert(activeContech.removeTask(activeT) == true);
                    backgroundQueueTask(activeT);
                }
                
                updateContextTaskList(activeContech);
                
                activeT = activeContech.activeTask();
            }
            else if (activeT->getBBCount() >= MAX_BLOCK_THRESHOLD)
            {
                // There is no available time stamp for ending this task
                //   Assume that every basic block costs 1 cycle, which is a
                //   lower bound
                activeT->setEndTime(activeT->getStartTime() + MAX_BLOCK_THRESHOLD);
                activeContech.createBasicBlockContinuation();
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
            if (DEBUG) {fprintf(stderr, "Create: %d -> %d\n", event->contech_id, event->tc.other_id);}
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
                Context& otherContext = context[event->tj.other_id];
                
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
                        if (parallelMiddle)
                            updateContextTaskList(activeContech);
                    }
                }
                
                if (DEBUG) eventDebugPrint(activeContech.activeTask()->getTaskId(), "exited", otherContext.activeTask()->getTaskId(), startTime, endTime);

            // I joined with another task
            } 
            else 
            {
                Context& otherContext = context[event->tj.other_id];
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
                    
                    if (parallelMiddle)
                        updateContextTaskList(otherContext);
                    
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

        } 
        
        // Memcpy etc
        else if (event->event_type == ct_event_bulk_memory_op)
        {
        
        }
        // End switch block on event type

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
                    //t->setFileOffset(bytesWritten);
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

//    printf("Max Queued Event Count: %u\n", maxQueuedCount);
    
    {
        struct timeb tp;
        ftime(&tp);
        printf("MIDDLE_END: %d.%03d\n", (unsigned int)tp.time, tp.millitm);
    }
    
    close_ct_file(out);
    
    return 0;
}
