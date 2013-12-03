#include "../../common/taskLib/ct_file.h"
#include "../../common/taskLib/Task.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <queue>
#include <deque>
#include <string>

using namespace std;
using namespace contech;

#define MAX_THREADS 32

typedef struct _ctid_harmony_state
{
    bool blocked;               // is this task running or blocked?
    bool terminated;            // has this task terminated
    ct_timestamp taskCurrTime;  // all events before this time have been processed
    ct_timestamp taskRate;      // how many cycles does basic block take on average
    Task* currentTask;       // pointer to task being processed
    TaskId nextTaskId;          // next task id in this contech id
    Task::basicBlockActionCollection::iterator currentBB;    //current basic block to next be processed
    Task::basicBlockActionCollection currentBBCol;           //hold the basic block collection for the end iterator
} ctid_harmony_state, *pctid_harmony_state;

// support structures to enable ordered access and get task by id
deque<Task*> taskList;
map<TaskId, Task*> taskMap;

//
// Get next task from the order
//
Task* getNextTask(ct_file* in)
{
    // Task list holds the pending ordered traversal, if empty, go to file
    if (taskList.empty())
    {
        //fprintf(stderr, "From file\n");
        return Task::readContechTask(in);
    }
    else
    {
        //fprintf(stderr, "From list %d\n", taskList.size());
        Task* t = taskList.front();
        taskList.pop_front();
        taskMap.erase(t->getTaskId());
        return t;
    }
    
    return NULL;
}

//
// Request tasks in order until ID is found
//
Task* getTaskById(ct_file* in, TaskId id)
{
    Task* t = NULL;
    auto f = taskMap.find(id);
    ct_timestamp st;
    
    // Is the requested task in the map?
    if (f != taskMap.end())
    {
        //fprintf(stderr, "From Map %s\n", Task::taskIdToString(id).c_str());
        t = f->second;
        return t;
    }
    
    // Process tasks from file until the requested task is found
    while ((t = Task::readContechTask(in)) != NULL)
    {
        TaskId tid = t->getTaskId();
        //fprintf(stderr, "  Have %s want %s tid->cont: %ld\n", Task::taskIdToString(tid).c_str(), Task::taskIdToString(id).c_str(), t->getContinuationTaskId());
        
        // Assert that tasks are in order
        if (!taskList.empty())
            assert(taskList.back()->getStartTime() <= t->getStartTime());
            
        // Add task to ordered list and to map by id
        taskList.push_back(t);
        taskMap[tid] = t;
        
        if (tid == id)
        {
            return t;
        }
        else
        {

        }
    }

    return NULL;
}

//
// Scan the vector of successor tasks and find the next in sequence
//
TaskId getSequenceTask(vector<TaskId>& succ, ContextId selfId)
{
    TaskId possible_succ = 0;

    for (auto i : succ)
    {
        if (i.getContextId() == selfId)
        {
            if (possible_succ == 0 ||
                i < possible_succ)
            {
                possible_succ = i;
            }
        }
    }
    
    return possible_succ;
}

int main(int argc, char const *argv[])
{
    //input check
    if(argc != 2){
        cout << "Usage: " << argv[0] << " taskGraphInputFile" << endl;
        exit(1);
    }

    ct_file* taskGraphIn  = create_ct_file_r(argv[1]);
    if(taskGraphIn == NULL) { //getUncompressedHandle(taskGraphIn) == NULL){
        cerr << "ERROR: Couldn't open input file" << endl;
        exit(1);
    }

    Task* currentTask;
    ct_timestamp oldStart = 0;
    int runningThreads = 0;
    map<ContextId, ctid_harmony_state*> contechState;
    vector<vector<int> > bbCount;

    // foreach task in graph
    //   Mark all basic blocks before this task has started based on runningThreads
    //   Then update existing tasks if complete
    //   Then include new task into state
    while (currentTask = getNextTask(taskGraphIn))
    {
        TaskId ctui = currentTask->getTaskId();
        ContextId ctci = currentTask->getContextId();
        
        ct_timestamp start = currentTask->getStartTime();
        ct_timestamp req = currentTask->getEndTime();
        
        if (start < oldStart)
        {
            fprintf(stderr, "(%d) Task: %s is older than processed tasks - %d\n", start, ctui.toString().c_str(), oldStart);
        }
        else
        {
            oldStart = start;
            fprintf(stderr, "(%d) Task: %s to %d\n", start, ctui.toString().c_str(), req);
        }
        
        // Iterate through every basic block, older than start
        for (auto hs_b = contechState.begin(), hs_e = contechState.end(); hs_b != hs_e; ++hs_b)
        {
            ctid_harmony_state* tempState = (hs_b->second);
            if (tempState->terminated == true) continue;
            Task* t = tempState->currentTask;
            ct_timestamp tempCurrent = tempState->taskCurrTime;
            ct_timestamp tempRate = tempState->taskRate;
            auto f = tempState->currentBB;
            string s = t->getTaskId().toString();
            
            //
            // tempRate = 0 -> no basic blocks or currentTask->time == nextTask->time
            //   Still, process the basic blocks
            //
            for (auto e = tempState->currentBBCol.end(); 
                 (tempCurrent <= start) && (f != e); ++f)
            {
                BasicBlockAction tbb = *f;
                
                // >= as max index is size - 1
                if (tbb.basic_block_id >= bbCount.size())
                {
                    //fprintf(stderr, "Resize %d < %ld\n", bbCount.size(), tbb.basic_block_id);
                    // Usually, if the basic block id is dramatically beyond the current ids
                    //   this is indicative of an error.
                    if (tbb.basic_block_id > (bbCount.size() * 100) &&
                        bbCount.size() > 10)
                    {
                        fprintf(stderr, "Is %ld in:\n", tbb.basic_block_id);
                        fprintf(stderr, "%s\n", t->toString().c_str());
                    }
                    
                    // Resize the bbcount vector and use a max threads vector as the initial value
                    // This way, there is no check that running threads is < current size; however,
                    // if running threads ever exceeds max threads, then ... recompile
                    vector<int> tVec;
                    tVec.resize(MAX_THREADS, 0);
                    bbCount.resize((tbb.basic_block_id + 32) & 0xffffffe0, tVec);
                }
                //fprintf(stderr, "%s: %d %d (%d ?=? %d) tr = %d / %d\n", s.c_str(), tbb.basic_block_id, runningThreads, tempCurrent, start, tempRate, t->getBBCount());
                bbCount[tbb.basic_block_id][runningThreads] ++;
                tempCurrent += tempRate;
            }
            
            // Should the task switch always be from currentTask == next?
            //   o.w. The last basic block "spans" start
            if (tempCurrent < start ||
                ctui == tempState->nextTaskId)
            {
                bool tBlock = tempState->blocked;
                
                //
                // Termination condition if the contech IDs change
                //   Or 0 is the successor task
                //
                if (t->getContextId() != tempState->nextTaskId.getContextId()
                    || tempState->nextTaskId == 0)
                {
                    delete t;
                    tempState->terminated = true;
                    if  (tBlock == false) runningThreads --;
                    continue;
                }
                
                // Is the new task running or doing something sychronizing?
                if (ctui == tempState->nextTaskId)
                {
                    tempState->currentTask = currentTask;
                }
                else
                {
                    tempState->currentTask = getTaskById(taskGraphIn, (tempState->nextTaskId));
                }
                
                if (tempState->currentTask->getType() == task_type_basic_blocks)
                    tempState->blocked = false;
                else
                    tempState->blocked = true;
                
                // If there is no continuation, then this task has terminated
                //tempState->nextTask = getTaskById(taskGraphIn, (tempState->currentTask)->getContinuationTaskId());
                
                /*if (tempState->nextTask == NULL)
                {
                    delete t;
                    tempState->terminated = true;
                    if  (tBlock == false) runningThreads --;
                    continue;
                }*/
                tempState->taskCurrTime = tempState->currentTask->getStartTime();
                tempState->currentBBCol = tempState->currentTask->getBasicBlockActions();
                tempState->currentBB = tempState->currentBBCol.begin();
                
                // If the last task was blocked, increment running in case the next one is not
                if (tBlock)
                    runningThreads++;
                    
                // If the task is blocked, then it is not running.
                //  o.w. compute the task rate for this next task
                if (tempState->blocked == true)
                {
                    runningThreads --;
                    tempState->taskRate = 0;
                }
                else
                {
                    int bbc = tempState->currentTask->getBBCount();
                    tempState->taskRate = (tempState->currentTask->getEndTime() - tempState->taskCurrTime/* + bbc - 1*/) 
                                            / (bbc);                
                    //fprintf(stderr, "%ld - %ld / %d\n", tempState->nextTask->getStartTime(), tempState->taskCurrTime, (tempState->currentTask->getBBCount()));
                    if (tempState->taskRate == 0)
                    {
                      //  tempState->taskRate = 1;
                        
                    }
                }
                assert(runningThreads < MAX_THREADS);
                
                delete t;
            }
            else
            {
                tempState->currentBB = f;
                tempState->taskCurrTime = tempCurrent;
            }
        }  // end of foreach task in contechState
        
        //
        // If ctci is not in contechState, then it is a new contech
        //   TODO: Due to barriers, it is possible that we'll need to "unterminate" some states
        //
        if (contechState.find(ctci) == contechState.end())
        {
            ctid_harmony_state* tempState = new ctid_harmony_state;
            
            //fprintf(stderr, "%d\n", ctci);
            assert(runningThreads < MAX_THREADS);
            
            tempState->terminated = false;
            tempState->taskCurrTime = start;
            tempState->currentBBCol = currentTask->getBasicBlockActions();
            tempState->currentBB = tempState->currentBBCol.begin();
            tempState->currentTask = currentTask;
            //tempState->nextTask = getTaskById(taskGraphIn, currentTask->getContinuationTaskId());
            
            if (currentTask->getType() == task_type_basic_blocks)
            {
                tempState->blocked = false;
                runningThreads++;
                tempState->taskRate = (tempState->currentTask->getEndTime() - start) / (currentTask->getBBCount());
                if (tempState->taskRate == 0) {
                   // tempState->taskRate = 1;
                    //fprintf(stderr, "%ld - %ld / %d\n", tempState->nextTask->getStartTime(), tempState->taskCurrTime, (tempState->currentTask->getBBCount()));
                }
            }
            else
            {
                tempState->blocked = true;
                tempState->taskRate = 0;
            }
            contechState[ctci] = tempState;
        }
    }

    // TODO: Process any remaining tasks that have not terminated
    
    // Print the basic block counts to csv format
    for (auto i = bbCount.begin(), e = bbCount.end(); i != e; ++i)
    {
        for (auto it = (*i).begin(), et = (*i).end(); it != et; ++it)
        {
            fprintf(stdout, "%d,", (*it));
        }
        // Print an extra "0", to follow the final comma       
        fprintf(stdout, "0\n");
    }

    close_ct_file(taskGraphIn);
    return 0;
}
