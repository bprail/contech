#include "../../common/taskLib/Task.hpp"
#include <algorithm>
#include <iostream>
#include <set>
#include <deque>

using namespace std;
using namespace contech;

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
        //fprintf(stderr, "From Map %s\n", id.toString().c_str());
        t = f->second;
        return t;
    }
    
    // Process tasks from file until the requested task is found
    while ((t = Task::readContechTask(in)) != NULL)
    {
        TaskId tid = t->getTaskId();
        //fprintf(stderr, "  Have %s want %s tid->cont: %ld\n", tid.toString().c_str(), id.toString().c_str(), t->getContinuationTaskId());
        
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

int main(int argc, char const *argv[])
{
    //input check
    if(argc != 3){
        cout << "Usage: " << argv[0] << " taskGraphInputFile taskGraphOutputFile" << endl;
        exit(1);
    }

    ct_file* taskGraphIn  = create_ct_file_r(argv[1]);
    if(isClosed(taskGraphIn)){
        cerr << "ERROR: Couldn't open input file" << endl;
        exit(1);
    }

    map<TaskId, Task*> taskGraph;
    vector<Task*> taskGraphOrder;
    int mergeCount = 0;
    while(Task* currentTask = getNextTask(taskGraphIn))
    {
        // If this task is only related to its contech, then it is a candidate
        // forall p  in (currentTask->getPredecessorTasks()) 
        //    p->getContextId() == currentTask->getContextId()
        // forall t in (currentTask->getPredecessorTasks())
        //    t->getContextId() == currentTask->getContextId()
        
        ContextId ctci = currentTask->getContextId();
        
        if (currentTask->getType() != task_type_sync)
        {
        
        }
        else {
            auto pred = currentTask->getPredecessorTasks();
            vector<Task*> p_tasks;
            vector<Task*> t_tasks;
            
            for (auto it = pred.begin(), et = pred.end(); it != et; ++it)
            {
                if (it->getContextId() != ctci) goto nonCandidate;
                auto p = taskGraph.find(*it);
                if (p != taskGraph.end())
                {
                    p_tasks.push_back(p->second);
                }
                else
                {
                    p_tasks.push_back(getTaskById(taskGraphIn, (*it)));
                }
            }

            auto succ = currentTask->getSuccessorTasks();

            for (auto it = succ.begin(), et = succ.end(); it != et; ++it)
            {
                if (it->getContextId() != ctci) goto nonCandidate;
                t_tasks.push_back(getTaskById(taskGraphIn, (*it)));
            }
            
            if (!Task::removeTask(currentTask, &p_tasks, &t_tasks))
            {
                cerr << "Failed to remove " << currentTask->getTaskId() << endl;
            }
            else
            {
                //cerr << "Removed " << currentTask->getTaskId() << endl;
                // Finally, find the p in p_task and t in t_task that are basic_block and merge
                mergeCount ++;
                delete currentTask;
                Task* p = NULL;
                Task* t = NULL;
                
                for (auto it = p_tasks.begin(), et = p_tasks.end(); it != et; ++it)
                {
                    if ((*it)->getType() == task_type_basic_blocks)
                    {
                        p = *it;
                        for (auto itt = t_tasks.begin(), ett = t_tasks.end(); itt != ett; ++itt)
                        {
                            if ((*itt)->getType() == task_type_basic_blocks)
                            {
                                t = *itt;
                                break;
                            }
                        }
                        break;
                    }
                }
                if (p == NULL || t == NULL)
                {
                    cerr << "Cannot find mergable basic block tasks after removal" << endl;
                
                    continue;
                }
                
                {
                    vector<Task*> t_t_tasks;
                    auto t_succ = t->getSuccessorTasks();
                    for (auto it = t_succ.begin(), et = t_succ.end(); it != et; ++it)
                    {
                        auto p = taskGraph.find(*it);
                        if (p != taskGraph.end())
                        {
                            t_t_tasks.push_back(p->second);
                        }
                        else
                        {
                            t_t_tasks.push_back(getTaskById(taskGraphIn, (*it)));
                        }
                    }
                    p->appendTask(t, &t_t_tasks);
                    taskList.erase(find(taskList.begin(), taskList.end(), t));
                    taskMap.erase(t->getTaskId());
                    delete t;
                }
                
                continue;
            }
        }
        
nonCandidate:
        taskGraph[currentTask->getTaskId()] = currentTask;
        taskGraphOrder.push_back(currentTask);
    }

    cout << "Successfully removed: " << mergeCount << " tasks." << endl;
    
    close_ct_file(taskGraphIn);
    
    ct_file* taskGraphOut  = create_ct_file_w(argv[2], true);
    if(isClosed(taskGraphOut)){
        cerr << "ERROR: Couldn't open output file" << endl;
        exit(1);
    }
    size_t bytesWritten = 0;
    for (auto it = taskGraphOrder.begin(), et = taskGraphOrder.end(); it != et; ++it)
    {
        Task* currentTaskCandidate = *it;
        currentTaskCandidate->setFileOffset(bytesWritten);
        //currentTaskCandidate->printTask();
        bytesWritten += Task::writeContechTask(*currentTaskCandidate, taskGraphOut);
        delete currentTaskCandidate;
    }
    close_ct_file(taskGraphOut);
    
    return 0;
}
