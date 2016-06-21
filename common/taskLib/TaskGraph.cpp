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

TaskGraph* TaskGraph::initFromFile(const char* fname)
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

TaskGraph* TaskGraph::initFromFile(FILE* f)
{
    if (f == NULL) { return NULL;}
    else {return new TaskGraph(f);}
}

//
// Construct a task graph from the file
//
TaskGraph::TaskGraph(ct_file* f)
{
    TaskGraph(getUncompressedHandle(f));
}

TaskGraph::TaskGraph(FILE* f)
{
    uint version = 0;
    uint64 taskIndexOffset = 0;
    inputFile = f;
    
    // This is to ensure the file is at the start
    fseek(f, 0, SEEK_SET);
    
    // First is the version number
    if (sizeof(uint32_t) != ct_read(&version, sizeof(uint32_t), f))
    {
        fprintf(stderr, "TASK GRAPH - Error reading from input file\n");
        return;
    }
    
    if (version != TASK_GRAPH_VERSION)
    {
        fprintf(stderr, "TASK GRAPH - Warning version number is %u, expected %u\n", version, TASK_GRAPH_VERSION);
    }
    
    // Next is the location of the taskIndex in the file
    ct_read(&taskIndexOffset, sizeof(uint64), f);
    ct_read(&ROIStart, sizeof(TaskId), f);
    ct_read(&ROIEnd, sizeof(TaskId), f);
    
    // Then comes the taskGraphInfo structure
    tgi = readTaskGraphInfo();
    
    // Now skip to the index
    initTaskIndex(taskIndexOffset);
}

TaskGraph::~TaskGraph()
{
    delete tgi;
}

//
// Get next task from the order
//
Task* TaskGraph::getNextTask()
{
    if (nextTask == taskOrder.end()) return NULL;
    
    //if ((e = ct_lock(inputFile))) return NULL;
    
    fseek(inputFile, *nextTask, SEEK_SET);
    ++nextTask;
    
    return Task::readContechTaskUnlock(inputFile);
}

void TaskGraph::resetTaskOrder()
{
    nextTask = taskOrder.begin();
}

//
// Sets the specified TaskId as the current task
//   getNextTask will retrieve the next task starting with TaskId
//
void TaskGraph::setTaskOrderCurrent(TaskId tid)
{
    uint64_t tidPos = taskIdx[tid];
    while (nextTask != taskOrder.end() &&
           *nextTask != tidPos) {++nextTask;}
}

//
// Request tasks in order until ID is found
//
Task* TaskGraph::getTaskById(TaskId id)
{
    auto it = taskIdx.find(id);
    
    if (it == taskIdx.end()) return NULL;
    
    //if (ct_lock(inputFile)) return NULL;
    fseek(inputFile, it->second, SEEK_SET);
    
    return Task::readContechTaskUnlock(inputFile);
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

void TaskGraph::initTaskIndex(uint64 off)
{
    if (0 != fseek(inputFile, off, SEEK_SET))
    {
        fprintf(stderr, "Failed to seek to specified offset for Task Graph Index - %lu\n", off);
        return;
    }
    //printf("At %lld, ready to read index\n", off);
    
    uint64 taskCount;
    ct_read(&taskCount, sizeof(uint64), inputFile);
    //printf("Tasks in index - %llu\n", taskCount);
    
    set<ContextId> uniqContexts;
    
    for (uint i = 0; i < taskCount; i++)
    {
        TaskId tid;
        uint64 pos;
        
        ct_read(&tid, sizeof(TaskId), inputFile);
        ct_read(&pos, sizeof(uint64), inputFile);
        
        //printf("%s at %llu\n", tid.toString().c_str(), pos);
        
        // We expect that the index comes after every task in the file
        assert(pos < off);
        // Every tid should only exist once in the index
        assert(taskIdx.find(tid) == taskIdx.end());
        taskIdx[tid] = pos;
        taskOrder.push_back(pos);
        uniqContexts.insert(tid.getContextId());
    }
    nextTask = taskOrder.begin();
    numOfContexts = uniqContexts.size();
}

TaskGraphInfo* TaskGraph::readTaskGraphInfo()
{
    // Task Graph Info follows the version number + task index offset
    //   assert(inputFile is at offset 4 + 8)
    
    TaskGraphInfo* tTgi = new TaskGraphInfo();
    tTgi->initTaskGraphInfo(inputFile);

    return tTgi;
}

unsigned int TaskGraph::getNumberOfTasks()
{
    return taskOrder.size();
}

unsigned int TaskGraph::getNumberOfContexts()
{
    return numOfContexts;
}

TaskId TaskGraph::getROIStart()
{
    return ROIStart;
}

TaskId TaskGraph::getROIEnd()
{
    return ROIEnd;
}
