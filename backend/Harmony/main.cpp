#include "../../common/taskLib/ct_file.h"
#include "../../common/taskLib/TaskGraph.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <queue>
#include <deque>
#include <string>

using namespace std;
using namespace contech;

#define MAX_THREADS 1024

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
        else
        {
            //printf("%d\t", i.getContextId());
        }
    }
    
    //printf("%d -> %s\n", selfId, possible_succ.toString().c_str());
    
    return possible_succ;
}

int main(int argc, char const *argv[])
{
    //input check
    if(argc != 2){
        cout << "Usage: " << argv[0] << " taskGraphInputFile" << endl;
        exit(1);
    }

    FILE* taskGraphIn  = fopen(argv[1], "rb");
    if (taskGraphIn == NULL) {
        cerr << "ERROR: Couldn't open input file" << endl;
        exit(1);
    }

    ct_timestamp oldStart = 0, theEnd;
    int runningThreads = 0, nextRunningCount = 0, maxRunning = 0;
    map<ContextId, ctid_harmony_state*> contechState;
    vector<vector<int> > bbCount;
    vector<ct_timestamp> hist;
    hist.resize(MAX_THREADS);

    // foreach task in graph
    //   Mark all basic blocks before this task has started based on runningThreads
    //   Then update existing tasks if complete
    //   Then include new task into state
    TaskGraph* tg = TaskGraph::initFromFile(taskGraphIn);
    if (tg == NULL) {}

    while(Task* currentTask = tg->readContechTask())
    {
        TaskId ctui = currentTask->getTaskId();
        ContextId ctci = currentTask->getContextId();
        
        ct_timestamp start = currentTask->getStartTime();
        ct_timestamp req = currentTask->getEndTime();
        theEnd = req;
        
        runningThreads = nextRunningCount;
        if (runningThreads > maxRunning)
            maxRunning = runningThreads;
        
        // Note - This is valid behaviour for a subsequent task to "start" earlier
        //   For example, the start of a sync task is when the attempt to acquire a lock is
        //     made.  The release of a lock could be made later than this attempt, and would
        //     then have a later timestamp.
        if (start < oldStart)
        {
            //fprintf(stderr, "(%d) Task: %s is older than processed tasks - %d\n", start, ctui.toString().c_str(), oldStart);
        }
        else
        {
            hist[runningThreads] += (start - oldStart);
            
            //fprintf(stdout, "%llu, %llu, %d\n", oldStart, start, runningThreads);
            oldStart = start;
            //fprintf(stderr, "(%d) Task: %s to %d\n", start, ctui.toString().c_str(), req);
        }
        
        ct_timestamp nextParChange = start;
        #if 0
        do {
            ctid_harmony_state* oldestChange = NULL;
            unsigned int checkVal = 0;
            nextParChange = start;
            for (auto hs_b = contechState.begin(), hs_e = contechState.end(); hs_b != hs_e; ++hs_b)
            {
                ctid_harmony_state* tempState = (hs_b->second);
                if (tempState->terminated == true || tempState->blocked == true) continue;
                Task* t = tempState->currentTask;
                
                if (t->getStartTime() < start) checkVal++;

                ct_timestamp tempChange = t->getEndTime();
                if (tempChange < nextParChange && tempChange > oldStart)
                {
                    nextParChange = tempChange;
                    oldestChange = tempState;
                }
            }
            
            if (oldestChange != NULL)
            {
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
                    if (!tempState->blocked)
                    {
                        for (auto e = tempState->currentBBCol.end(); 
                             (tempCurrent <= nextParChange) && (f != e); ++f)
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
                            //if (runningThreads > 1)
                                bbCount[tbb.basic_block_id][runningThreads] ++;
                            tempCurrent += tempRate;
                        }
                        tempState->taskCurrTime = tempCurrent;
                        tempState->currentBB = f;
                    }
                }
            
                oldestChange->blocked = true;
                hist[runningThreads] += (nextParChange - oldStart);
                //fprintf(stdout, "%llu, %llu, %d\n", oldStart, nextParChange, runningThreads);
                oldStart = nextParChange;
                runningThreads --;
            }
        } while (nextParChange < start);
        #endif
        nextRunningCount = runningThreads;
        
        //fprintf(stdout, "%llu, %d, %s, %c\n", start, runningThreads, ctui.toString().c_str(),
        //        ((currentTask->getType() == task_type_basic_blocks)?'W':'b'));
        
        bool matchContext = false;
        
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
            if (!tempState->blocked)
            {
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
                        #if 0
                        if (tbb.basic_block_id > (bbCount.size() * 100) &&
                            bbCount.size() > 10)
                        {
                            fprintf(stderr, "Is %ld in:\n", tbb.basic_block_id);
                            fprintf(stderr, "%s\n", t->toString().c_str());
                        }
                        #endif
                        
                        // Resize the bbcount vector and use a max threads vector as the initial value
                        // This way, there is no check that running threads is < current size; however,
                        // if running threads ever exceeds max threads, then ... recompile
                        vector<int> tVec;
                        tVec.resize(MAX_THREADS, 0);
                        bbCount.resize((tbb.basic_block_id + 32) & 0xffffffe0, tVec);
                    }
                    //fprintf(stderr, "%s: %d %d (%d ?=? %d) tr = %d / %d\n", s.c_str(), tbb.basic_block_id, runningThreads, tempCurrent, start, tempRate, t->getBBCount());
                    
                    //if (runningThreads > 1)
                        bbCount[tbb.basic_block_id][runningThreads] ++;
                    tempCurrent += tempRate;
                }
            }
            
            
            
            {
                /*if (ctui.getContextId() == tempState->nextTaskId.getContextId())
                {
                    fprintf(stdout, "TaskId: %s -> %s\n", ctui.toString().c_str(), tempState->nextTaskId.toString().c_str());
                }*/
                if (tempState->blocked != true && f == tempState->currentBBCol.end())
                {
                    tempState->blocked = true;
                    nextRunningCount --;
                    if (t->getContextId() != tempState->nextTaskId.getContextId()
                        || tempState->nextTaskId == 0)
                    {
                        //fprintf(stderr, "Delete(%d) -- %s\n", __LINE__, t->getTaskId().toString().c_str());
                        delete t;
                        tempState->terminated = true;
                        tempState->currentTask = NULL;
                        continue;
                    }
                }
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
            // If there is no continuation, then this task has terminated
            tempState->nextTaskId = getSequenceTask((tempState->currentTask)->getSuccessorTasks(), 
                                                    tempState->currentTask->getContextId());
            
            if (currentTask->getType() == task_type_basic_blocks)
            {
                tempState->blocked = false;
                nextRunningCount++;
                tempState->taskRate = (tempState->currentTask->getEndTime() - start) / (1 + currentTask->getBBCount());
                //fprintf(stdout, "%ld - %ld / %d\n", tempState->currentTask->getEndTime(), tempState->taskCurrTime, (tempState->currentTask->getBBCount()));
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
        else
        {
            // Should the task switch always be from currentTask == next?
            //   o.w. The last basic block "spans" start
            ctid_harmony_state* tempState = contechState.find(ctci)->second;
            Task* t = tempState->currentTask;
            if (ctci == tempState->currentTask->getContextId())
            {
                bool tBlock = tempState->blocked;
                matchContext = true;
                
                
                //
                // Termination condition if the contech IDs change
                //   Or 0 is the successor task
                //
                // In this, the current task is complete, so a 0 indicates no
                //   successor task available to replace the current task.
                if (false && t->getContextId() != tempState->nextTaskId.getContextId()
                    /*|| tempState->nextTaskId == 0*/)
                {
                    delete t;
                    tempState->terminated = true;
                    if  (tBlock == false) nextRunningCount --;
                    continue;
                }
                
                if (t->getContextId() != tempState->nextTaskId.getContextId())
                {
                    fprintf(stderr, "%s != %s @ %s\n", t->getTaskId().toString().c_str(),
                                                  tempState->nextTaskId.toString().c_str(),
                                                  ctui.toString().c_str());
                }
                
                // Is the new task running or doing something synchronizing?
                if (true/*ctui == tempState->nextTaskId*/)
                {
                    tempState->currentTask = currentTask;
                }
                else
                {
                    tempState->currentTask = tg->getContechTask(tempState->nextTaskId);
                }
                
                if (tempState->currentTask->getType() == task_type_basic_blocks)
                    tempState->blocked = false;
                else
                    tempState->blocked = true;
                
                // If there is no continuation, then this task has terminated
                tempState->nextTaskId = getSequenceTask((tempState->currentTask)->getSuccessorTasks(), 
                                                        tempState->currentTask->getContextId());
                
                tempState->taskCurrTime = tempState->currentTask->getStartTime();
                tempState->currentBBCol = tempState->currentTask->getBasicBlockActions();
                tempState->currentBB = tempState->currentBBCol.begin();
                
                // If the last task was blocked, increment running in case the next one is not
                if (tBlock)
                    nextRunningCount++;
                    
                // If the task is blocked, then it is not running.
                //  o.w. compute the task rate for this next task
                if (tempState->blocked == true)
                {
                    nextRunningCount --;
                    tempState->taskRate = 0;
                    //fprintf(stdout, "%llu, d\n", start);
                }
                else
                {
                    int bbc = 1 + tempState->currentTask->getBBCount();
                    tempState->taskRate = (tempState->currentTask->getEndTime() - tempState->taskCurrTime/* + bbc - 1*/) 
                                            / (bbc);                
                    //fprintf(stdout, "%ld - %ld / %d\n", tempState->currentTask->getEndTime(), tempState->taskCurrTime, (tempState->currentTask->getBBCount()));
                    if (tempState->taskRate == 0)
                    {
                        //tempState->taskRate = 1;
                        
                    }
                }
                assert(runningThreads < MAX_THREADS);
                
                //if (runningThreads == nextRunningCount)
                //fprintf(stdout, "\t%d -> %d\t%d -> %d\n", t->getType(), currentTask->getType(), runningThreads, nextRunningCount);
                delete t;
            }
        }
    }
    delete tg;
    // TODO: Process any remaining tasks that have not terminated
    
    ct_timestamp sum = theEnd - 0, tSum;
    int i = 0;
    tSum = sum;
    for (auto it = hist.begin(), et = hist.end(); it != et; ++it, ++i)
    {
        if (i == 0) continue;
        //hist[i] = (*it) / i;
        tSum -= hist[i];
    }
    //printf("%llu\t%d\n", hist[0], runningThreads);
    hist[0] = tSum;
    int threadCount = 0;
    for (auto it = hist.begin(), et = hist.end(); it != et; ++it)
    {
        fprintf(stdout, "%f, ", 100.0 * (((double)*it) / (double)sum));
        threadCount++;
        if (threadCount > maxRunning)
            break;
    }
    fprintf(stdout, "\n");
    
    // Print the basic block counts to csv format
    for (auto i = bbCount.begin(), e = bbCount.end(); i != e; ++i)
    {
        threadCount = 0;
        for (auto it = (*i).begin(), et = (*i).end(); it != et; ++it)
        {
            fprintf(stdout, "%d,", (*it));
            threadCount++;
            if (threadCount > maxRunning)
                break;
        }
        // Print an extra "0", to follow the final comma       
        fprintf(stdout, "0\n");
    }

    fclose(taskGraphIn);
    return 0;
}
