#include "ct_graph.hpp"
#include <iostream>
#include <algorithm>
#include <iostream>
#include <set>
#include <deque>
#include <iomanip>
#define MAX_SEEKS 40
using namespace contech;

int main(int argc, char* argv[])
{
    printf("==Performing BFS==\n\n");
    
        ct_file* in = create_ct_file_r("/net/tinker/pvassenk/contech/tmp/taskparallel_tiled.taskgraph.uncomp");
    
    ct_graph* graph = new ct_graph(in);
    
    graph->initializeFileOffsetMap();
    graph->initializeBFS();
    Task* nextTask;
    while(nextTask = graph->getNextTaskInBFSOrder()){
        TaskId next = nextTask->getTaskId();
        cout << Task::getContextIdfromTaskId(next) << ":" << Task::getSeqIdfromTaskId(next) << endl;
        cout << "Adding " << Task::getContextIdfromTaskId(nextTask->getChildTaskId()) << ":" << Task::getSeqIdfromTaskId(nextTask->getChildTaskId()) << " and " << Task::getContextIdfromTaskId(nextTask->getContinuationTaskId()) << ":" << Task::getSeqIdfromTaskId(nextTask->getContinuationTaskId()) << endl;
    }
    delete graph;
    
    return 0;
    
    
}
