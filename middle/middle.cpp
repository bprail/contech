#include "middle.hpp"
#include "taskWrite.hpp"
#include <sys/timeb.h>
#include <pthread.h>

using namespace std;
using namespace contech;

void* backgroundTaskWriter(void*);
ct_tsc_t totalCycles = 0;

int main(int argc, char* argv[])
{
    bool parallelMiddle = true;
    pthread_t backgroundT;
    EventQ eventQ;
    bool roiEvent = false;
    
    // First attempt middle layer in parallel, if there is an error,
    //   then restart in serial mode.
    //   TODO: Implement restart / reset, or a flag for running serially.
reset_middle:
    if (argc < 3)
    {
        fprintf(stderr, "Missing positional argument(s)\n");
        fprintf(stderr, "%s <event trace>* <taskgraph> [-d]\n", argv[0]);
        return 1;
    }
    
    // Print debug statements?
    bool DEBUG = false;
    if (!strcmp(argv[argc - 1], "-d"))
    {        
        DEBUG = true;
        printf("Debug mode enabled.\n");
    }
    
    int lastInPos = argc - 2;
    int totalRanks = 0;
    if (DEBUG == true) lastInPos--;
    
    for (int argPos = 1; argPos <= lastInPos; argPos++, totalRanks++)
    {
        FILE* in;
        in = fopen(argv[argPos], "rb");
        assert(in != NULL && "Could not open input file");
        eventQ.registerEventList(in);
    }
    
    // Open output file
    // Use command line argument or stdout
    //FILE* out;
    FILE* out;
    int outArgPos = argc - 1;
    if (DEBUG == true) outArgPos--;
    out = fopen(argv[outArgPos], "wb");
    assert(out != NULL && "Could not open output file");
    
    int taskGraphVersion = TASK_GRAPH_VERSION;
    unsigned long long space = 0;
    
    // Init TaskGraphFile
    ct_write(&taskGraphVersion, sizeof(int), out);
    ct_write(&space, sizeof(unsigned long long), out); // Index
    ct_write(&space, sizeof(unsigned long long), out); // ROI start
    ct_write(&space, sizeof(unsigned long long), out); // ROI end
    
    pthread_mutex_init(&taskQueueLock, NULL);
    pthread_cond_init(&taskQueueCond, NULL);
    pthread_mutex_init(&taskMemLock, NULL);
    pthread_cond_init(&taskMemCond, NULL);
    taskQueue = new deque<Task*>;
    int r = pthread_create(&backgroundT, NULL, backgroundTaskWriter, &out);
    assert(r == 0);
    
    // Track the owners of sync primitives
    map<ct_addr_t, Task*> ownerList;
    
    // Track the barrier task for each address
    map<ct_addr_t, BarrierWrapper> barrierList;

    // Declare each context
    map<ContextId, Context> context;

    // MPI Transfers src-rank -> dst rank -> tag -> task
    map <int, map <int, map <int, Task*> > > mpiSendQ;
    map <int, map <int, map <int, Task*> > > mpiRecvQ;
    map <int, map <ct_addr_t, mpi_recv_req> > mpiReq;
    
    struct mpi_bcast {
        int arrival_count;
        ct_addr_t buf_ptr;
        Task* root_sync, *root_bb;
        vector<Task*> dst_sync;
        vector<ct_memory_op> dst_buf;
    } ;
    
    map <int, map <size_t, mpi_bcast*> > mpiBCast;
    
    // Context 0 is special, since it is uncreated
    if (totalRanks > 1)
    {
        //context[0].tasks.push_front(new Task(0, task_type_create));
        context[0].tasks[0] = new Task(0, task_type_create);
    }
    else
    {
        //context[0].tasks.push_front(new Task(0, task_type_basic_blocks));
        context[0].tasks[0] = new Task(0, task_type_basic_blocks);
    }
    context[0].hasStarted = true;
    

    // Count the number of events processed
    uint64 eventCount = 0;

    {
        struct timeb tp;
        ftime(&tp);
        printf("MIDDLE_START: %d.%03d\n", (unsigned int)tp.time, tp.millitm);
    }
    
    // Scan through the file for the first real event
    bool seenFirstEvent = false;
    int currentRank = 0;
    TaskGraphInfo *tgi = new TaskGraphInfo();
    while (ct_event* event = eventQ.getNextContechEvent(&currentRank))
    {
        if (event->contech_id == 0
        &&  event->event_type == ct_event_task_create
        &&  event->tc.other_id == 0)
        {
            // Use the timestamp of the first event in context 0 as t=0 in absolute time
            context[(currentRank << 24) | 0].timeOffset = event->tc.start_time;
            
            // Since every instance should have identical bbinfo, the first create should be
            //   rank 0, and then every other rank
            assert(currentRank == 0);
            if (DEBUG) printf("Global offset is %lu\n", context[0].timeOffset);
            seenFirstEvent = true;
        }
        else if (event->event_type == ct_event_basic_block_info && currentRank == 0)
        {
            string functionName, fileName, callFunName;
            if (event->bbi.fun_name != NULL) functionName.assign(event->bbi.fun_name);
            if (event->bbi.file_name != NULL) fileName.assign(event->bbi.file_name);
            if (event->bbi.callFun_name != NULL) callFunName.assign(event->bbi.callFun_name);
            //printf("%d - %d at %d\t", event->bbi.basic_block_id, event->bbi.num_mem_ops, event->bbi.line_num);
            //printf("in %s() of %s\n", event->bbi.fun_name, event->bbi.file_name);
            //printf("%d <> %d\n", event->bbi.crit_path_len, event->bbi.num_ops);
            
            tgi->addRawBasicBlockInfo(event->bbi.basic_block_id, 
                                     event->bbi.flags,
                                     event->bbi.line_num, 
                                     event->bbi.num_mem_ops,
                                     event->bbi.num_ops,
                                     event->bbi.crit_path_len,
                                     functionName,
                                     fileName,
                                     callFunName);
        }
        EventLib::deleteContechEvent(event);
        if (seenFirstEvent) break;
    }
    assert(seenFirstEvent);
    
    tgi->writeTaskGraphInfo(out);
    delete tgi;

    // Main loop: Process the events from the file in order
    while (ct_event* event = eventQ.getNextContechEvent(&currentRank))
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
            case ct_event_bulk_memory_op:
            case ct_event_mpi_transfer:
            case ct_event_mpi_wait:
            case ct_event_mpi_allone:
            case ct_event_roi:
                break;
            default:
                EventLib::deleteContechEvent(event);
                continue;
        }

        // New context ids should only appear on task create events
        // Seeing an invalid context id is a good sign that the trace is corrupt
        if (event->event_type != ct_event_task_create && 
            !context.count((currentRank << 24) | event->contech_id))
        {
            cerr << "ERROR: Saw an event " << event->event_type <<" from a new context " << event->contech_id << " before seeing a create event for that context." << endl;
            cerr << "Either the trace is corrupt or the trace file is missing a create event for the new context. " << endl;
            //displayContechEventDebugInfo();
            assert(0);
        }
        
        // The context in which this event occurred
        Context& activeContech = context[(currentRank << 24) | event->contech_id];

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
            case ct_event_mpi_transfer:
                startTime = event->mpixf.start_time;
                endTime = event->mpixf.end_time;
                break;
            case ct_event_mpi_allone:
                startTime = event->mpiao.start_time;
                endTime = event->mpiao.end_time;
                break;
            default:
                hasTime = false;
                break;
        }
        
        // Apply timestamp offsets
        if (hasTime)
        {
            // TODO: Why does NAS-is fail on this assert?
            if (event->event_type != ct_event_task_create && 
                startTime <= activeContech.timeOffset)
            {
                printf("Event: %p of type %d is too early (%lu <= %lu)\n", event, event->event_type, startTime, activeContech.timeOffset);
            }
            assert(event->event_type == ct_event_task_create || startTime > activeContech.timeOffset);
            if (endTime < startTime)
            {
                printf("Timestamps reordered on type (%d): %lu <= %lu\n", event->event_type,
                                                                            startTime,
                                                                            endTime);
            }
            assert(startTime <= endTime);
        
        
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
                
                if (DEBUG) {fprintf(stderr, "%s (%d) -> %s via Basic Block (%d)\n", 
                                            activeT->getTaskId().toString().c_str(), activeT->getType(),
                                            activeContech.activeTask()->getTaskId().toString().c_str(),
                                            event->bb.basic_block_id);}
                
                // Is the current active task a create or a complete join?
                attemptBackgroundQueueTask(activeT, activeContech);
                
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
                activeContech.removeTask(activeT);
                backgroundQueueTask(activeT);
                updateContextTaskList(activeContech);
                
                activeT = activeContech.activeTask();
            }
            
            // If the basic block action will overflow, then split the task at this time
            try {
                // Record that this task executed this basic block
                activeT->recordBasicBlockAction(event->bb.basic_block_id);
            }
            catch (std::bad_alloc)
            {
                activeT->setEndTime(activeT->getStartTime() + MAX_BLOCK_THRESHOLD);
                activeContech.createBasicBlockContinuation();
                activeContech.removeTask(activeT);
                backgroundQueueTask(activeT);
                updateContextTaskList(activeContech);
                
                activeT = activeContech.activeTask();
                activeT->recordBasicBlockAction(event->bb.basic_block_id);
            }

            // Examine memory operations
            for (uint i = 0; i < event->bb.len; i++)
            {
                ct_memory_op memOp = event->bb.mem_op_array[i];
                memOp.rank = currentRank;
                activeT->recordMemOpAction(memOp.is_write, memOp.pow_size, memOp.data);
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
                TaskId childTaskId((currentRank << 24) | event->tc.other_id, 0);
            
                // If other_id is 0, then this is the initial create,
                //   It will need to link back to an original creator
                if (event->tc.other_id == 0)
                {
                    Task* taskCreate;
                    
                    context[(currentRank << 24) | 0].hasStarted = true;
                    //context[(currentRank << 24) | 0].tasks.push_front(new Task(childTaskId, task_type_basic_blocks));
                    context[(currentRank << 24) | 0].tasks[childTaskId] = new Task(childTaskId, task_type_basic_blocks);
                    context[(currentRank << 24) | 0].timeOffset = event->tc.start_time;
                    taskCreate = context[0].activeTask();
                    assert(taskCreate->getType() == task_type_create);
                    taskCreate->addSuccessor(childTaskId);
                    activeContech.activeTask()->addPredecessor(taskCreate->getTaskId());
                }
                else
                {
                    // Make a "task create" task
                    //   N.B. This create task may be a combination of several create events.
                    Task* taskCreate;
                    
                    if (activeContech.activeTask()->getType() != task_type_create)
                    {
                        Task* activeT = activeContech.activeTask();
                        taskCreate = activeContech.createContinuation(task_type_create, startTime, endTime);
                        attemptBackgroundQueueTask(activeT, activeContech);
                    }
                    else
                    {
                        taskCreate = activeContech.activeTask();
                        if (taskCreate->getStartTime() > startTime) taskCreate->setStartTime(startTime);
                        if (taskCreate->getEndTime() < endTime) taskCreate->setEndTime(endTime);
                    }

                    // Assign the new task as a child
                    taskCreate->addSuccessor(childTaskId);
                    eventQ.readyEvents(currentRank, event->tc.other_id);
                    
                    // Add the information so that the created task knows its creator.
                    if (context[(currentRank << 24) | event->tc.other_id].hasStarted == true)
                    {
                        Task* childTask = context[(currentRank << 24) | event->tc.other_id].getTask(childTaskId);
                        assert (childTask != NULL);
                        childTask->addPredecessor(taskCreate->getTaskId());
                    }
                    else
                    {
                        activeContech.creatorMap[(currentRank << 24) | event->tc.other_id] = taskCreate->getTaskId();
                    }
                    
                    if (DEBUG) eventDebugPrint(activeContech.activeTask()->getTaskId(), "created", childTaskId, startTime, endTime);
                }
            // If this context was not already running, then it was just created
            } else {
                TaskId newContechTaskId((currentRank << 24) | event->contech_id, 0);

                // Compute time offset for new context
                activeContech.timeOffset = context[(currentRank << 24) | event->tc.other_id].timeOffset + event->tc.approx_skew;
                startTime = startTime - activeContech.timeOffset;
                endTime = endTime - activeContech.timeOffset;

                // Start the first task for the new context
                activeContech.hasStarted = true;
                //activeContech.tasks.push_front(new Task(newContechTaskId, task_type_basic_blocks));
                activeContech.tasks[newContechTaskId] = new Task(newContechTaskId, task_type_basic_blocks);
                activeContech.activeTask()->setStartTime(endTime);

                // Record parent of this task
                if (context[(currentRank << 24) | event->tc.other_id].hasStarted == true)
                {
                    TaskId creatorId = context[(currentRank << 24) | event->tc.other_id].getCreator((currentRank << 24) | event->contech_id);
                    if (creatorId != TaskId(0))
                    {
                        activeContech.activeTask()->addPredecessor(creatorId);
                    }
                    if (DEBUG) eventDebugPrint(activeContech.activeTask()->getTaskId(), "started by", creatorId, startTime, endTime);
                }
                else if (DEBUG) eventDebugPrint(activeContech.activeTask()->getTaskId(), "started by", TaskId(event->tc.other_id,0), startTime, endTime);
                
                if (DEBUG) cerr << activeContech.activeTask()->getContextId() << ": skew = " << event->tc.approx_skew << endl;
            }
        }
        
        // Sync events
        else if (event->event_type == ct_event_sync)
        {
            // Create a sync task
            Task* activeT = activeContech.activeTask();
            Task* sync = activeContech.createContinuation(task_type_sync, startTime, endTime);
            attemptBackgroundQueueTask(activeT, activeContech);
            ct_memory_op syncA;
            syncA.data = 0;
            syncA.addr = event->sy.sync_addr;
            syncA.rank = currentRank;
            
            // Record the address in this sync task as an action
            activeContech.activeTask()->recordMemOpAction(true, 8, syncA.data);

            // Create a continuation
            activeContech.createBasicBlockContinuation();
            
            // Make the sync dependent on whomever accessed the sync primitive last         
            auto it = ownerList.find(syncA.data);
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
                ownerList[syncA.data] = sync;
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
            // I exited
            if (event->tj.isExit)
            {
                Task* activeT = activeContech.activeTask();
                Task* otherTask = NULL;
                TaskId myId = activeT->getTaskId();
                Context& otherContext = context[(currentRank << 24) | event->tj.other_id];
                
                activeT->setEndTime(startTime);
                activeContech.endTime = startTime;
                
                if (DEBUG) eventDebugPrint(activeT->getTaskId(), "exited", otherContext.activeTask()->getTaskId(), startTime, endTime);
                otherTask = otherContext.childExits(myId);
                if (otherTask != NULL)
                {
                    TaskId otherTaskId = otherTask->getTaskId();
                    activeT->addSuccessor(otherTaskId);
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
                    
                    activeContech.removeTask(activeT);
                    backgroundQueueTask(activeT);
                }
            } 
            else // I joined with another task
            {
                Context& otherContext = context[(currentRank << 24) | event->tj.other_id];
                if (otherContext.endTime != 0)
                {
                    // Create a join task
                    Task* taskJoin;
                    Task* otherTask = otherContext.activeTask();
                    
                    if (activeContech.activeTask()->getType() != task_type_join)
                    {
                        Task* activeT = activeContech.activeTask();
                        taskJoin = activeContech.createContinuation(task_type_join, startTime, endTime);
                        attemptBackgroundQueueTask(activeT, activeContech);
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
                    if (parallelMiddle)
                    {
                        otherContext.removeTask(otherTask);
                        backgroundQueueTask(otherTask);
                        updateContextTaskList(otherContext);
                    }
                    
                    
                }
                else
                {
                    // Child has not exited yet
                    
                    // Create a join task
                    Task* taskJoin;
                    if (activeContech.activeTask()->getType() != task_type_join)
                    {
                        Task* activeT = activeContech.activeTask();
                        taskJoin = activeContech.createContinuation(task_type_join, startTime, endTime);
                        attemptBackgroundQueueTask(activeT, activeContech);
                    }
                    else
                    {
                        taskJoin = activeContech.activeTask();
                        if (taskJoin->getStartTime() > startTime) taskJoin->setStartTime(startTime);
                        if (taskJoin->getEndTime() < endTime) taskJoin->setEndTime(endTime);
                    }
                    activeContech.getChildJoin(ContextId((currentRank << 24) | event->tj.other_id), taskJoin);
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
                ct_memory_op barA;
                barA.data = 0;
                barA.addr = event->bar.sync_addr;
                barA.rank = currentRank;
                if (activeContech.activeTask()->getType() != task_type_basic_blocks)
                {
                    Task* activeT = activeContech.activeTask();
                    activeContech.createBasicBlockContinuation();
                    attemptBackgroundQueueTask(activeT, activeContech);
                }
                Task* barrierTask = barrierList[barA.data].onEnter(*activeContech.activeTask(), startTime, event->bar.sync_addr);
                if (DEBUG) eventDebugPrint(activeContech.activeTask()->getTaskId(), "arrived at barrier", barrierTask->getTaskId(), startTime, endTime);
            }

            // Leaving a barrier
            else
            {
                // Record my exit from the barrier, and get the associated barrier task
                bool isFinished = false;
                ct_memory_op barA;
                barA.data = 0;
                barA.addr = event->bar.sync_addr;
                barA.rank = currentRank;
                Task* barrierTask = barrierList[barA.data].onExit(activeContech.activeTask(), endTime, &isFinished);
                if (DEBUG) 
                {
                    eventDebugPrint(activeContech.activeTask()->getTaskId(), "leaving barrier", barrierTask->getTaskId(), startTime, endTime);
                    if (isFinished == true)
                    {
                        printf("\tBarrier(%lx) finished\n", barA.data);
                    }
                }

                // If I own the barrier, my continuation's ID has to come after it. Otherwise just use the next ID.
                bool myBarrier = (barrierTask->getContextId() == ((currentRank << 24) | event->contech_id));

                Task* continuation;
                if (myBarrier)
                {
                    Task* activeT = activeContech.activeTask();
                    // Create the continuation task (this is a special case, since this is a continuation of the barrier, which is not the active task)
                    // TODO This copies a lot of code from Context::createBasicBlockContinuation()
                    continuation = new Task(barrierTask->getTaskId().getNext(), task_type_basic_blocks);
                    activeContech.activeTask()->addSuccessor(continuation->getTaskId());
                    continuation->addPredecessor(activeContech.activeTask()->getTaskId());
                    // Barrier owner is responsible for making sure the barrier task gets added to the output file
                    //activeContech.tasks.push_front(barrierTask);
                    activeContech.tasks[barrierTask->getTaskId()] = barrierTask;
                    // Make it the active task for this context
                    //activeContech.tasks.push_front(continuation);
                    activeContech.tasks[continuation->getTaskId()] = continuation;
                    attemptBackgroundQueueTask(activeT, activeContech);
                }
                else
                {
                    Task* activeT = activeContech.activeTask();
                    continuation = activeContech.createBasicBlockContinuation();
                    attemptBackgroundQueueTask(activeT, activeContech);
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
            ct_memory_op memA;
            memA.data = 0;
            memA.addr = event->mem.alloc_addr;
            memA.rank = currentRank;
            if (event->mem.isAllocate)
            {
                activeContech.activeTask()->recordMallocAction(memA.data, event->mem.size);
            } else {
                activeContech.activeTask()->recordFreeAction(memA.data);
            }

        } 
        
        // Memcpy etc
        //  In the case of etc, src may be NULL
        else if (event->event_type == ct_event_bulk_memory_op)
        {
            ct_memory_op srcA, dstA;
            srcA.data = 0;
            srcA.addr = event->bm.src_addr;
            srcA.rank = currentRank;
            dstA.data = 0;
            dstA.addr = event->bm.dst_addr;
            dstA.rank = currentRank;
            activeContech.activeTask()->recordMemCpyAction(event->bm.size, dstA.data, srcA.data);
        }
        
        else if (event->event_type == ct_event_mpi_transfer)
        {
            // MPI maps using a 3-tuple {src_rank, tag, datatype} -> {dst_rank, tag, datatype}
            //   We have discarded datatype
            
            //  TODO: find why this assert fails
            //assert(currentRank != event->mpixf.comm_rank);
            
            // Send: S -> R -> S*
            //   S* is only present when send is blocking
            if (event->mpixf.isSend == true)
            {
                Task* firstSend;
                
                activeContech.createContinuation(task_type_sync, startTime, endTime - 1);
                firstSend = activeContech.activeTask();
                firstSend->setSyncType(sync_type_mpi_transfer);
                
                if (event->mpixf.isBlocking == true)
                {
                    activeContech.createContinuation(task_type_sync, endTime - 1, endTime);
                    activeContech.activeTask()->setSyncType(sync_type_mpi_transfer);
                }
                
                // Test if Recv has already happened
                auto recvIt = mpiRecvQ[event->mpixf.comm_rank][currentRank].find(event->mpixf.tag);
                if (recvIt != mpiRecvQ[event->mpixf.comm_rank][currentRank].end())
                {
                    Task* recvTask = recvIt->second;
                    Task* secSend = NULL;
                    firstSend->addSuccessor(recvTask->getTaskId());
                    recvTask->addPredecessor(firstSend->getTaskId());
                    
                    printf("Send - %u:%u <-> %u:%u\n", (uint32_t)firstSend->getContextId(),
                                                       (uint32_t)firstSend->getSeqId(),
                                                       (uint32_t)recvTask->getContextId(),
                                                       (uint32_t)recvTask->getSeqId());
                    
                    if (event->mpixf.isBlocking == true)
                    {
                        secSend = activeContech.activeTask();
                        //secSend->addPredecessor(recvTask->getTaskId());
                        //recvTask->addSuccessor(secSend->getTaskId());
                    }
                    
                    // remove tuple
                    mpiRecvQ[event->mpixf.comm_rank][currentRank].erase(recvIt);
                    
                    //TODO: Background Queue the tasks
                    bool rem = context[firstSend->getContextId()].removeTask(firstSend);
                    assert(rem == true);
                    backgroundQueueTask(firstSend);
                    rem = context[recvTask->getContextId()].removeTask(recvTask);
                    assert(rem == true);
                    backgroundQueueTask(recvTask);
                    activeContech.createBasicBlockContinuation();
                    if (secSend != NULL)
                    {
                        rem = context[secSend->getContextId()].removeTask(secSend);
                        assert(rem == true);
                        backgroundQueueTask(secSend);
                    }
                }
                else // Queue send info
                {
                    mpiSendQ[currentRank][event->mpixf.comm_rank][event->mpixf.tag] = firstSend;
                    activeContech.createBasicBlockContinuation();
                }
            }
            else
            {
                // Recv: -> R ->
                //   Recv is nonblocking, then there should be a MPI_wait()
                //   In this scenario, the task cannot be created until the wait completes

                if (event->mpixf.isBlocking == false)
                {
                    struct mpi_recv_req mrr;
                
                    mrr.comm_rank = event->mpixf.comm_rank;
                    mrr.tag = event->mpixf.tag;
                    mrr.buf_ptr = event->mpixf.buf_ptr;
                    mrr.buf_size = event->mpixf.buf_size;
                
                    mpiReq[currentRank][event->mpixf.req_ptr] = mrr;
                }
                else
                {
                    // Blocking recv
                    //   Create a task to receive the data
                    activeContech.createContinuation(task_type_sync, startTime, endTime);
                    activeContech.activeTask()->setSyncType(sync_type_mpi_transfer);
                    
                    auto sendIt = mpiSendQ[event->mpixf.comm_rank][currentRank].find(event->mpixf.tag);
                    if (sendIt != mpiSendQ[event->mpixf.comm_rank][currentRank].end())
                    {
                        Task* firstSend = sendIt->second;
                        Task* recvT = activeContech.activeTask();
                        
                        recvT->addPredecessor(firstSend->getTaskId());
                        firstSend->addSuccessor(recvT->getTaskId());
                        printf("Recv - %u:%u <-> %u:%u\n", (uint32_t)firstSend->getContextId(),
                                                           (uint32_t)firstSend->getSeqId(),
                                                           (uint32_t)recvT->getContextId(),
                                                           (uint32_t)recvT->getSeqId());
                        
                        // Remove tuple
                        mpiSendQ[event->mpixf.comm_rank][currentRank].erase(sendIt);
                        
                        // TODO: Find second send task, if it exists
                        
                        activeContech.createBasicBlockContinuation();
                        bool rem = context[firstSend->getContextId()].removeTask(firstSend);
                        assert(rem == true);
                        backgroundQueueTask(firstSend);
                        rem = context[recvT->getContextId()].removeTask(recvT);
                        assert(rem == true);
                        backgroundQueueTask(recvT);
                        
                        // scratch
                        ct_memory_op srcA, dstA;
                        srcA.data = 0;
                        srcA.addr = event->bm.src_addr; // TODO: fix this addr
                        srcA.rank = event->mpixf.comm_rank;
                        dstA.data = 0;
                        dstA.addr = event->mpixf.buf_ptr;
                        dstA.rank = currentRank;
                        activeContech.activeTask()->recordMemCpyAction(event->bm.size, dstA.data, srcA.data);
                    }
                    else
                    {
                        mpiRecvQ[currentRank][event->mpixf.comm_rank][event->mpixf.tag] = activeContech.activeTask();
                        activeContech.createBasicBlockContinuation();
                    }
                }
            }
        }
        else if (event->event_type == ct_event_mpi_allone)
        {
            if (event->mpiao.isToAll == true)
            {
                auto bcast = mpiBCast[event->mpiao.one_comm_rank].find(event->mpiao.buf_size);
                mpi_bcast* mbc = NULL;
                if (bcast == mpiBCast[event->mpiao.one_comm_rank].end())
                {
                    mbc = new mpi_bcast;
                    mbc->arrival_count = 1;
                    mbc->root_sync = NULL;
                    mbc->dst_sync.clear();
                    mbc->dst_sync.resize(totalRanks);
                    mpiBCast[event->mpiao.one_comm_rank][event->mpiao.buf_size] = mbc;
                }
                else
                {
                    mbc = bcast->second;
                }
                
                // If the current rank is the comm rank, then this is the broadcaster.
                if (event->mpiao.one_comm_rank == currentRank)
                {
                    activeContech.createContinuation(task_type_sync, startTime, endTime);
                    activeContech.activeTask()->setSyncType(sync_type_mpi_transfer);
                    bcast->second->root_sync = activeContech.activeTask();
                    bcast->second->buf_ptr = event->mpiao.buf_ptr;
                }
                else
                {
                    ct_memory_op dstA;
                    
                    Task* current = activeContech.activeTask();
                    activeContech.createContinuation(task_type_sync, startTime, endTime);
                    activeContech.activeTask()->setSyncType(sync_type_mpi_transfer);
                    bcast->second->dst_sync.push_back(activeContech.activeTask());
                    
                    dstA.data = 0;
                    dstA.addr = event->mpiao.buf_ptr;
                    dstA.rank = currentRank;
                    
                    bcast->second->dst_buf.push_back(dstA);
                    backgroundQueueTask(current);
                }
                mbc->arrival_count++;
                
                if (mbc->arrival_count == totalRanks)
                {
                    Task* root_sync = mbc->root_sync;
                    Task* root_bb = mbc->root_bb;
                    int i = 0;
                    
                    for (auto it = mbc->dst_sync.begin(), et = mbc->dst_sync.end(); it != et; ++it, i++)
                    {
                        Task* dst_sync = *it;
                        root_sync->addSuccessor(dst_sync->getTaskId());
                        dst_sync->addPredecessor(root_sync->getTaskId());
                        
                        ct_memory_op srcA;
                        srcA.data = 0;
                        srcA.addr = mbc->buf_ptr;
                        srcA.rank = event->mpiao.one_comm_rank;
                        root_bb->recordMemCpyAction(event->mpiao.buf_size, mbc->dst_buf[i].data, srcA.data);
                        backgroundQueueTask(*it);
                    }
                    
                    backgroundQueueTask(root_bb);
                    backgroundQueueTask(root_sync);
                    
                    mpiBCast[event->mpiao.one_comm_rank].erase(bcast);
                    delete mbc;
                }
            }
            else
            {
                // MPI_Reduce
            }
        }
        else if (event->event_type == ct_event_mpi_wait)
        {
            struct mpi_recv_req mrr;
            
            mrr = mpiReq[currentRank][event->mpiw.req_ptr];
            
            mpiReq[currentRank].erase(event->mpiw.req_ptr);
            
            // Copied from blocking receive path
            //   In execution, the send must have already happened, but interleaved
            //   traces may give a different order
            activeContech.createContinuation(task_type_sync, startTime, endTime);
            activeContech.activeTask()->setSyncType(sync_type_mpi_transfer);
            
            auto sendIt = mpiSendQ[event->mpixf.comm_rank][currentRank].find(event->mpixf.tag);
            if (sendIt != mpiSendQ[event->mpixf.comm_rank][currentRank].end())
            {
                Task* firstSend = sendIt->second;
                Task* recvT = activeContech.activeTask();
                
                recvT->addPredecessor(firstSend->getTaskId());
                firstSend->addSuccessor(recvT->getTaskId());
                printf("Recv - %u:%u <-> %u:%u\n", (uint32_t)firstSend->getContextId(),
                                                   (uint32_t)firstSend->getSeqId(),
                                                   (uint32_t)recvT->getContextId(),
                                                   (uint32_t)recvT->getSeqId());
                
                // Remove tuple
                mpiSendQ[event->mpixf.comm_rank][currentRank].erase(sendIt);
                
                // TODO: Find second send task, if it exists
                
                activeContech.createBasicBlockContinuation();
                bool rem = context[firstSend->getContextId()].removeTask(firstSend);
                assert(rem == true);
                backgroundQueueTask(firstSend);
                rem = context[recvT->getContextId()].removeTask(recvT);
                assert(rem == true);
                backgroundQueueTask(recvT);
            }
            else
            {
                mpiRecvQ[currentRank][event->mpixf.comm_rank][event->mpixf.tag] = activeContech.activeTask();
                activeContech.createBasicBlockContinuation();
            }
        }
        else if (event->event_type == ct_event_roi)
        {
            Task* activeT = activeContech.activeTask();
            ct_tsc_t roiTime = event->roi.start_time - activeContech.timeOffset;
            activeT->setEndTime(roiTime);
            activeContech.createBasicBlockContinuation();
            attemptBackgroundQueueTask(activeT, activeContech);
            
            activeT = activeContech.activeTask();
            TaskId tid = activeT->getTaskId();
            if (roiEvent == false)
            {
                setROIStart(tid);
                printf("DEBUG - ROI Start - %lu - %lu\n", (uint64_t)tid, roiTime);
                roiEvent = true;
            }
            else
            {
                setROIEnd(tid);
                printf("DEBUG - ROI End - %lu - %lu\n", (uint64_t)tid, roiTime);
            }
        }
        // End switch block on event type

        // Free memory for the processed event
        EventLib::deleteContechEvent(event);
    }
    //displayContechEventDiagInfo();

    // TODO: for every context if endtime == 0, then join?
    
    if (DEBUG) printf("Processed %lu events.\n", eventCount);
    
    char* d = NULL;
    
    for (auto& p : context)
    {
        Context& c = p.second;
        
        //printf("%d\t%llx\t%llx\t%llx\n", p.first, c.timeOffset, c.startTime, c.endTime);
        
        for (auto t : c.tasks)
        {
            backgroundQueueTask(t.second);
        }
    }
    eventQ.printSpaceTime(totalCycles);
    
    pthread_mutex_lock(&taskQueueLock);
    noMoreTasks = true;
    pthread_cond_signal(&taskQueueCond);
    pthread_mutex_unlock(&taskQueueLock);
    {
        struct timeb tp;
        ftime(&tp);
        printf("MIDDLE_QUEUE: %d.%03d\n", (unsigned int)tp.time, tp.millitm);
    }
    pthread_join(backgroundT, (void**) &d);
    
    {
        struct timeb tp;
        ftime(&tp);
        printf("MIDDLE_END: %d.%03d\n", (unsigned int)tp.time, tp.millitm);
    }
    
    fclose(out);
    
    return 0;
}
