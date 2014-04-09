#include "../../common/taskLib/TaskGraph.hpp"
#include <algorithm>
#include <iostream>
#include <set>
#include <deque>

using namespace std;
using namespace contech;

int main(int argc, char const *argv[])
{
    //input check
    if(argc != 3){
        cout << "Usage: " << argv[0] << " taskGraphInputFile taskGraphOutputFile" << endl;
        exit(1);
    }

    // TODO: support writing out of a task graph by a backend, as currently the logic
    //  partially resides in middle layer
    fprintf(stderr, "ERROR: BACKEND NOT REVISED FOR CURRENT TASK GRAPH FORMAT\n");
    return 0;
    
    ct_file* taskGraphIn  = create_ct_file_r(argv[1]);
    if (taskGraphIn == NULL){
        cerr << "ERROR: Couldn't open input file" << endl;
        exit(1);
    }

    map<TaskId, Task*> taskGraph;
    vector<Task*> taskGraphOrder;
    int mergeCount = 0;
    
    TaskGraph* tg = TaskGraph::initFromFile(taskGraphIn);
    if (tg == NULL) {}

    while(Task* currentTask = tg->readContechTask())
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
                    p_tasks.push_back(tg->getContechTask(*it));
                }
            }

            auto succ = currentTask->getSuccessorTasks();

            for (auto it = succ.begin(), et = succ.end(); it != et; ++it)
            {
                if (it->getContextId() != ctci) goto nonCandidate;
                t_tasks.push_back(tg->getContechTask(*it));
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
                            t_t_tasks.push_back(tg->getContechTask(*it));
                        }
                    }
                    p->appendTask(t, &t_t_tasks);
                    delete t;
                }
                
                continue;
            }
        }
        
nonCandidate:
        taskGraph[currentTask->getTaskId()] = currentTask;
        taskGraphOrder.push_back(currentTask);
    }
    delete tg;

    cout << "Successfully removed: " << mergeCount << " tasks." << endl;
    
    close_ct_file(taskGraphIn);
    
    ct_file* taskGraphOut  = create_ct_file_w(argv[2], true);
    if (taskGraphOut == NULL){
        cerr << "ERROR: Couldn't open output file" << endl;
        exit(1);
    }
    size_t bytesWritten = 0;
    for (auto it = taskGraphOrder.begin(), et = taskGraphOrder.end(); it != et; ++it)
    {
        Task* currentTaskCandidate = *it;
        //currentTaskCandidate->setFileOffset(bytesWritten);
        //currentTaskCandidate->printTask();
        bytesWritten += Task::writeContechTask(*currentTaskCandidate, taskGraphOut);
        delete currentTaskCandidate;
    }
    close_ct_file(taskGraphOut);
    
    return 0;
}
