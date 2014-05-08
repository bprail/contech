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
#include <deque>
#include <algorithm>
#include <inttypes.h>

#define TASK_GRAPH_VERSION 4314

using namespace std;
namespace contech {

class TaskGraph
{
private:
    ct_file* inputFile;
    TaskGraphInfo* tgi;
    
    // Use an index to find each task in the graph
    map<TaskId, uint64> taskIdx;
    vector<uint64> taskOrder;
    vector<uint64>::iterator nextTask;
    
    // Privately, attempt to read a task graph info struct
    TaskGraphInfo* readTaskGraphInfo();
    void initTaskIndex(unsigned long long);
    
    TaskGraph(ct_file*);

public:
    static TaskGraph* initFromFile(char*);
    static TaskGraph* initFromFile(ct_file*);
    
    Task* getNextTask();
    Task* getTaskById(TaskId id);
    
    // These calls are deprecated and will be removed soon...
    Task* readContechTask();
    Task* getContechTask(TaskId);
    
    TaskGraphInfo* getTaskGraphInfo();
    ~TaskGraph();
};

}

#endif