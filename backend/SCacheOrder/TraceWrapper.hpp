#ifndef TRACEWRAPPER_HPP
#define TRACEWRAPPER_HPP

#include <ct_file.h>
#include <TaskGraph.hpp>

#include <queue>
#include <map>
#include <vector>

struct MemReq{
    unsigned int ctid;
    bool isWrite;
    char numOfBytes;
    unsigned long int address;
    unsigned int bbid;
};

typedef struct _ctid_current_state
{
    bool blocked;               // is this task running or blocked?
    bool terminated;            // has this task terminated
    contech::ct_timestamp taskCurrTime;  // all events before this time have been processed
    contech::ct_timestamp taskRate;      // how many cycles does basic block take on average
    contech::Task* currentTask;          // pointer to task being processed
    contech::TaskId nextTaskId;          // next task id in this contech id
    contech::Task::basicBlockActionCollection::iterator currentBB;    //current basic block to next be processed
    contech::Task::basicBlockActionCollection currentBBCol;           //hold the basic block collection for the end iterator
} ctid_current_state, *pctid_current_state;

class MemReqContainer
{
public:
    contech::ct_timestamp reqTime;
    vector <contech::MemoryAction> mav;
    unsigned int bbid;
    unsigned int ctid;
    //unsigned int pushedOps;
    
    bool operator()( MemReqContainer &t1, MemReqContainer &t2)
    {
        return t1.reqTime > t2.reqTime;
    }
};

class TraceWrapper
{
public:
    TraceWrapper(char*);
    ~TraceWrapper();
    
    contech::Task* pauseTask;
    contech::ct_timestamp priorStart;
    contech::TaskGraph* tg;
    contech::ct_timestamp lastOpTime;
    std::priority_queue <MemReqContainer, vector<MemReqContainer>, MemReqContainer> memReqQ;
    map<contech::ContextId, ctid_current_state*> contechState;
    
    int populateQueue();
    contech::TaskId getSequenceTask(vector<contech::TaskId>& succ, contech::ContextId selfId);
    
    int getNextMemoryRequest(MemReqContainer&);
};

#endif