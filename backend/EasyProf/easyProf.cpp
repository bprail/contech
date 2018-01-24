#include "../../common/taskLib/TaskGraph.hpp"
#include <algorithm>
#include <iostream>
#include <set>
#include <stdio.h>
#include <math.h>
#include <climits>

using namespace std;
using namespace contech;

struct statPerGroup{
    uint64 max;
    uint64 min;
    double avg;
    double dev;
    ContextId maxCon;
    ContextId minCon;
};

struct statPerGroup getStat(vector<map<ContextId, uint64> > input, uint groupId, ContextId first){
    struct statPerGroup res;
    res.max = 0;
    res.avg = 0;
    res.dev = 0;
    res.min = ULONG_MAX;
    uint64 total = 0;
    map<ContextId, uint64> tmp = input[groupId];
    uint count = 0;
    for (map<ContextId, uint64>::iterator it = tmp.begin(); it != tmp.end(); it++){
        if (it->first == first){
            continue;
        }
        total += it->second;
        count++;
        if (it->second > res.max){
            res.max = it->second;
            res.maxCon = it->first;
        }
        if (it->second < res.min){
            res.min = it->second;
            res.minCon = it->first;
        }
    }
    double sumDeltaSq = 0;
    if (count != 0){
        res.avg = (double)total/(double)count;
    }
    for (map<ContextId, uint64>::iterator it = tmp.begin(); it != tmp.end(); it++){
        if (it->first == first){
            continue;
        }
        double delta = (double)it->second-(double)res.avg;
        sumDeltaSq += (delta*delta);
    }
    res.dev = sqrt(sumDeltaSq);
    if (res.min == ULONG_MAX){
        res.min = 0;
    }
    return res;
}

int main(int argc, char const *argv[])
{
    //input check
    if (argc < 2)
    {
        cout << "Usage: " << argv[0] << " taskGraphInputFile" << endl;
        exit(1);
    }

    FILE* taskGraphIn  = fopen(argv[1], "rb");
    if(taskGraphIn == NULL)
    {
        cerr << "ERROR: Couldn't open input file" << endl;
        exit(1);
    }

    printf("EasyProf Ver. 1.0.0\n");
    printf("\nProcessing taskgraph... This may take a while\n");

    uint64 totalMemOps = 0;
    uint64 totalMemReads = 0;
    uint64 totalMemWrites = 0;
    uint64 totalMallocs = 0;
    uint64 totalFrees = 0;
    uint64 totalReadBytes = 0;
    uint64 totalWriteBytes = 0;
    uint64 totalMallocedBytes = 0;
    uint64 totalFreedBytes = 0;
    uint64 totalBasicBlocks = 0;
    uint64 totalTaskCount = 0;
    uint64 totalTime = 0;
    uint64 totalMemBytes = 0;
    uint64 totalTasksMainThread = 0;
    uint64 totalTimeMainThread = 0;
    uint64 totalTimeSequentialBeforeCreate = 0;
    uint64 totalTimeSequentialAfterJoin = 0;
    uint64 totalTimeParallel = 0;
    uint64 totalTimeSyncMainThread = 0;
    uint syncCount = 0;
    uint barrCount = 0;
    uint createCount = 0;
    uint joinCount = 0;

    uint nThreadGroup = 0;
    uint maxParallelThread = 1;
    uint64 maxTaskTime = 0;
    TaskId maxTaskTimeId;
    uint64 maxMemFootprint = 0;
    TaskId maxMemFootprintId;
    uint64 maxMemOps = 0;
    TaskId maxMemOpsId;
    map<ContextId, uint64> maxTimePerContext;
    map<ContextId, uint64> maxMemOpsPerContext;
    map<ContextId, uint64> maxMemFootprintPerContext;
    map<ContextId, TaskId> maxTimePerContextId;
    map<ContextId, TaskId> maxMemOpsPerContextId;
    map<ContextId, TaskId> maxMemFootprintPerContextId;

    map<ContextId, uint64> finishTime;
    map<ContextId, uint64> startTime;
    map<TaskId, uint64> taskTime;
    vector<uint> threadPerGroup;
    vector<vector<TaskId> > taskIdPerGroup;
    vector<map<ContextId, uint64> > totalTimePerGroup;
    vector<map<ContextId, uint64> > totalTasksPerGroup;
    vector<map<ContextId, uint64> > totalSyncsPerGroup;
    vector<map<ContextId, uint64> > totalSyncTimePerGroup;
    vector<map<ContextId, uint64> > totalBasicBlocksPerGroup;
    vector<map<ContextId, uint64> > memOpsPerGroup;
    vector<map<ContextId, uint64> > mallocPerGroup;
    vector<map<ContextId, uint64> > freePerGroup;
    vector<map<ContextId, uint64> > memSizePerGroup;
    vector<double> timeImbalancePerGroup;
    vector<double> memOpImbalancePerGroup;
    vector<double> memSizeImbalancePerGroup;

    set<uint> uniqueBlocks;

    TaskGraph* tg = TaskGraph::initFromFile(taskGraphIn);
    bool modelROI = (argc > 2);
    if (tg == NULL) 
    {
        fprintf(stderr, "Failure to open task graph\n");
    }
    
    TaskGraphInfo* tgi = tg->getTaskGraphInfo();
    if (modelROI) tg->setTaskOrderCurrent(tg->getROIStart());

    ContextId mainId;
    TaskId backTrackId;
    Task* currentTask = tg->getNextTask();
    if (currentTask != NULL){
        mainId = currentTask->getContextId();
        backTrackId = currentTask->getTaskId();
    }


    vector<TaskId> unTraversedContexts;
    vector<uint> insideThreadGroup;
    vector<vector<ContextId> > openGroups;
    uint createGroupTimer = 0;
    uint currentParallelThread = 1;
    uint nThreadGroupBackup = nThreadGroup;
    cout << "Currently processing ContextId " << currentTask->getContextId().toString() << "\n";
    maxTimePerContext[currentTask->getContextId()] = 0;
    maxTimePerContextId[currentTask->getContextId()] = currentTask->getTaskId();
    maxMemOpsPerContext[currentTask->getContextId()] = 0;
    maxMemOpsPerContextId[currentTask->getContextId()] = currentTask->getTaskId();
    maxMemFootprintPerContext[currentTask->getContextId()] = 0;
    maxMemFootprintPerContextId[currentTask->getContextId()] = currentTask->getTaskId();
    // Iterate through main thread
    while(currentTask != NULL){
        uint64 time = currentTask->getEndTime() - currentTask->getStartTime();
        // Reset timer
        if(createGroupTimer != 0){
            createGroupTimer--;
        }
        TaskId currentId = currentTask->getTaskId();
        ContextId currentCId = currentTask->getContextId();
        totalTasksMainThread++;
        totalTimeMainThread += time;
        totalTime+=time;
        if (time > maxTaskTime){
            maxTaskTime = time;
            maxTaskTimeId = currentId;
        }
        if (time > maxTimePerContext[currentCId]){
            maxTimePerContext[currentCId] = time;
            maxTimePerContextId[currentCId] = currentId;
        }
        if (startTime.size() == 0){
            startTime[currentCId] = currentTask->getStartTime();
        }
        totalTaskCount++;
        taskTime[currentId] = time;
        switch(currentTask->getType()){
            // Create new thread, add that thread to the new thread group and ignore it for now
            case task_type_create:
            {   
                vector<TaskId> nextTasks = currentTask->getSuccessorTasks();
                
                if (currentParallelThread>maxParallelThread){
                    maxParallelThread = currentParallelThread;
                }
                // Create new thread group 
                if (createGroupTimer == 0){
                    // Increment thread group count
                    insideThreadGroup.push_back(nThreadGroup);
                    nThreadGroup++;
                    vector<ContextId> openG;
                    vector<TaskId> currentGroup;
                    currentGroup.push_back(currentId);
                    openG.push_back(currentCId);
                    threadPerGroup.push_back(1);
                    for (uint i = 0; i < nextTasks.size(); i++){
                        // new task             
                        if (nextTasks[i].getContextId() != currentCId){
                            currentParallelThread += 1;
                            unTraversedContexts.push_back(nextTasks[i]);
                            currentGroup.push_back(nextTasks[i]);
                            openG.push_back(nextTasks[i].getContextId());
                            startTime[nextTasks[i].getContextId()] = currentTask->getEndTime();
                            threadPerGroup[nThreadGroup-1]+=1;
                        }
                    }
                    // Initialize data structures
                    taskIdPerGroup.push_back(currentGroup);
                    openGroups.push_back(openG);
                    map<ContextId, uint64> totalTime;
                    totalTime[currentCId] = time;
                    totalTimePerGroup.push_back(totalTime);
                    map<ContextId, uint64> totalTasks;
                    totalTasks[currentCId] = 1;
                    totalTasksPerGroup.push_back(totalTasks);
                    map<ContextId, uint64> totalSyncs;
                    totalSyncs[currentCId] = 1;
                    totalSyncsPerGroup.push_back(totalSyncs);
                    map<ContextId, uint64> totalSyncTime;
                    totalSyncTime[currentCId] = time;
                    totalSyncTimePerGroup.push_back(totalSyncTime);
                    map<ContextId, uint64> totalBasicBlocks;
                    totalBasicBlocks[currentCId] = 0; 
                    totalBasicBlocksPerGroup.push_back(totalBasicBlocks);
                    map<ContextId, uint64> memOps;
                    memOps[currentCId] = 0;
                    memOpsPerGroup.push_back(memOps);
                    map<ContextId, uint64> mallocs;
                    mallocs[currentCId] = 0;
                    mallocPerGroup.push_back(mallocs);
                    map<ContextId, uint64> frees;
                    frees[currentCId] = 0;
                    freePerGroup.push_back(frees);
                    map<ContextId, uint64> memSize;
                    memSize[currentCId] = 0;
                    memSizePerGroup.push_back(memSize);

                }
                // Belongs to old thread group
                else{
                    vector<TaskId> *currentGroup = &taskIdPerGroup[insideThreadGroup.back()];
                    vector<ContextId> *openG = &openGroups[insideThreadGroup.back()];
                    
                    for (uint i = 0; i < nextTasks.size(); i++){
                        // new task
                        if (nextTasks[i].getContextId() != currentTask->getContextId()){
                            currentParallelThread += 1;
                            unTraversedContexts.push_back(nextTasks[i]);
                            (*currentGroup).push_back(nextTasks[i]);
                            (*openG).push_back(nextTasks[i].getContextId());
                            startTime[nextTasks[i].getContextId()] = currentTask->getEndTime();
                            threadPerGroup[insideThreadGroup.back()] += 1;
                        }
                    }
                    
                    for (uint i = 0; i < insideThreadGroup.size(); i++){
                        map<ContextId, uint64> *totalTime = &totalTimePerGroup[insideThreadGroup[i]];
                        (*totalTime)[currentCId] +=time;
                        map<ContextId, uint64> *totalTasks = &totalTasksPerGroup[insideThreadGroup[i]];
                        (*totalTasks)[currentCId] += 1;
                        map<ContextId, uint64> *totalSyncs = &totalSyncsPerGroup[insideThreadGroup[i]];
                        (*totalSyncs)[currentCId] += 1;
                        map<ContextId, uint64> *totalSyncTime = &totalSyncTimePerGroup[insideThreadGroup[i]];
                        (*totalSyncTime)[currentCId] += time;
                    }
                }
                createGroupTimer = 3;
                createCount++;
                totalTimeSyncMainThread += time;
                break;
            }
            case task_type_join:
            {
                vector<TaskId> prevTasks = currentTask->getPredecessorTasks();
                currentParallelThread -= 1;
                ContextId joinedThread;
                uint groupNo = -1;
                // Determine which task group a task belongs.
                for (uint i = 0; i < prevTasks.size(); i++){
                    if (prevTasks[i].getContextId() != currentCId){
                        joinedThread = prevTasks[i].getContextId();
                    }
                }
                finishTime[joinedThread] = currentTask->getEndTime();
                vector<ContextId> *tmp = NULL;
                for (uint i = 0; i < openGroups.size(); i++){
                    tmp = &openGroups[i];
                    if ((*tmp).size() == 0){
                        continue;
                    }
                    vector<ContextId>::iterator it = find((*tmp).begin(), (*tmp).end(), joinedThread);
                    if ((*tmp).front() != joinedThread && it != (*tmp).end()){
                        groupNo = i;
                        (*tmp).erase(it);
                        break;
                    }
                }
                // All children joined
                if ((*tmp).size() == 1){
                    openGroups[groupNo].clear();
                    insideThreadGroup.erase(find(insideThreadGroup.begin(), insideThreadGroup.end(), groupNo));
                }
                for (uint i = 0; i < insideThreadGroup.size(); i++){
                    map<ContextId, uint64> *totalTime = &totalTimePerGroup[insideThreadGroup[i]];
                    (*totalTime)[currentCId] +=time;
                    map<ContextId, uint64> *totalTasks = &totalTasksPerGroup[insideThreadGroup[i]];
                    (*totalTasks)[currentCId] += 1;
                    map<ContextId, uint64> *totalSyncs = &totalSyncsPerGroup[insideThreadGroup[i]];
                    (*totalSyncs)[currentCId] += 1;
                    map<ContextId, uint64> *totalSyncTime = &totalSyncTimePerGroup[insideThreadGroup[i]];
                    (*totalSyncTime)[currentCId] += time;
                }
                totalTimeSyncMainThread += time;
                joinCount++;
                break;
            }
            case task_type_sync:
            {
                // Situation when sync marks the end of context
                vector<TaskId> prevTasks = currentTask->getPredecessorTasks();
                for (uint i = 0; i < prevTasks.size(); i++){
                    Task* tmp = tg->getTaskById(prevTasks[i]);
                    if (tmp->getType() == task_type_sync){
                        Task* tmp2 = tg->getTaskById(tmp->getTaskId().getNext());
                        if (tmp2->getSuccessorTasks().size() == 0){
                            ContextId joinedThread = tmp2->getContextId();
                            finishTime[joinedThread] = currentTask->getEndTime();
                            uint groupNo = -1;
                            vector<ContextId> *tmp3 = NULL;
                            for (uint i = 0; i < openGroups.size(); i++){
                                tmp3 = &openGroups[i];
                                if ((*tmp3).size() == 0){
                                    continue;
                                }
                                vector<ContextId>::iterator it = find((*tmp3).begin(), (*tmp3).end(), joinedThread);
                                if ((*tmp3).front() != joinedThread && it != (*tmp3).end()){
                                    groupNo = i;
                                    (*tmp3).erase(it);
                                    break;
                                }
                            }
                            // All children joined
                            if ((*tmp3).size() == 1){
                                openGroups[groupNo].clear();
                                insideThreadGroup.erase(find(insideThreadGroup.begin(), insideThreadGroup.end(), groupNo));
                            }
                        }
                    }
                }
                for (uint i = 0; i < insideThreadGroup.size(); i++){
                    map<ContextId, uint64> *totalTime = &totalTimePerGroup[insideThreadGroup[i]];
                    (*totalTime)[currentCId] +=time;
                    map<ContextId, uint64> *totalTasks = &totalTasksPerGroup[insideThreadGroup[i]];
                    (*totalTasks)[currentCId] += 1;
                    map<ContextId, uint64> *totalSyncs = &totalSyncsPerGroup[insideThreadGroup[i]];
                    (*totalSyncs)[currentCId] += 1;
                    map<ContextId, uint64> *totalSyncTime = &totalSyncTimePerGroup[insideThreadGroup[i]];
                    (*totalSyncTime)[currentCId] += time;
                }
                totalTimeSyncMainThread += time;
                syncCount++;
                break;
            }
            case task_type_barrier:
            {
                for (uint i = 0; i < insideThreadGroup.size(); i++){
                    map<ContextId, uint64> *totalTime = &totalTimePerGroup[insideThreadGroup[i]];
                    (*totalTime)[currentCId] +=time;
                    map<ContextId, uint64> *totalTasks = &totalTasksPerGroup[insideThreadGroup[i]];
                    (*totalTasks)[currentCId] += 1;
                    map<ContextId, uint64> *totalSyncs = &totalSyncsPerGroup[insideThreadGroup[i]];
                    (*totalSyncs)[currentCId] += 1;
                    map<ContextId, uint64> *totalSyncTime = &totalSyncTimePerGroup[insideThreadGroup[i]];
                    (*totalSyncTime)[currentCId] += time;
                }
                totalTimeSyncMainThread += time;
                barrCount++;
                break;
            }
            case task_type_basic_blocks:
            {
                if (insideThreadGroup.size() == 0 && nThreadGroupBackup == nThreadGroup){
                    totalTimeSequentialBeforeCreate += time;
                }
                else if (insideThreadGroup.size() == 0){
                    totalTimeSequentialAfterJoin += time;
                }
                else{
                    totalTimeParallel += time;
                }
                uint64 thisMemFootprint = 0;
                uint64 thisMemOps = 0;
                auto bba = currentTask->getBasicBlockActions();
                for (auto f = bba.begin(), e = bba.end(); f != e; ++f)
                {
                    BasicBlockAction bb = *f;
                    uniqueBlocks.insert((uint)bb.basic_block_id);
                    
                    auto bbi = tgi->getBasicBlockInfo((uint)bb.basic_block_id);
                    
                    
                    totalBasicBlocks++;

                    // Note that memory actions include malloc, etc
                    for (MemoryAction mem : f.getMemoryActions())
                    {   
                        for (uint i = 0; i < insideThreadGroup.size(); i++){
                            map<ContextId, uint64> *memOps = &memOpsPerGroup[insideThreadGroup[i]];
                            (*memOps)[currentCId] += 1;
                            if (mem.type == action_type_mem_read || mem.type == action_type_mem_write){
                                map<ContextId, uint64> *memBytes = &memSizePerGroup[insideThreadGroup[i]];
                                (*memBytes)[currentCId] += (0x1 << mem.pow_size);
                            }
                            if (mem.type == action_type_malloc){
                                map<ContextId, uint64> *mallocs = &mallocPerGroup[insideThreadGroup[i]];
                                (*mallocs)[currentCId] += 1;
                            }
                            if (mem.type == action_type_free){
                                map<ContextId, uint64> *frees = &freePerGroup[insideThreadGroup[i]];
                                (*frees)[currentCId] += 1;
                            }
                        }
                        totalMemOps++;
                        thisMemOps++;
                        if (mem.type == action_type_mem_read || mem.type == action_type_mem_write){
                            totalMemBytes += (0x1 << mem.pow_size);
                            thisMemFootprint += (0x1 << mem.pow_size);
                        }
                        if (mem.type == action_type_mem_read){
                            totalMemReads++;
                            totalReadBytes += (0x1 << mem.pow_size);
                        }
                        if (mem.type == action_type_mem_write){
                            totalMemWrites++;
                            totalWriteBytes += (0x1 << mem.pow_size);
                        }
                        if (mem.type == action_type_malloc){
                            totalMallocs++;
                            totalMallocedBytes += (0x1 << mem.pow_size);
                        }
                        if (mem.type == action_type_free){
                            totalFrees++;
                            totalFreedBytes += (0x1 << mem.pow_size);
                        }
                    }
                }
                for (uint i = 0; i < insideThreadGroup.size(); i++){
                    map<ContextId, uint64> *basicBlocks = &totalBasicBlocksPerGroup[insideThreadGroup[i]];
                    (*basicBlocks)[currentCId] += 1;
                }
                if (thisMemOps > maxMemOps){
                    maxMemOps = thisMemOps;
                    maxMemOpsId = currentId;
                }
                if (thisMemFootprint > maxMemFootprint){
                    maxMemFootprint = thisMemFootprint;
                    maxMemFootprintId = currentId;
                }
                if (thisMemOps > maxMemOpsPerContext[currentCId]){
                    maxMemOpsPerContext[currentCId] = thisMemOps;
                    maxMemOpsPerContextId[currentCId] = currentId;
                }
                if (thisMemFootprint > maxMemFootprintPerContext[currentCId]){
                    maxMemFootprintPerContext[currentCId] = thisMemFootprint;
                    maxMemFootprintPerContextId[currentCId] = currentId;
                }
                break;
            }
        }
        currentId = currentId.getNext();
        delete currentTask;
        currentTask = tg->getTaskById(currentId);
    }

    // Traverse remaining contexts
    while (unTraversedContexts.size() != 0){
        currentTask = tg->getTaskById(*unTraversedContexts.begin());
        unTraversedContexts.erase(unTraversedContexts.begin());
        uint groupId = (uint)-1;
        for (uint i = 0; i < taskIdPerGroup.size(); i++){
            vector<TaskId> tmp = taskIdPerGroup[i];
            for (uint j = 0; j < tmp.size(); j++){
                if (tmp[j] == currentTask->getTaskId()){
                    groupId = i;
                    break;
                }
            }
        }
        cout << "Currently processing ContextId " << currentTask->getContextId().toString() << "\n";
        if (groupId == (uint)-1){
            printf("Error reading other contexts, quitting...\n");
            exit(1);
        }
        insideThreadGroup.clear();
        insideThreadGroup.push_back(groupId);
        createGroupTimer = 0;
        uint currentParallelThread = threadPerGroup[groupId];
        openGroups.clear();
        maxTimePerContext[currentTask->getContextId()] = 0;
        maxTimePerContextId[currentTask->getContextId()] = currentTask->getTaskId();
        maxMemOpsPerContext[currentTask->getContextId()] = 0;
        maxMemOpsPerContextId[currentTask->getContextId()] = currentTask->getTaskId();
        maxMemFootprintPerContext[currentTask->getContextId()] = 0;
        maxMemFootprintPerContextId[currentTask->getContextId()] = currentTask->getTaskId();

        // iterate through other context IDs
        while(currentTask != NULL){
            uint64 time = currentTask->getEndTime() - currentTask->getStartTime();
            // Reset timer
            if(createGroupTimer != 0){
                createGroupTimer--;
            }
            totalTaskCount++;
            totalTime+=time;
            TaskId currentId = currentTask->getTaskId();
            ContextId currentCId = currentTask->getContextId();
            if (time > maxTaskTime){
                maxTaskTime = time;
                maxTaskTimeId = currentId;
            }
            if (time > maxTimePerContext[currentCId]){
                maxTimePerContext[currentCId] = time;
                maxTimePerContextId[currentCId] = currentId;
            }
            if (startTime.size() == 0){
                startTime[currentCId] = currentTask->getStartTime();
            }
            taskTime[currentId] = time;
            switch(currentTask->getType()){
                // Create new thread, add that thread to the new thread group and ignore it for now
                case task_type_create:
                {   
                    vector<TaskId> nextTasks = currentTask->getSuccessorTasks();
                    
                    if (currentParallelThread>maxParallelThread){
                        maxParallelThread = currentParallelThread;
                    }
                    // Create new thread group 
                    if (createGroupTimer == 0){
                        // Increment thread group count
                        insideThreadGroup.push_back(nThreadGroup);
                        nThreadGroup++;
                        vector<ContextId> openG;
                        vector<TaskId> currentGroup;
                        currentGroup.push_back(currentId);
                        openG.push_back(currentCId);
                        threadPerGroup.push_back(1);
                        for (uint i = 0; i < nextTasks.size(); i++){
                            // new task             
                            if (nextTasks[i].getContextId() != currentCId){
                                currentParallelThread += 1;
                                unTraversedContexts.push_back(nextTasks[i]);
                                currentGroup.push_back(nextTasks[i]);
                                openG.push_back(nextTasks[i].getContextId());
                                startTime[nextTasks[i].getContextId()] = currentTask->getEndTime();
                                threadPerGroup[nThreadGroup-1]+=1;
                            }
                        }
                        // Initialize data structures
                        taskIdPerGroup.push_back(currentGroup);
                        openGroups.push_back(openG);
                        map<ContextId, uint64> totalTime;
                        totalTime[currentCId] = time;
                        totalTimePerGroup.push_back(totalTime);
                        map<ContextId, uint64> totalTasks;
                        totalTasks[currentCId] = 1;
                        totalTasksPerGroup.push_back(totalTasks);
                        map<ContextId, uint64> totalSyncs;
                        totalSyncs[currentCId] = 1;
                        totalSyncsPerGroup.push_back(totalSyncs);
                        map<ContextId, uint64> totalSyncTime;
                        totalSyncTime[currentCId] = time;
                        totalSyncTimePerGroup.push_back(totalSyncTime);
                        map<ContextId, uint64> totalBasicBlocks;
                        totalBasicBlocks[currentCId] = 0; 
                        totalBasicBlocksPerGroup.push_back(totalBasicBlocks);
                        map<ContextId, uint64> memOps;
                        memOps[currentCId] = 0;
                        memOpsPerGroup.push_back(memOps);
                        map<ContextId, uint64> mallocs;
                        mallocs[currentCId] = 0;
                        mallocPerGroup.push_back(mallocs);
                        map<ContextId, uint64> frees;
                        frees[currentCId] = 0;
                        freePerGroup.push_back(frees);
                        map<ContextId, uint64> memSize;
                        memSize[currentCId] = 0;
                        memSizePerGroup.push_back(memSize);

                    }
                    // Belongs to old thread group
                    else{
                        vector<TaskId> *currentGroup = &taskIdPerGroup[insideThreadGroup.back()];
                        vector<ContextId> *openG = &openGroups[insideThreadGroup.back()];
                        for (uint i = 0; i < nextTasks.size(); i++){
                            // new task
                            if (nextTasks[i].getContextId() != currentTask->getContextId()){
                                currentParallelThread += 1;
                                unTraversedContexts.push_back(nextTasks[i]);
                                (*currentGroup).push_back(nextTasks[i]);
                                (*openG).push_back(nextTasks[i].getContextId());
                                startTime[nextTasks[i].getContextId()] = currentTask->getEndTime();
                                threadPerGroup[insideThreadGroup.back()] += 1;
                            }
                        }
                        for (uint i = 0; i < insideThreadGroup.size(); i++){
                            map<ContextId, uint64> *totalTime = &totalTimePerGroup[insideThreadGroup[i]];
                            (*totalTime)[currentCId] +=time;
                            map<ContextId, uint64> *totalTasks = &totalTasksPerGroup[insideThreadGroup[i]];
                            (*totalTasks)[currentCId] += 1;
                            map<ContextId, uint64> *totalSyncs = &totalSyncsPerGroup[insideThreadGroup[i]];
                            (*totalSyncs)[currentCId] += 1;
                            map<ContextId, uint64> *totalSyncTime = &totalSyncTimePerGroup[insideThreadGroup[i]];
                            (*totalSyncTime)[currentCId] += time;
                        }
                    }
                    createGroupTimer = 3;
                    createCount++;
                    break;
                }
                case task_type_join:
                {
                    vector<TaskId> prevTasks = currentTask->getPredecessorTasks();
                    currentParallelThread -= 1;
                    ContextId joinedThread;
                    uint groupNo = -1;
                    // Determine which task group a task belongs.
                    for (uint i = 0; i < prevTasks.size(); i++){
                        if (prevTasks[i].getContextId() != currentCId){
                            joinedThread = prevTasks[i].getContextId();
                        }
                    }
                    finishTime[joinedThread] = currentTask->getEndTime();
                    vector<ContextId> *tmp = NULL;
                    for (uint i = 0; i < openGroups.size(); i++){
                        tmp = &openGroups[i];
                        if ((*tmp).size() == 0){
                            continue;
                        }
                        vector<ContextId>::iterator it = find((*tmp).begin(), (*tmp).end(), joinedThread);
                        if ((*tmp).front() != joinedThread && it != (*tmp).end()){
                            groupNo = i;
                            (*tmp).erase(it);
                            break;
                        }
                    }
                    // All children joined
                    if ((*tmp).size() == 1){
                        openGroups[groupNo].clear();
                        insideThreadGroup.erase(find(insideThreadGroup.begin(), insideThreadGroup.end(), groupNo));
                    }
                    for (uint i = 0; i < insideThreadGroup.size(); i++){
                        map<ContextId, uint64> *totalTime = &totalTimePerGroup[insideThreadGroup[i]];
                        (*totalTime)[currentCId] +=time;
                        map<ContextId, uint64> *totalTasks = &totalTasksPerGroup[insideThreadGroup[i]];
                        (*totalTasks)[currentCId] += 1;
                        map<ContextId, uint64> *totalSyncs = &totalSyncsPerGroup[insideThreadGroup[i]];
                        (*totalSyncs)[currentCId] += 1;
                        map<ContextId, uint64> *totalSyncTime = &totalSyncTimePerGroup[insideThreadGroup[i]];
                        (*totalSyncTime)[currentCId] += time;
                    }
                    joinCount++;
                    break;
                }
                case task_type_sync:
                {
                    // Situation when sync marks the end of context
                    vector<TaskId> prevTasks = currentTask->getPredecessorTasks();
                    for (uint i = 0; i < prevTasks.size(); i++){
                        Task* tmp = tg->getTaskById(prevTasks[i]);
                        if (tmp->getType() == task_type_sync){
                            Task* tmp2 = tg->getTaskById(tmp->getTaskId().getNext());
                            if (tmp2->getSuccessorTasks().size() == 0){
                                ContextId joinedThread = tmp2->getContextId();
                                finishTime[joinedThread] = currentTask->getEndTime();
                                uint groupNo = -1;
                                vector<ContextId> *tmp3 = NULL;
                                for (uint i = 0; i < openGroups.size(); i++){
                                    tmp3 = &openGroups[i];
                                    if ((*tmp3).size() == 0){
                                        continue;
                                    }
                                    vector<ContextId>::iterator it = find((*tmp3).begin(), (*tmp3).end(), joinedThread);
                                    if ((*tmp3).front() != joinedThread && it != (*tmp3).end()){
                                        groupNo = i;
                                        (*tmp3).erase(it);
                                        break;
                                    }
                                }
                                // All children joined
                                if ((*tmp3).size() == 1){
                                    openGroups[groupNo].clear();
                                    insideThreadGroup.erase(find(insideThreadGroup.begin(), insideThreadGroup.end(), groupNo));
                                }
                            }
                        }
                    }
                    for (uint i = 0; i < insideThreadGroup.size(); i++){
                        map<ContextId, uint64> *totalTime = &totalTimePerGroup[insideThreadGroup[i]];
                        (*totalTime)[currentCId] +=time;
                        map<ContextId, uint64> *totalTasks = &totalTasksPerGroup[insideThreadGroup[i]];
                        (*totalTasks)[currentCId] += 1;
                        map<ContextId, uint64> *totalSyncs = &totalSyncsPerGroup[insideThreadGroup[i]];
                        (*totalSyncs)[currentCId] += 1;
                        map<ContextId, uint64> *totalSyncTime = &totalSyncTimePerGroup[insideThreadGroup[i]];
                        (*totalSyncTime)[currentCId] += time;
                    }
                    syncCount++;
                    break;
                }
                case task_type_barrier:
                {
                    for (uint i = 0; i < insideThreadGroup.size(); i++){
                        map<ContextId, uint64> *totalTime = &totalTimePerGroup[insideThreadGroup[i]];
                        (*totalTime)[currentCId] +=time;
                        map<ContextId, uint64> *totalTasks = &totalTasksPerGroup[insideThreadGroup[i]];
                        (*totalTasks)[currentCId] += 1;
                        map<ContextId, uint64> *totalSyncs = &totalSyncsPerGroup[insideThreadGroup[i]];
                        (*totalSyncs)[currentCId] += 1;
                        map<ContextId, uint64> *totalSyncTime = &totalSyncTimePerGroup[insideThreadGroup[i]];
                        (*totalSyncTime)[currentCId] += time;
                    }
                    barrCount++;
                    break;
                }
                case task_type_basic_blocks:
                {
                    uint64 thisMemFootprint = 0;
                    uint64 thisMemOps = 0;
                    auto bba = currentTask->getBasicBlockActions();
                    for (auto f = bba.begin(), e = bba.end(); f != e; ++f)
                    {
                        BasicBlockAction bb = *f;
                        uniqueBlocks.insert((uint)bb.basic_block_id);
                        
                        auto bbi = tgi->getBasicBlockInfo((uint)bb.basic_block_id);
                        
                        
                        totalBasicBlocks++;

                        // Note that memory actions include malloc, etc
                        for (MemoryAction mem : f.getMemoryActions())
                        {   
                            for (uint i = 0; i < insideThreadGroup.size(); i++){
                                map<ContextId, uint64> *memOps = &memOpsPerGroup[insideThreadGroup[i]];
                                (*memOps)[currentCId] += 1;
                                if (mem.type == action_type_mem_read || mem.type == action_type_mem_write){
                                    map<ContextId, uint64> *memBytes = &memSizePerGroup[insideThreadGroup[i]];
                                    (*memBytes)[currentCId] += (0x1 << mem.pow_size);
                                }
                                if (mem.type == action_type_malloc){
                                    map<ContextId, uint64> *mallocs = &mallocPerGroup[insideThreadGroup[i]];
                                    (*mallocs)[currentCId] += 1;
                                }
                                if (mem.type == action_type_free){
                                    map<ContextId, uint64> *frees = &freePerGroup[insideThreadGroup[i]];
                                    (*frees)[currentCId] += 1;
                                }
                            }
                            totalMemOps++;
                            thisMemOps++;
                            if (mem.type == action_type_mem_read || mem.type == action_type_mem_write){
                                totalMemBytes += (0x1 << mem.pow_size);
                                thisMemFootprint += (0x1 << mem.pow_size);
                            }
                            if (mem.type == action_type_mem_read){
                                totalMemReads++;
                                totalReadBytes += (0x1 << mem.pow_size);
                            }
                            if (mem.type == action_type_mem_write){
                                totalMemWrites++;
                                totalWriteBytes += (0x1 << mem.pow_size);
                            }
                            if (mem.type == action_type_malloc){
                                totalMallocs++;
                                totalMallocedBytes += (0x1 << mem.pow_size);
                            }
                            if (mem.type == action_type_free){
                                totalFrees++;
                                totalFreedBytes += (0x1 << mem.pow_size);
                            }
                        }
                    }
                    for (uint i = 0; i < insideThreadGroup.size(); i++){
                        map<ContextId, uint64> *totalTime = &totalTimePerGroup[insideThreadGroup[i]];
                        (*totalTime)[currentCId] +=time;
                        map<ContextId, uint64> *totalTasks = &totalTasksPerGroup[insideThreadGroup[i]];
                        (*totalTasks)[currentCId] += 1;
                        map<ContextId, uint64> *basicBlocks = &totalBasicBlocksPerGroup[insideThreadGroup[i]];
                        (*basicBlocks)[currentCId] += 1;
                    }
                    if (thisMemOps > maxMemOps){
                        maxMemOps = thisMemOps;
                        maxMemOpsId = currentId;
                    }
                    if (thisMemFootprint > maxMemFootprint){
                        maxMemFootprint = thisMemFootprint;
                        maxMemFootprintId = currentId;
                    }
                    if (thisMemOps > maxMemOpsPerContext[currentCId]){
                        maxMemOpsPerContext[currentCId] = thisMemOps;
                        maxMemOpsPerContextId[currentCId] = currentId;
                    }
                    if (thisMemFootprint > maxMemFootprintPerContext[currentCId]){
                        maxMemFootprintPerContext[currentCId] = thisMemFootprint;
                        maxMemFootprintPerContextId[currentCId] = currentId;
                    }
                    break;
                }
            }
            currentId = currentId.getNext();
            delete currentTask;
            currentTask = tg->getTaskById(currentId);
        }
    }

    printf("Processing taskgraph %s successful!\n", argv[1]);
    printf("\n");
    printf("For help, please input \"help\"\n");
    printf("List of available commands:\n");
    printf("stat           [show basic statistics about the program]\n");
    printf("mem            [show information about memory usage]\n");
    printf("main           [show information about main execution context (or main thread)]\n");
    printf("group          [show information about execution context group (or parallelized threads)]\n");
    printf("g{groupId}     [show information about specific context group. Example: g3]\n");
    printf("c{contextId}   [show information about specific context. Example: c7]\n");
    printf("quit           [quit EasyProf]\n");
    printf("\n");

    while(true){
        string input = "";
        cout << ">> ";
        getline(cin, input);
        if (input.compare("quit") == 0){
            exit(0);
        }
        else if(input.compare("help") == 0){
            printf("stat           [show basic statistics about the program]\n");
            printf("mem            [show information about memory usage]\n");
            printf("main           [show information about main execution context (or main thread)]\n");
            printf("group          [show information about execution context group (or parallelized threads)]\n");
            printf("g{groupId}     [show information about specific context group. Example: g3]\n");
            printf("c{contextId}   [show information about specific context. Example: c7]\n");
            printf("quit           [quit EasyProf]\n");
        }
        else if(input.compare("stat") == 0){
            printf("Overall Statistics for %s\n", argv[1]);
            printf("----------------------------------\n");
            printf("Total number of create: %u\n", createCount);
            printf("Total number of join: %u\n", joinCount);
            printf("Total number of sync: %u\n", syncCount);
            printf("Total number of barrier: %u\n", barrCount);
            printf("\n");
            printf("Total basic blocks: %lu\n", totalBasicBlocks);
            printf("Unique basic blocks: %lu\n", uniqueBlocks.size());
            printf("Total tasks: %lu\n", totalTaskCount);
            printf("Average time per task: %lf\n", (double)totalTime/((double)totalTaskCount));
            printf("Max time per task: %lu, task id is %s\n", maxTaskTime, maxTaskTimeId.toString().c_str());
            printf("\n");
            printf("Total time taken: %lu\n", totalTimeMainThread);
            printf("Total memory operations: %lu\n", totalMemOps);
            printf("Total bytes Accessed: %lu\n", totalMemBytes);
            printf("Total context groups: %u\n", nThreadGroup);
            printf("Max parallelized contexts: %u\n", maxParallelThread);
            printf("\n");
        }
        else if(input.compare("main") == 0){
            printf("Main Execution Context Statistics\n");
            printf("----------------------------------\n");
            printf("Total time spent by main context: %lu\n", totalTimeMainThread);
            printf("Total time spent by main context before any thread creation: %lu\n", totalTimeSequentialBeforeCreate);
            printf("Total time spent by main context after all thread join: %lu\n", totalTimeSequentialAfterJoin);
            printf("Total sequential time spent by main context: %lu\n", totalTimeSequentialAfterJoin + totalTimeSequentialBeforeCreate);
            printf("Sequential execution time percentage of main context: %lf%%\n", (double)(totalTimeSequentialAfterJoin + totalTimeSequentialBeforeCreate)*100/(double)totalTimeMainThread);
            printf("Total synchronization time spent by main context: %lu\n", totalTimeSyncMainThread);
            printf("Synchronization time percentage of main context: %lf%%\n", (double)(totalTimeSyncMainThread)*100/(double)totalTimeMainThread);
            printf("\n");
        }
        else if(input.compare("mem") == 0){
            printf("Memory Operations Statistics\n");
            printf("----------------------------------\n");
            printf("Total memory operations: %lu\n", totalMemOps);
            printf("Total number of reads: %lu\n", totalMemReads);
            printf("Total number of writes: %lu\n", totalMemWrites);
            printf("Total number of mallocs: %lu\n", totalMallocs);
            printf("Total number of frees: %lu\n", totalFrees);
            printf("\n");
            printf("Total bytes Accessed: %lu\n", totalMemBytes);
            printf("Total number of bytes read: %lu\n", totalReadBytes);
            printf("Total number of bytes writen: %lu\n", totalWriteBytes);
            printf("Potential leaked memory in bytes: %lu\n", totalMallocedBytes - totalFreedBytes);
            printf("\n");
            printf("Average memory operations per task: %lu\n", totalMemOps/totalTaskCount);
            printf("Average memory footprint per task in bytes: %lu\n", totalMemBytes/totalTaskCount);
            printf("Max memory operations per task: %lu, task id is %s\n", maxMemOps, maxMemOpsId.toString().c_str());
            printf("Max memory footprint per task in bytes: %lu, task id is %s\n", maxMemFootprint, maxMemFootprintId.toString().c_str());
            printf("For race detection, please run the Helgrind backend.\n");
        }
        else if(input.compare("group") == 0){
            printf("Context Group Statistics\n");
            printf("----------------------------------\n");
            printf("Total context groups: %u\n", nThreadGroup);
            printf("For detailed statistics, please query specific group ID\n");
            for (uint i = 0; i < nThreadGroup; i++){
                printf("\n");
                printf("Group ID %u general statistics\n", i);
                printf("----------------------------------\n");
                printf("Note: Work imbalance is calculated by 100*(max - min)/min\n");
                ContextId first = taskIdPerGroup[i].front().getContextId();
                struct statPerGroup timeCurrent = getStat(totalTimePerGroup, i, first);
                struct statPerGroup tasksCurrent = getStat(totalTasksPerGroup, i, first);
                struct statPerGroup syncsCurrent = getStat(totalSyncsPerGroup, i, first);
                struct statPerGroup synctimeCurrent = getStat(totalSyncTimePerGroup, i, first);
                struct statPerGroup bbCurrent = getStat(totalBasicBlocksPerGroup, i, first);
                struct statPerGroup memopsCurrent = getStat(memOpsPerGroup, i, first);
                struct statPerGroup mallocCurrent = getStat(mallocPerGroup, i, first);
                struct statPerGroup freeCurrent = getStat(freePerGroup, i, first);
                struct statPerGroup memsizeCurrent = getStat(memSizePerGroup, i, first);
                printf("Number of contexts in current group: %lu\n", taskIdPerGroup[i].size());
                printf("Average time for current group: %lf\n", timeCurrent.avg);
                printf("Average number of tasks for current group: %lu\n", (uint64)tasksCurrent.avg);
                printf("Average number of synchronizations for current group: %lu\n", (uint64)syncsCurrent.avg);
                printf("Average time of synchronization for current group: %lf\n", synctimeCurrent.avg);
                printf("Average basic blocks for current group: %lu\n", (uint64)bbCurrent.avg);
                printf("Average memory operations for current group: %lu\n", (uint64)memopsCurrent.avg);
                printf("Average memory footprint in bytes for current group: %lu\n", (uint64)memsizeCurrent.avg);
                printf("Average mallocs for current group: %lu\n", (uint64)mallocCurrent.avg);
                printf("Average frees for current group: %lu\n", (uint64)freeCurrent.avg);
                printf("Work imbalance by time: %lf%%\n", (double)(timeCurrent.max-timeCurrent.min)*100/(double)(timeCurrent.min));
                printf("Work imbalance by memory operations: %lf%%\n", (double)(memopsCurrent.max-memopsCurrent.min)*100/(double)(memopsCurrent.min));
                printf("Work imbalance by memory footprint: %lf%%\n", (double)(memsizeCurrent.max-memsizeCurrent.min)*100/(double)(memsizeCurrent.min));
            }
        }
        else if(input.size() > 1 && input.at(0) == 'g'){
            int groupId;
            try{groupId = stoi(input.substr(1));}
            catch(const exception& e){
                printf("Wrong command. For help, please input \"help\"\n");
                continue;
            }
            if ((uint)groupId >= threadPerGroup.size()){
                printf("Group ID doesn't exist! Note: group ID start with 0\n");
            }
            else{
                printf("Group ID %u detailed statistics\n", groupId);
                printf("----------------------------------\n");
                printf("Note: Work imbalance is calculated by 100*(max - min)/min\n");
                printf("Context ID in this group: ");
                for (uint i = 0; i < taskIdPerGroup[groupId].size(); i++){
                    printf("%s ", taskIdPerGroup[groupId][i].getContextId().toString().c_str());
                }
                ContextId first = taskIdPerGroup[groupId].front().getContextId();
                struct statPerGroup timeCurrent = getStat(totalTimePerGroup, groupId, first);
                struct statPerGroup tasksCurrent = getStat(totalTasksPerGroup, groupId, first);
                struct statPerGroup syncsCurrent = getStat(totalSyncsPerGroup, groupId, first);
                struct statPerGroup synctimeCurrent = getStat(totalSyncTimePerGroup, groupId, first);
                struct statPerGroup bbCurrent = getStat(totalBasicBlocksPerGroup, groupId, first);
                struct statPerGroup memopsCurrent = getStat(memOpsPerGroup, groupId, first);
                struct statPerGroup mallocCurrent = getStat(mallocPerGroup, groupId, first);
                struct statPerGroup freeCurrent = getStat(freePerGroup, groupId, first);
                struct statPerGroup memsizeCurrent = getStat(memSizePerGroup, groupId, first);
                printf("\nAverage time for current group: %lf\n", timeCurrent.avg);
                printf("Max time for current group: %lf ms, context ID is %s\n", timeCurrent.max/(double)1000000, timeCurrent.maxCon.toString().c_str());
                printf("Min time for current group: %lf ms, context ID is %s\n", timeCurrent.min/(double)1000000, timeCurrent.minCon.toString().c_str());
                printf("Standard deviation time for current group: %lf\n", timeCurrent.dev);
                printf("\n");
                printf("Average number of tasks for current group: %lu\n", (uint64)tasksCurrent.avg);
                printf("Max number of tasks for current group: %lu, context ID is %s\n", (uint64)tasksCurrent.max, tasksCurrent.maxCon.toString().c_str());
                printf("Min number of tasks for current group: %lu, context ID is %s\n", (uint64)tasksCurrent.min, tasksCurrent.minCon.toString().c_str());
                printf("Standard deviation number of tasks for current group: %lu\n", (uint64)tasksCurrent.dev);
                printf("\n");
                printf("Average number of synchronizations for current group: %lu\n", (uint64)syncsCurrent.avg);
                printf("Max number of synchronizations for current group: %lu, context ID is %s\n", (uint64)syncsCurrent.max, syncsCurrent.maxCon.toString().c_str());
                printf("Min number of synchronizations for current group: %lu, context ID is %s\n", (uint64)syncsCurrent.min, syncsCurrent.minCon.toString().c_str());
                printf("Standard deviation number of synchronizations for current group: %lu\n", (uint64)syncsCurrent.dev);
                printf("\n");
                printf("Average time of synchronization for current group: %lf\n", synctimeCurrent.avg);
                printf("Max time of synchronization for current group: %lu, context ID is %s\n", synctimeCurrent.max, synctimeCurrent.maxCon.toString().c_str());
                printf("Min time of synchronization for current group: %lu, context ID is %s\n", synctimeCurrent.min, synctimeCurrent.minCon.toString().c_str());
                printf("Standard deviation time of synchronization for current group: %lf\n", synctimeCurrent.dev);
                printf("\n");
                printf("Average basic blocks for current group: %lu\n", (uint64)bbCurrent.avg);
                printf("Max basic blocks for current group: %lu, context ID is %s\n", (uint64)bbCurrent.max, bbCurrent.maxCon.toString().c_str());
                printf("Min basic blocks for current group: %lu, context ID is %s\n", (uint64)bbCurrent.min, bbCurrent.minCon.toString().c_str());
                printf("Standard deviation basic blocks for current group: %lu\n", (uint64)bbCurrent.dev);
                printf("\n");
                printf("Average memory operations for current group: %lu\n", (uint64)memopsCurrent.avg);
                printf("Max memory operations for current group: %lu, context ID is %s\n", (uint64)memopsCurrent.max, memopsCurrent.maxCon.toString().c_str());
                printf("Min memory operations for current group: %lu, context ID is %s\n", (uint64)memopsCurrent.min, memopsCurrent.minCon.toString().c_str());
                printf("Standard deviation memory operations for current group: %lu\n", (uint64)memopsCurrent.dev);
                printf("\n");
                printf("Average memory footprint in bytes for current group: %lu\n", (uint64)memsizeCurrent.avg);
                printf("Max memory footprint in byte for current group: %lu, context ID is %s\n", (uint64)memsizeCurrent.max, memsizeCurrent.maxCon.toString().c_str());
                printf("Min memory footprint in byte for current group: %lu, context ID is %s\n", (uint64)memsizeCurrent.min, memsizeCurrent.minCon.toString().c_str());
                printf("Standard deviation memory footprint for current group: %lu\n", (uint64)memsizeCurrent.dev);
                printf("\n");
                printf("Average mallocs for current group: %lu\n", (uint64)mallocCurrent.avg);
                printf("Average frees for current group: %lu\n", (uint64)freeCurrent.avg);
                printf("Work imbalance by time: %lf%%\n", (double)(timeCurrent.max-timeCurrent.min)*100/(double)(timeCurrent.min));
                printf("Work imbalance by memory operations: %lf%%\n", (double)(memopsCurrent.max-memopsCurrent.min)*100/(double)(memopsCurrent.min));
                printf("Work imbalance by memory footprint: %lf%%\n", (double)(memsizeCurrent.max-memsizeCurrent.min)*100/(double)(memsizeCurrent.min));
            }
        }
        else if(input.size() != 0 && input.at(0) == 'c'){
            int contextId;
            try{contextId = stoi(input.substr(1));}
            catch(const exception& e){
                printf("Wrong command. For help, please input \"help\"\n");
                continue;
            }
            ContextId cId = ContextId((uint)contextId);
            if (maxTimePerContext.find(cId) == maxTimePerContext.end()){
                printf("Context ID doesn't exist!\n");
            }
            else{
                printf("Context ID %u detailed statistics\n", contextId);
                printf("----------------------------------\n");
                printf("Context ID is inside the following groups: ");
                uint groupId = 0;
                for (uint i = 0; i < taskIdPerGroup.size(); i++){
                    for (uint j = 0; j < taskIdPerGroup[i].size(); j++){
                        if (taskIdPerGroup[i][j].getContextId() == cId){
                            printf("%u ", i);
                            groupId = i;
                        }
                    }
                }
                if (contextId == 0){
                    groupId = 0;
                }
                printf("\nTotal time for current context: %ld\n", totalTimePerGroup[groupId][cId]);
                printf("Total number of tasks for current context: %ld\n", totalTasksPerGroup[groupId][cId]);
                printf("Total number of synchronization for current context: %ld\n", totalSyncsPerGroup[groupId][cId]);
                printf("Total time of synchronization for current context: %lf ms\n", (double)totalSyncTimePerGroup[groupId][cId]/(double)1000000);
                printf("Total basic blocks for current context: %ld\n", totalBasicBlocksPerGroup[groupId][cId]);
                printf("Total memory operations for current context: %ld\n", memOpsPerGroup[groupId][cId]);
                printf("Total memory footprint in byte for current context: %ld\n", memSizePerGroup[groupId][cId]);
                printf("Total mallocs for current context: %ld\n", mallocPerGroup[groupId][cId]);
                printf("Total frees for current context: %ld\n", freePerGroup[groupId][cId]);
                printf("Max task time for current context: %lf ms, task id is %s\n", (double)maxTimePerContext[cId]/(double)1000000, maxTimePerContextId[cId].toString().c_str());
                printf("Max task memory operations for current context: %ld, task id is %s\n", maxMemOpsPerContext[cId], maxMemOpsPerContextId[cId].toString().c_str());
                printf("Max task memory footprint for current context: %ld, task id is %s\n", maxMemFootprintPerContext[cId], maxMemFootprintPerContextId[cId].toString().c_str());
            }
        }
        else{
            printf("Wrong command. For help, please input \"help\"\n");
        }
    }


    fclose(taskGraphIn);
    return 0;
}
