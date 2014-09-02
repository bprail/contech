#ifndef TASK_HPP
#define TASK_HPP

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
#include <algorithm>
#include <cinttypes>

using namespace std;
namespace contech {

typedef uint32_t uint; // Assumed to be 32 bits
typedef uint64_t uint64; // Assumed to be 64 bits
typedef uint64 ct_timestamp;
enum task_type { task_type_basic_blocks = 0, task_type_sync, task_type_barrier, task_type_create, task_type_join};
enum sync_type { sync_type_unknown = 0, sync_type_lock, sync_type_condition_variable, sync_type_user_defined};

class TaskGraph;

class Task
{
friend class TaskGraph;
protected:
    static Task* readContechTaskUnlock(ct_file* in);

private:

    TaskId taskId = 0;
    ct_timestamp startTime = 0;
    ct_timestamp endTime = 0;

    // Internal list of actions (memOp's, mallocs, frees, and basic blocks)
    vector<Action> a;
    // Internal list of successor tasks
    vector<TaskId> s;
    // Internal list of predecessor tasks
    vector<TaskId> p;

    // The type of event that this task represents
    task_type type;
    sync_type syncType;
        
    // file offset (in bytes) in a taskgraph file. Enables constant time task access
    //uint64 fileOffset;

    int bbCount;
    
public:

    // Default constructor
    Task();
    // Constructs a task with the given taskId and start time
    Task(TaskId taskId, task_type type);

    // Compares the contents of two tasks
    bool operator==(const Task& rhs) const;

    // The unique ID for this task
    TaskId getTaskId() const;
    // The task ID for this task
    SeqId getSeqId() const;
    // The contech ID for this task
    ContextId getContextId() const;
    // The absolute time when this task started
    ct_timestamp getStartTime() const;
    void setStartTime(ct_timestamp time);
    ct_timestamp getEndTime() const;
    void setEndTime(ct_timestamp time);

    void recordMemOpAction(bool is_write, short pow_size, uint64 addr);
    void recordMallocAction(uint64 addr, uint64 size);
    void recordFreeAction(uint64 addr);
    void recordBasicBlockAction(uint id);

    // List of all successors to this task.
    vector<TaskId>& getSuccessorTasks();
    void addSuccessor(TaskId succ);
    // List of all predecessors to this task.
    vector<TaskId>& getPredecessorTasks();
    void addPredecessor(TaskId pred);

    int getBBCount() const {return bbCount;}
    
    task_type getType() const;
    void setType(task_type e);
    
    sync_type getSyncType() const;
    void setSyncType(sync_type e);
        
    //uint64 getFileOffset() const;
    //void setFileOffset(uint64 offset);

    string toString() const;

    void appendTask(Task*, vector<Task*>*);
    static bool removeTask(Task* rem, vector<Task*>* p, vector<Task*>* s);
    
    //returns the record size written
    static size_t writeContechTask(Task& task, ct_file* out);

    // Wraps the internal list of actions, presenting it as an iterable collection of only memory reads and writes
    // Internally, we skip past actions that we don't care about on increment
    class memOpCollection
    {
        friend class iterator;

        public:
            class iterator
            {
                public:
                    typedef iterator self_type;
                    typedef Action value_type;
                    typedef Action& reference;
                    typedef vector<Action>::iterator pointer;
                    typedef std::forward_iterator_tag iterator_category;
                    typedef int difference_type;

                    iterator() {}
                    iterator(pointer i, memOpCollection* c) : it(i), parent(c) { }
                    self_type operator++(int post)
                    {
                        self_type temp = *this; operator++(); return temp;
                    }
                    self_type& operator++()
                    {
                        // Advance the iterator to the next memory action, skipping basic blocks
                        do { ++it; }
                        while ( it != parent->last && !it->isMemOp());
                        return *this;
                    }
                    reference operator*() { return *it; }
                    pointer operator->() { return it; }
                    bool operator==(const self_type& rhs) { return it == rhs.it; }
                    bool operator!=(const self_type& rhs) { return it != rhs.it; }
                private:
                    pointer it;
                    memOpCollection* parent; // We need to know where the end of the underlying list is, so we don't try to skip past it
            };
            iterator begin();
            iterator end();
            uint size();

            memOpCollection();
            memOpCollection(vector<Action>::iterator f, vector<Action>::iterator e);

        private:
            vector<Action>::iterator first;
            vector<Action>::iterator last;

    };

    // Wraps the internal list of actions, presenting it as an iterable collection of only memory events
    // Internally, we skip past actions that we don't care about on increment
    class memoryActionCollection
    {
        friend class iterator;

        public:
            class iterator
            {
                public:
                    typedef iterator self_type;
                    typedef Action value_type;
                    typedef Action& reference;
                    typedef vector<Action>::iterator pointer;
                    typedef std::forward_iterator_tag iterator_category;
                    typedef int difference_type;

                    iterator() {}
                    iterator(pointer i, memoryActionCollection* c) : it(i), parent(c) { }
                    self_type operator++(int post)
                    {
                        self_type temp = *this; operator++(); return temp;
                    }
                    self_type& operator++()
                    {
                        // Advance the iterator to the next memory action, skipping basic blocks
                        do { ++it; }
                        while ( it != parent->last && !it->isMemoryAction());
                        return *this;
                    }
                    reference operator*() { return *it; }
                    pointer operator->() { return it; }
                    bool operator==(const self_type& rhs) { return it == rhs.it; }
                    bool operator!=(const self_type& rhs) { return it != rhs.it; }
                private:
                    pointer it;
                    memoryActionCollection* parent; // We need to know where the end of the underlying list is, so we don't try to skip past it
            };
            iterator begin();
            iterator end();
            uint size();

            memoryActionCollection();
            memoryActionCollection(vector<Action>::iterator f, vector<Action>::iterator e);

        private:
            vector<Action>::iterator first;
            vector<Action>::iterator last;

    };

    // Wraps the internal list of actions, presenting it as an iterable collection of basic blocks
    // The user can request an iterator to the contained basic blocks
    // Internally, we skip past actions that we don't care about on increment
    class basicBlockActionCollection
    {
        friend class iterator;

        public:
            class iterator
            {
                public:
                    typedef iterator self_type;
                    typedef Action value_type;
                    typedef Action& reference;
                    typedef vector<Action>::iterator pointer;
                    typedef std::forward_iterator_tag iterator_category;
                    typedef int difference_type;

                    iterator() {}
                    iterator(pointer i, basicBlockActionCollection* c) : it(i), parent(c) { }
                    self_type operator++(int post)
                    {
                        self_type temp = *this; operator++(); return temp;
                    }
                    self_type& operator++()
                    {
                        // Advance the iterator to the next basic block action, skipping memory actions
                        do { ++it; }
                        while ( it != parent->last && !it->isBasicBlockAction());
                        return *this;
                    }
                    reference operator*() { return *it; }
                    pointer operator->() { return it; }
                    bool operator==(const self_type& rhs) { return it == rhs.it; }
                    bool operator!=(const self_type& rhs) { return it != rhs.it; }

                    memoryActionCollection getMemoryActions();
                    memOpCollection getMemOps();
                private:
                    pointer it;
                    basicBlockActionCollection* parent; // We need to know where the end of the underlying list is, so we don't try to skip past it
            };
            iterator begin();
            iterator end();
            uint size();

            basicBlockActionCollection();
            basicBlockActionCollection(vector<Action>::iterator f, vector<Action>::iterator e);


        private:
            vector<Action>::iterator first;
            vector<Action>::iterator last;

    };

    vector<Action>& getActions();
    memOpCollection getMemOps();
    memoryActionCollection getMemoryActions();
    basicBlockActionCollection getBasicBlockActions();
};

inline static std::ostream& operator<<(std::ostream& out, const task_type& taskType)
{
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
    return out;
}

inline static std::ostream& operator<<(std::ostream& os, const Task& i) { os << i.toString(); return os; }

} // end namespace contech



#endif
