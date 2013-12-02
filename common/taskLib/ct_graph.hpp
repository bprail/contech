#ifndef CT_GRAPH_H
#define CT_GRAPH_H

#include "Task.hpp"
#include "ct_file.h"
#include <queue>
#include <list>
#include <unordered_map>
#include <iostream>
using namespace std;

namespace contech {
class CachedEntry {
public:
    Task* cachedTask;
    list<TaskId>::iterator lru_position;
};

class ct_graph
{
private:
    ct_file* taskGraph;
    
    //cache
    const unsigned int maxSize = 100;
    unordered_map<TaskId,CachedEntry*> taskMap;
    list<TaskId> lru_list;
    void insertIntoCache(TaskId uid,Task* task);
    Task* getFromCache(TaskId uid);
    
public:
    //constructor
    ct_graph(ct_file* taskGraph);
    
    
    
    //initializes a fileoffset maps that allows random access to tasks in the file.
    //Current implementation requires scan of the file
    void initializeFileOffsetMap();
    //Once the fileOffsetMap has been initialized, get a task by ID
    Task* getTaskByUniqueId(TaskId uid);
    //instantiate a static map
    static map<TaskId,uint64> createFileOffsetMap(){
        map<TaskId,uint64> m;
        return m;
    }
    static map<TaskId,bool> createBFSStateMap(){
        map<TaskId,bool> m;
        return m;
    }
    
    static map<TaskId,uint64> fileOffsetMap;
    static map<TaskId,bool> visited; // maintains which nodes have been visited in bfs
    static queue<TaskId> q; // maintains bfs state
    
    //get next task in the order that a BFS traversal would occur. It maintains state
    //about where it currently is
    Task* getNextTaskInBFSOrder();
    void initializeBFS(); // clear out the map and queue responsible for holding state. 
    
    
};

} // end namespace contech
#endif
