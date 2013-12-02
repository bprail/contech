#include "ct_graph.hpp"
using namespace contech;

map<TaskId,uint64> ct_graph::fileOffsetMap =  ct_graph::createFileOffsetMap();
map<TaskId,bool> ct_graph::visited = ct_graph::createBFSStateMap();
queue<TaskId> ct_graph::q;

ct_graph::ct_graph(ct_file* taskGraph){
    this->taskGraph = taskGraph;
}


void ct_graph::insertIntoCache(TaskId uid,Task* task){
    //This check is here to ignore entries that already exist. This can be removed if this assumption can be broken.
    if(this->taskMap.count(uid) == 0){// if the cache entry doesn't exist
    // then add the UID to the LRU list
    lru_list.push_front(uid);
    
    //create a new entry, populate it, and insert
    CachedEntry* cached = new CachedEntry();
    cached->cachedTask = task;
    cached->lru_position = lru_list.begin();
    this->taskMap[uid] = cached;
    
    //if table is over the size limit, remove LRU entry
    if(this->taskMap.size() > maxSize){
        TaskId lru_key = lru_list.back();
        delete this->taskMap[lru_key]->cachedTask;
            this->taskMap.erase(lru_key);
        lru_list.pop_back();
    }
    } 
}

Task* ct_graph::getFromCache(TaskId uid){
    Task* task;
    
    if(this->taskMap.count(uid) == 0){
    //if uid not in cache return null
    task = NULL;
    } else {
    
    CachedEntry* cached = this->taskMap[uid];
    //update LRU
    list<TaskId>::iterator li = cached->lru_position;
        lru_list.splice(lru_list.begin(), lru_list, li);

        task = cached->cachedTask;
    }
    return task;
}


void ct_graph::initializeFileOffsetMap(){
    Task* currentTask;
    while(currentTask = Task::readContechTask(this->taskGraph)){
        ct_graph::fileOffsetMap[currentTask->getTaskId()] = currentTask->getFileOffset();
    if(this->taskMap.size() < maxSize){ // attempt to reduce cold misses
        insertIntoCache(currentTask->getTaskId(),currentTask);
    } else {
        delete currentTask;
    }
    }
}

Task* ct_graph::getTaskByUniqueId(TaskId uid){
    Task* task;
    if((task = getFromCache(uid)) == NULL){
    ct_seek(ct_graph::fileOffsetMap[uid],this->taskGraph);
    task = Task::readContechTask(this->taskGraph);
    insertIntoCache(uid,task);
    }
    return task;
}
/* Not up to date
Task* ct_graph::getNextTaskInBFSOrder(){
    
    Task* nextTask = NULL;
    while(!q.empty() && nextTask == NULL){
    TaskId next = q.front();
    if(!ct_graph::visited[next]){
        nextTask = this->getTaskByUniqueId(next);
        visited[next] =  true;
        q.push(nextTask->getChildTaskId());
        q.push(nextTask->getContinuationTaskId());
    }
    q.pop();
    }
    nextTask = Task::readContechTask(this->taskGraph);
    return nextTask;
}*/

void ct_graph::initializeBFS(){
    ct_graph::visited.erase(
                ct_graph::visited.begin(),
                ct_graph::visited.end());
    while(!ct_graph::q.empty()){
    ct_graph::q.pop();
    }
    ct_graph::q.push(0);
    ct_seek(0,this->taskGraph);
}
