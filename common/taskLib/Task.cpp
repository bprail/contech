#include "Task.hpp"

using namespace std;
using namespace contech;

// TODO Remove default constructor
Task::Task()
{
}

Task::Task(TaskId taskId, task_type type)
{
    this->taskId = taskId;
    this->type = type;
    bbCount = 0;
}

bool Task::operator==(const Task& rhs) const
{
    return  taskId == rhs.taskId &&
            startTime == rhs.startTime &&
            endTime == rhs.endTime &&
            a == rhs.a &&
            s == rhs.s &&
            p == rhs.p &&
            type == rhs.type;
}

// The unique ID for this task
TaskId Task::getTaskId() const { return taskId; }

// The task ID for this task
SeqId Task::getSeqId() const { return taskId.getSeqId(); }

// The contech ID for this task
ContextId Task::getContextId() const { return taskId.getContextId(); }

// The absolute time when this task started/ended
ct_timestamp Task::getStartTime() const { return startTime; }
void Task::setStartTime(ct_timestamp time) { startTime = time; }
ct_timestamp Task::getEndTime() const { return endTime; }
void Task::setEndTime(ct_timestamp time) { endTime = time; }

// Append argument task to this task
void Task::appendTask(Task* app, vector<Task*>* app_t)
{
    // Tasks are the same type and this is pred to app
    assert(type == app->type &&
           (find(app->p.begin(), app->p.end(), taskId) != app->p.end()));
    a.insert(a.end(), app->a.begin(), app->a.end());
    bbCount += app->bbCount;
    s = app->s;
    
    // Now s.p = this
    if (app_t->empty()) return;
    for (auto it = app_t->begin(), et = app_t->end(); it != et; ++it)
    {
        auto t_rem = find((*it)->p.begin(), (*it)->p.end(), app->taskId);
        if (t_rem != (*it)->p.end())
        {
            (*t_rem) = taskId;
        }
    }
}

// Remove rem from the task graph
//   p must contain the Task for all of rem->p
//   s must contain the Task for all of rem->s
bool Task::removeTask(Task* rem, vector<Task*>* p, vector<Task*>* s)
{
    // First check if p and s vectors are equivalent
    if (rem->p.size() != p->size()) return false;
    if (p->size() != 0)
    {
        for (auto it = p->begin(), et = p->end(); it != et; ++it)
        {
            //cerr << "find : " << (*it)->taskId  << endl;
            if (find(rem->p.begin(), rem->p.end(), (*it)->taskId) == rem->p.end()) return false;
        }
    }
    
    if (rem->s.size() != s->size()) return false;
    if (s->size() != 0)
    {
        for (auto it = s->begin(), et = s->end(); it != et; ++it)
        {
            if (find(rem->s.begin(), rem->s.end(), (*it)->taskId) == rem->s.end()) return false;
        }
    }
    
    // With removing the node in the graph
    //   There are several possible reattachments:
    //   - All to all
    //   - Type to type
    //   - ID to ID
    // Let's assume that type to type represents the best preservation of the graph
    
    // To map directly p->s, the lists need to be the same size
    if (p->size() != s->size()) return false;
    
    // TODO: Store all updates and make them transactionally
    if (p->size() == 0) return true;
    for (auto it = p->begin(), et = p->end(); it != et; ++it)
    {
        bool match = false;
        for (auto itt = s->begin(), ett = s->end(); itt != ett; ++itt)
        {
            // Same type, let's roll
            if ((*it)->type == (*itt)->type)
            {
                auto p_rem = find((*it)->s.begin(), (*it)->s.end(), rem->taskId);
                auto t_rem = find((*itt)->p.begin(), (*itt)->p.end(), rem->taskId);
                if (p_rem == (*it)->s.end()) continue;
                if (t_rem == (*it)->p.end()) continue;
                
                // p->s = s
                // s->p = p
                // cerr << "Match: " << taskIdToString((*it)->taskId) << " <-> " << taskIdToString((*itt)->taskId) << "\n";
                *(p_rem) = (*itt)->taskId;
                *(t_rem) = (*it)->taskId;
                match = true;
                break;
            }
        }
        
        // Set of types in p, s are not equal
        if (match == false) return false;
    }
    
    // Task rem is no longer part of p's s, nor s's p
    return true;
}

// Record that a memop occurred in this task
void Task::recordMemOpAction(bool is_write, short pow_size, uint64 addr)
{
    MemoryAction mem;
    mem.type = is_write ? action_type_mem_write : action_type_mem_read;
    mem.pow_size = pow_size;
    mem.addr = addr;
    this->a.push_back(mem);
}

// Record that a malloc occurred in this task
void Task::recordMallocAction(uint64 addr, uint64 size)
{
    MemoryAction mem;
    mem.type = action_type_malloc;
    mem.addr = addr;
    this->a.push_back(mem);
    mem.type = action_type_size;
    mem.addr = size;
    this->a.push_back(mem);
}

// Record that a free occurred in this task
void Task::recordFreeAction(uint64 addr)
{
    MemoryAction mem;
    mem.type = action_type_free;
    mem.addr = addr;
    this->a.push_back(mem);
}

// Record that a basic block occurred in this task
void Task::recordBasicBlockAction(uint id)
{
    BasicBlockAction bb;
    bb.type = action_type_basicBlock;
    bb.basic_block_id = id;
    this->a.push_back(bb);
    bbCount++;
}

// Get all the actions that occurred in this task
vector<Action>& Task::getActions() { return a; }

// Get all the memOps (reads/writes) that occurred in this task
Task::memOpCollection Task::getMemOps() { return memOpCollection(a.begin(), a.end()); }

Task::memOpCollection::memOpCollection(){}
Task::memOpCollection::memOpCollection(vector<Action>::iterator f, vector<Action>::iterator e) : first(f), last(e)
{
    // Skip until the first memOp
    while (first != last && !first->isMemOp()) first++;
}
Task::memOpCollection::iterator Task::memOpCollection::begin() { return iterator(first, this); }
Task::memOpCollection::iterator Task::memOpCollection::end() { return iterator(last, this); }
uint Task::memOpCollection::size()
{
    uint size = 0;
    for (auto m : *this) size++;
    return size;
}

// Get all the memory actions that occurred in this task
Task::memoryActionCollection Task::getMemoryActions() { return memoryActionCollection(a.begin(), a.end()); }

Task::memoryActionCollection::memoryActionCollection(){}
Task::memoryActionCollection::memoryActionCollection(vector<Action>::iterator f, vector<Action>::iterator e) : first(f), last(e)
{
    // Skip until the first memory action
    while (first != last && !first->isMemoryAction()) first++;
}
Task::memoryActionCollection::iterator Task::memoryActionCollection::begin() { return iterator(first, this); }
Task::memoryActionCollection::iterator Task::memoryActionCollection::end() { return iterator(last, this); }
uint Task::memoryActionCollection::size()
{
    uint size = 0;
    for (auto m : *this) size++;
    return size;
}

// Get all the basic block actions that occurred in this task
Task::basicBlockActionCollection Task::getBasicBlockActions() { return basicBlockActionCollection(a.begin(), a.end()); }

Task::basicBlockActionCollection::basicBlockActionCollection(){}
Task::basicBlockActionCollection::basicBlockActionCollection(vector<Action>::iterator f, vector<Action>::iterator e) : first(f), last(e)
{
    // Skip until the first basic block action
    while (first != last && !first->isBasicBlockAction() ) first++;
}

Task::memoryActionCollection Task::basicBlockActionCollection::iterator::getMemoryActions()
{
    // Get an iterator that points to the next basic block
    iterator end = *this;
    end++;
    // Return a new collection of memory actions that starts with this block and ends at the next one
    return memoryActionCollection(it, end.it);
}

Task::memOpCollection Task::basicBlockActionCollection::iterator::getMemOps()
{
    // Get an iterator that points to the next basic block
    iterator end = *this;
    if (end.it != end.parent->last) end++;
    // Return a new collection of memOps that starts with this block and ends at the next one
    return memOpCollection(it, end.it);
}


Task::basicBlockActionCollection::iterator Task::basicBlockActionCollection::begin() { return iterator(first, this); }
Task::basicBlockActionCollection::iterator Task::basicBlockActionCollection::end() { return iterator(last, this); }
uint Task::basicBlockActionCollection::size()
{
    uint size = 0;
    for (auto b : *this) size++;
    return size;
}

vector<TaskId>& Task::getSuccessorTasks() { return s; }
void Task::addSuccessor(TaskId succ) { s.push_back(succ); }
vector<TaskId>& Task::getPredecessorTasks() { return p; }
void Task::addPredecessor(TaskId pred) { p.push_back(pred); }

task_type Task::getType() const { return type; }
void Task::setType(task_type e) { type = e; }

sync_type Task::getSyncType() const { return syncType; }
void Task::setSyncType(sync_type e) { syncType = e; }

// Deserialize a Task from a file
Task* Task::readContechTask(ct_file* in)
{
    Task* task = new Task();
        
    // Read in record length
    uint recordLength;
    ct_read(&recordLength, sizeof(uint), in);

    if (ct_eof(in)) { return NULL;}

    ct_read(&task->taskId, sizeof(TaskId), in);
    ct_read(&task->startTime, sizeof(ct_timestamp), in);
    ct_read(&task->endTime, sizeof(ct_timestamp), in);

    // Read size and data for a vector
    uint asize;
    task->bbCount = 0;
    ct_read(&asize, sizeof(uint), in);
    task->a.clear();
    task->a.reserve(asize);
    assert(task->a.capacity() >= asize);
    for (uint i = 0; i < asize; i++)
    {
        Action action;
        ct_read(&action.data, sizeof(uint64), in);
        task->a.push_back(action);
        if (action.isBasicBlockAction()) task->bbCount++;
    }

    // Read size and data for s vector
    uint ssize;
    ct_read(&ssize, sizeof(uint), in);
    task->s.reserve(ssize);
    for (uint i = 0; i < ssize; i++)
    {
        TaskId succ;
        ct_read(&succ, sizeof(TaskId), in);
        task->s.push_back(succ);
    }

    // Read size and data for p vector
    uint psize;
    ct_read(&psize, sizeof(uint), in);
    task->p.clear();
    task->p.reserve(psize);
    for (uint i = 0; i < psize; i++)
    {
        TaskId pred;
        ct_read(&pred, sizeof(TaskId), in);
        task->p.push_back(pred);
    }

    task_type typeInt;
    ct_read(&typeInt, sizeof(task_type), in);
    task->type = (task_type)typeInt;
    
    sync_type typeIntSync;
    ct_read(&typeIntSync, sizeof(sync_type), in);
    task->syncType = (sync_type)typeIntSync;
    
    uint64 fileOffset;
    ct_read(&fileOffset,sizeof(uint64),in);
    task->setFileOffset(fileOffset);
    
    return task;
}

// Serialize a Task to a file
size_t Task::writeContechTask(Task& task, ct_file* out)
{
    // Calculate record length
    uint asize = task.a.size();
    uint ssize = task.s.size();
    uint psize = task.p.size();

    uint recordLength =
        // Unique ID
        sizeof(TaskId) +
        // Start Time
        sizeof(ct_timestamp) +
        // Req Time
        sizeof(ct_timestamp) +
        // Size of action list
        sizeof(uint) +
        // action list
        asize * sizeof(uint64) +
        // Size of s list
        sizeof(uint) +
        // s list
        ssize * sizeof(uint64) +
        // Size of p list
        sizeof(uint) +
        // p list
        psize * sizeof(uint64) +
        // Type
        sizeof(task_type) +
        // Sync Type
        sizeof(sync_type) + 
        //fileOffset
        sizeof(uint64);

    // Record length
    ct_write(&recordLength, sizeof(uint), out);

    // Unique ID
    ct_write(&task.taskId, sizeof(TaskId), out);
    // Start Time
    ct_write(&task.startTime, sizeof(ct_timestamp), out);
    // Req Time
    ct_write(&task.endTime, sizeof(ct_timestamp), out);

    // Size of action list
    ct_write(&asize, sizeof(uint), out);
    // action list
    for (Action a : task.a)
    {
        ct_write(&a.data, sizeof(uint64), out);
    }

    // Size of s list
    ct_write (&ssize, sizeof(uint), out);
    // s list
    for (TaskId val : task.s)
    {
        ct_write(&val, sizeof(TaskId), out);
    }

    // Size of p list
    ct_write (&psize, sizeof(uint), out);
    // p list
    for (TaskId val : task.p)
    {
        ct_write(&val, sizeof(TaskId), out);
    }

    // Terminator Event
    ct_write(&task.type, sizeof(task_type), out);
    ct_write(&task.syncType, sizeof(sync_type), out);
    
    //File offset
    ct_write(&task.fileOffset,sizeof(uint64),out);
    
    //account for the recordLength itself with the addition
    return recordLength + sizeof(uint);
}

uint64 Task::getFileOffset() const {
    return this->fileOffset;
}
void Task::setFileOffset(uint64 offset){
    this->fileOffset = offset;
}

/*
string Task::taskTypeToString(task_type taskType)
{
    ostringstream out;
    switch (taskType)
    {
        case task_type_basic_blocks:
            out << "BasicBlocks";
            break;
        case task_type_sync:
            out << "Sync";
            break;
        case task_type_barrier:
            out << "Barrier";
            break;
        case task_type_create:
            out << "Create";
            break;
        case task_type_join:
            out << "Join";
            break;
        default:
            out << "Unknown";
            break;
    }
    return out.str();
}
*/

string Task::toString() const {
    ostringstream out;

    out << "taskId:" << taskId << endl;
    out << "startTime:" << startTime << endl;
    out << "endTime:" << endTime << endl;
    out << "Type:" << type << endl;
    out << "fileOffset:" << fileOffset << endl;

    out << "a:";
    for (Action action : a)
    {
        out << action.toString();
    }
    out << endl;

    out << "s:";
    for (TaskId task : s)
    {
        out << task << ",";
    }
    out << endl;

    out << "p:";
    for (TaskId task : p)
    {
        out << task << ",";
    }
    out << endl;

    return out.str();
}

