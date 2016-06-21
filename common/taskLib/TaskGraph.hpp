#ifndef TASK_GRAPH_HPP
#define TASK_GRAPH_HPP

#include "Task.hpp"
#include "TaskGraphInfo.hpp"
#include "TaskId.hpp"
#include "Action.hpp"
#include "ct_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <sstream>
#include <assert.h>
#include <vector>
#include <map>
#include <set>
#include <deque>
#include <algorithm>
#include <inttypes.h>

#define TASK_GRAPH_VERSION 4315

using namespace std;
namespace contech {

class TaskGraph
{
private:
    FILE* inputFile;
    TaskGraphInfo* tgi;
    
    // Use an index to find each task in the graph
    //   TaskId -> position in file
    map<TaskId, uint64> taskIdx;
    
    // Store the positions of each task
    vector<uint64> taskOrder;
    vector<uint64>::iterator nextTask;
    
    TaskId ROIStart;
    TaskId ROIEnd;
    
    unsigned int numOfContexts;
    
    // Privately, attempt to read a task graph info struct
    TaskGraphInfo* readTaskGraphInfo();
    void initTaskIndex(uint64);
    
    TaskGraph(ct_file*);
    TaskGraph(FILE*);

public:
    static TaskGraph* initFromFile(char*);
    static TaskGraph* initFromFile(const char*);
    static TaskGraph* initFromFile(ct_file*);
    static TaskGraph* initFromFile(FILE*);
    
    Task* getNextTask();
    Task* getTaskById(TaskId id);
    void setTaskOrderCurrent(TaskId tid);
    void resetTaskOrder();
    
    unsigned int getNumberOfTasks();
    unsigned int getNumberOfContexts();
    
    // These calls are deprecated and will be removed soon...
    Task* readContechTask();
    Task* getContechTask(TaskId);
    
    TaskId getROIStart();
    TaskId getROIEnd();
    
    TaskGraphInfo* getTaskGraphInfo();
    ~TaskGraph();
};

}

#endif