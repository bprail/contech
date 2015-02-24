#include "../../common/taskLib/TaskGraph.hpp"
#include <algorithm>
#include <iostream>
#include <vector>
#include <map>
#include <stdio.h>

using namespace std;
using namespace contech;

struct TaskPathNode {
	ct_timestamp duration;
	ct_timestamp length;
	TaskId critNode;
	vector<TaskId> pred;
};

int main(int argc, char const *argv[])
{
    //input check
    if(argc != 2){
        cout << "Usage: " << argv[0] << " taskGraphInputFile" << endl;
        exit(1);
    }

    ct_file* taskGraphIn  = create_ct_file_r(argv[1]);
    if(taskGraphIn == NULL){
        cerr << "ERROR: Couldn't open input file" << endl;
        exit(1);
    }

	bool inROI = false;
    TaskGraph* tg = TaskGraph::initFromFile(taskGraphIn);
    
    if (tg == NULL) {}
    
    TaskGraphInfo* tgi = tg->getTaskGraphInfo();
	map<TaskId, TaskPathNode> shadowGraph;
	
	TaskId roiStart = tg->getROIStart(), roiEnd = tg->getROIEnd();
	tg->setTaskOrderCurrent(roiStart);

    // Critical path is a search for longest path
	//   It should start at ROI end and work backward through the predecessors
	//      For each task, look at its length and compute the schedule
	//   For implementation, we will traverse and record these details
    while (Task* currentTask = tg->getNextTask())
	{
		TaskPathNode tpn;
		TaskId tid = currentTask->getTaskId();
		
		tpn.duration = currentTask->getEndTime() - currentTask->getStartTime();
		tpn.pred = currentTask->getPredecessorTasks();
		
		ct_timestamp lpath = 0;
		TaskId cnode = 0;
		
		for (TaskId p : tpn.pred)
		{
			if (shadowGraph[p].length > lpath) {lpath = shadowGraph[p].length; cnode = p;}
		}
		
		tpn.length = lpath + tpn.duration;
		tpn.critNode = cnode;
		shadowGraph[tid] = tpn;
		
		delete currentTask;
		if (tid == roiEnd) break;
	}
	
    delete tg;

	TaskId currentId = roiEnd;
	printf("%s -> %s\n", roiStart.toString().c_str(), roiEnd.toString().c_str());
	while (currentId != roiStart)
	{
		printf("%s, %llu\n", currentId.toString().c_str(), shadowGraph[currentId].length);
		currentId = shadowGraph[currentId].critNode;
	}
	printf("%s, %llu\n", currentId.toString().c_str(), shadowGraph[currentId].length);
	
    close_ct_file(taskGraphIn);
    return 0;
}
