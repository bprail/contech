#include "TaskGraph.hpp"

using namespace contech;

//
// Cannot call constructor directly, but wrap with "factory"
//
TaskGraph* TaskGraph::initFromFile(char* fname)
{
    ct_file* f = create_ct_file_r(fname);
    
    if (f == NULL) { return NULL;}
    else {return new TaskGraph(f);}
}

TaskGraph* TaskGraph::initFromFile(ct_file* f)
{
    if (f == NULL) { return NULL;}
    else {return new TaskGraph(f);}
}

//
// Construct a task graph from the file
//
TaskGraph::TaskGraph(ct_file* f)
{
    uint version = 0;
    unsigned long long taskIndexOffset = 0;
    inputFile = f;
    
    // First is the version number
    ct_read(&version, sizeof(uint), f);
    if (false /*sizeof(uint) != ct_read(&version, sizeof(uint), f)*/)
    {
        fprintf(stderr, "TASK GRAPH - Error reading from input file\n");
        return;
    }
    
    if (version < TASK_GRAPH_VERSION)
    {
        fprintf(stderr, "TASK GRAPH - Warning version number is %u, expected %u\n", version, TASK_GRAPH_VERSION);
    }
    
    // Next is the location of the taskIndex in the file
    ct_read(&taskIndexOffset, sizeof(unsigned long long), f);
    
    // Then comes the taskGraphInfo structure
    tgi = readTaskGraphInfo();
    
    // Now skip to the index
    initTaskIndex(taskIndexOffset);
}

//
// Get next task from the order
//
Task* TaskGraph::getNextTask()
{
    if (nextTask == taskIdx.end()) return NULL;
    
    ct_seek(inputFile, nextTask->second);
    ++nextTask;
    
    return Task::readContechTask(inputFile);
}

//
// Request tasks in order until ID is found
//
Task* TaskGraph::getTaskById(TaskId id)
{
    auto it = taskIdx.find(id);
    
    if (it == taskIdx.end()) return NULL;
    
    ct_seek(inputFile, it->second);
    
    return Task::readContechTask(inputFile);
}

Task* TaskGraph::readContechTask()
{
    if (inputFile == NULL) return NULL;
    return getNextTask();
}

Task* TaskGraph::getContechTask(TaskId tid)
{
    if (inputFile == NULL) return NULL;
    return getTaskById(tid);
}

TaskGraphInfo* TaskGraph::getTaskGraphInfo()
{
    return tgi;
}

void TaskGraph::initTaskIndex(unsigned long long off)
{
    if (0 != ct_seek(inputFile, off))
    {
        fprintf(stderr, "Failed to seek to specified offset for Task Graph Index - %lld\n", off);
        return;
    }
    printf("At %lld, ready to read index\n", off);
    
    uint64 taskCount;
    ct_read(&taskCount, sizeof(uint64), inputFile);
    printf("Tasks in index - %llu\n", taskCount);
    
    for (uint i = 0; i < taskCount; i++)
    {
        TaskId tid;
        unsigned long long pos;
        
        ct_read(&tid, sizeof(TaskId), inputFile);
        ct_read(&pos, sizeof(unsigned long long), inputFile);
        
        // We expect that the index comes after every task in the file
        assert(pos < off);
        
        taskIdx[tid] = pos;
    }
    nextTask = taskIdx.begin();
}

TaskGraphInfo* TaskGraph::readTaskGraphInfo()
{
    // Task Graph Info follows the version number + task index offset
    //   assert(inputFile is at offset 4 + 8)
    
    TaskGraphInfo* tTgi = new TaskGraphInfo(inputFile);

    return tTgi;
}