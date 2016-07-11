#include "../../common/taskLib/ct_file.h"
#include "../../common/taskLib/TaskGraph.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <queue>
#include <deque>
#include <string>

using namespace std;
using namespace contech;

#define MAX_THREADS 1024

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

 
    TaskGraph* tg = TaskGraph::initFromFile(taskGraphIn);
    if (tg == NULL) {}

    // BBID -> set of Contexts
    map <unsigned int, set<ContextId> > bbExecMap;
    bool firstCreate = false;
    
    while(Task* currentTask = tg->readContechTask())
    {
        TaskId ctui = currentTask->getTaskId();
        ContextId ctci = currentTask->getContextId();
        
        if (firstCreate == true)
        {
            ;
        }
        else
        {
            if (currentTask->getType() != task_type_create)
            {
                delete currentTask;
                continue;
            }
            firstCreate = true;
        }
        
        auto bba = currentTask->getBasicBlockActions();
        for (auto bbit = bba.begin(), bbet = bba.end(); bbit != bbet; ++bbit)
        {
            unsigned int bbid = ((BasicBlockAction)*bbit).basic_block_id;
            
            bbExecMap[bbid].insert(ctci);
        }
        
        delete currentTask;
    }
    delete tg;
    
    ContextId czero = ContextId(0);
    for (auto bbvit = bbExecMap.begin(), bbvet = bbExecMap.end(); bbvit != bbvet; ++bbvit)
    {
        printf("%u, %u, ", bbvit->first, bbvit->second.size());
        bbvit->second.erase(czero);
        printf("%u\n", bbvit->second.size());
    }

    fclose(taskGraphIn);
    return 0;
}
