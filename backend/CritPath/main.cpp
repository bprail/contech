#include "../../common/taskLib/TaskGraph.hpp"
#include <algorithm>
#include <iostream>
#include <vector>
#include <map>
#include <stdio.h>

using namespace std;
using namespace contech;

#define RES_COUNT 10

enum CritPathTypes {
    BASE_PATH,
    SPEEDUP_2X_PATH,
    SPEEDUP_IDEAL_PATH,
    SPEEDUP_BOTTLE_PATH,
    SPEEDUP_BUFFER_PATH,
    SPEEDUP_STATIC_PATH,
    NUM_PATHS
};

enum SimpleIdealRes {
    NONE = 0,
    INTEGER,
    FLOAT,
    MEM,
    NO_SPEEDUP,
    NUM_SIMPLE_RES
};

struct TaskPathNode {
	ct_timestamp duration[NUM_PATHS];
	ct_timestamp length[NUM_PATHS];
    ct_timestamp length_bottle[NUM_PATHS];
    ct_timestamp slack[NUM_PATHS];
    ct_timestamp syncTime[NUM_PATHS];
    ct_timestamp bbCount[NUM_PATHS];
    ct_timestamp minSync, maxSync;
    int numSync;
	TaskId critNode[NUM_PATHS];
    int16_t bottleRes, idealRes;
    task_type tType;
	vector<TaskId> pred;
};

class vec11 {
    public:
    vec11():v(RES_COUNT + 1) { f = -1;}
    vector<uint32_t> v;
    int f;
};

struct TaskSpeedup {
    int16_t bottleRes;
    vector<float> confSpeedup;
};

class ProgramSpeedup {
    public:
        ProgramSpeedup(uint16_t n) : numConfigs(n) {}
        map<TaskId, TaskSpeedup> taskSpeedupMap;
        float getSpeedup(TaskId, int8_t*, int8_t*, int maxPos = -1);
        float getBottleSpeedup(TaskId);
        float getSpeedup(TaskId, int pos);
        float getSpeedupInv(TaskId, int8_t*, int8_t*);
        const uint16_t numConfigs;
};

SimpleIdealRes getSimpleClass(int16_t r)
{
    switch (r)
    {
        case -1: return NO_SPEEDUP;
        case 0:
        case 1:
        case 2: return INTEGER;
        case 3:
        case 4: 
        case 5: return FLOAT;
        case 6:
        case 7:
        case 8:
        case 9: return MEM;
        case 10: return NONE;
        default: fprintf(stderr, "Unknown r: %d\n", r); return NO_SPEEDUP;
    }
}

float ProgramSpeedup::getBottleSpeedup(TaskId tid)
{
    auto tspt = taskSpeedupMap.find(tid);
    if (tspt == taskSpeedupMap.end()) return 1.0f;
    auto p = tspt->second.bottleRes;
    if (p < tspt->second.confSpeedup.size()) return tspt->second.confSpeedup[p];
    return 1.0f;
}

float ProgramSpeedup::getSpeedup(TaskId tid, int pos)
{
    auto tspt = taskSpeedupMap.find(tid);
    if (tspt == taskSpeedupMap.end()) return 1.0f;
    if (pos == -1) return 1.0f;
    if (pos < tspt->second.confSpeedup.size()) return tspt->second.confSpeedup[pos];
    return 1.0f;
}

float ProgramSpeedup::getSpeedup(TaskId tid, int8_t* bottleR, int8_t* speedR, int maxPos)
{
    float maxSpeedUp = 1.0f;
    auto tspt = taskSpeedupMap.find(tid);
    
    *bottleR = -1; *speedR = -1;
    
    if (tspt == taskSpeedupMap.end()) return maxSpeedUp;
    TaskSpeedup tsp = tspt->second;
    
    unsigned int pos = 0;
    
    *bottleR = tsp.bottleRes;
    for (auto it = tsp.confSpeedup.begin(), et = tsp.confSpeedup.end(); it != et; ++it, pos++)
    {
        if (pos == maxPos) break;
        if (*it > maxSpeedUp) 
        {
            maxSpeedUp = *it;
            *speedR = pos;
        }
        else if (*it == maxSpeedUp && maxSpeedUp > 1.0f)
        {
            // Given equal speedups, choose the bottleneck resource
            //   But only when this is an actual speedup and not parity.
            if (pos == tsp.bottleRes) *speedR = pos;
        }
    }
    
    return maxSpeedUp;
}

float ProgramSpeedup::getSpeedupInv(TaskId tid, int8_t* bottleR, int8_t* speedR)
{
    float maxSpeedUp = 1.0f;
    auto tspt = taskSpeedupMap.find(tid);
    
    *bottleR = -1;
    
    if (tspt == taskSpeedupMap.end()) return maxSpeedUp;
    TaskSpeedup tsp = tspt->second;
    
    unsigned int pos = 0;
    
    *bottleR = tsp.bottleRes;
    for (auto it = tsp.confSpeedup.begin(), et = tsp.confSpeedup.end(); it != et; ++it, pos++)
    {
        if (*it < maxSpeedUp) 
        {
            maxSpeedUp = *it;
            *speedR = pos;
        }
        else if (*it == maxSpeedUp)
        {
            if (pos == tsp.bottleRes) *speedR = true;
        }
    }
    
    return maxSpeedUp;
}

// Read in speed up file
//  Format: (all numbers are in binary, speedups are 4byte floats, 
//           configs, bottlenecks are 2 byte ints, others are 4 byte ints)
//    <number of configurations>-<number of tasks>
//    <context ID>:<sequence ID><config speedup>..<config speedup><bottleneck resource>_<bottleneck type>
ProgramSpeedup* processTaskSpeedup(const char* fileName)
{
    FILE* tSpeedFile = fopen(fileName, "rb");
    
    if (tSpeedFile == NULL)
    {
        cerr << "ERROR: Could not open task speedup file" << endl;
        return new ProgramSpeedup(0);
    }
    
    uint16_t numConfigs;
    uint32_t numTasks = 0;
    char chkVal = 0;
    
    fread(&numConfigs, sizeof(numConfigs), 1, tSpeedFile);
    fread(&chkVal, sizeof(chkVal), 1, tSpeedFile);
    assert(chkVal == '-');
    fread(&numTasks, sizeof(numTasks), 1, tSpeedFile);
    
    assert(numConfigs < 32);
    ProgramSpeedup* pPS = new ProgramSpeedup(numConfigs);
    uint32_t confHist[32] = {0};
    
    for (unsigned int i = 0; i < numTasks; i++)
    {
        TaskId tid;
        ContextId ctid;
        SeqId seqid;
        TaskSpeedup ts;
        
        fread(&ctid, sizeof(ctid), 1, tSpeedFile);
        fread(&chkVal, sizeof(chkVal), 1, tSpeedFile);
        assert(chkVal == ':');
        fread(&seqid, sizeof(seqid), 1, tSpeedFile);
        
        tid = TaskId(ctid, seqid);
        
        for (unsigned int nc = 0; nc < numConfigs; nc++)
        {
            float tCS = 1.0f;
            fread(&tCS, sizeof(tCS), 1, tSpeedFile);
            
            ts.confSpeedup.push_back(tCS);
        }
        /*printf("%s, ", tid.toString().c_str());
        for (auto it = ts.confSpeedup.begin(), et = ts.confSpeedup.end(); it != et; ++it)
        {
            printf("%f, ", *it);
        }
        printf("\n");*/
        
        short resId;
        short bnckId;
        fread(&resId, sizeof(resId), 1, tSpeedFile);
        fread(&chkVal, sizeof(chkVal), 1, tSpeedFile);
        if (chkVal != '_')
        {
            fprintf(stderr, "Failure on task %u of %u\n", i, numTasks);
            fprintf(stderr, "Was reading CT %s\n", tid.toString().c_str());
            assert(chkVal == '_');
        }
        // bnckId 0 - issue, 1 - latency
        fread(&bnckId, sizeof(bnckId), 1, tSpeedFile);
        
        //  Issue only test
        //if (bnckId == 1) continue;
        
        switch (resId)
        {
            case (0): resId = 0; break; // INT_ADD
            case (1): resId = 1; break; // INT_MUL
            case (2): resId = 2; break; // INT_DIV
            case (3): resId = 0; printf("INT_SHUF\n"); break; // INT_SHUF
            case (4): resId = 3; break; // FP_ADD
            case (5): resId = 4; break; // FP_MUL
            case (6): resId = 5; break; // FP_DIV
            case (7): resId = 0; printf("FP_SHUF\n"); break; // FP_SHUF
            case (8): resId = 6; break; // L1_LD -> L1
            case (9): resId = 6; break; // L1_ST -> L1
            case (10): resId = 7; break; // L2
            case (11): resId = 8; break; // L3
            case (12): resId = 9; break; // MEM
            case (13): resId = 10; break; // Non-work
            default: assert(0);
        }
        
        
        confHist[resId]++;
        //if (resId == 10) continue;
        
        //assert(resId < numConfigs);
        
        ts.bottleRes = resId;
        pPS->taskSpeedupMap[tid] = ts;
    }
    
    printf("ConfHist, ");
    for (unsigned int i = 0; i < numConfigs; i++)
    {
        printf("%u, ", confHist[i]);
    }
    printf("\n");
    
    fclose(tSpeedFile);
    
    return pPS;
}

int main(int argc, char const *argv[])
{
    uint pathLimit = NUM_PATHS;
    
    //input check
    if(argc < 3){
        cout << "Usage: " << argv[0] << " taskGraphInputFile task_speedup_file <path limit>" << endl;
        exit(1);
    }
    
    if (argc >3)
        pathLimit = atoi(argv[3]);

    FILE* taskGraphIn  = fopen(argv[1], "rb");
    if(taskGraphIn == NULL)
    {
        cerr << "ERROR: Could not open input file" << endl;
        exit(1);
    }

	bool inROI = false;
    TaskGraph* tg = TaskGraph::initFromFile(taskGraphIn);
    
    if (tg == NULL) {exit(1);}
    
    ProgramSpeedup* pPS = processTaskSpeedup(argv[2]);
    
    //if (pPS == NULL) {delete tg; close_ct_file(taskGraphIn); exit(1);}
    
    TaskGraphInfo* tgi = tg->getTaskGraphInfo();
	map<TaskId, TaskPathNode> shadowGraph;
	
    map<uint64_t, ct_timestamp> lastLockAction;
    map<ContextId, ct_timestamp> lastContextTime;
    map<ContextId, uint32_t> lastBB;
    
    // LastBB -> FirstBB -> config
    map<uint32_t, map <uint32_t, vec11 > > staticBBMap;
    
    map<ContextId, vec11> perContextConfUsage;
    map<ContextId, TaskId> contextStart;
    
	TaskId roiStart = tg->getROIStart(), roiEnd = tg->getROIEnd();
    ct_timestamp roiStartTime, roiEndTime;
    
    ct_timestamp smallSlowTask = ~0;
    TaskId smallID;
    int8_t smallRes;
    auto numContexts = tg->getNumberOfContexts();
    
    /*{
        Task* currentTask = tg->getContechTask(TaskId(49, 56));
        TaskId tid = currentTask->getTaskId();
        int tt = currentTask->getType();
        
        fprintf(stderr, "%s: %llu -> %llu, %d, %d\n", tid.toString().c_str(), currentTask->getStartTime(), currentTask->getEndTime(), tt, currentTask->getBBCount());
   
        auto tgi = tg->getTaskGraphInfo();
        auto bbac = currentTask->getBasicBlockActions();
        set<unsigned int> bbS;
        for (auto it = bbac.begin(), et = bbac.end(); it != et; ++it)
        {
            BasicBlockAction bba = *it;
            if (bbS.find(bba.basic_block_id) == bbS.end())
            {
                auto bbi = tgi->getBasicBlockInfo(bba.basic_block_id);
                bbS.insert(bba.basic_block_id);
           
                fprintf(stderr, "%u, %s, %s:%u, %d / %d\n", 
                                                     bba.basic_block_id, 
                                                     bbi.functionName.c_str(), 
                                                     bbi.fileName.c_str(),
                                                 bbi.lineNumber,
                                                 bbi.critPathLen, bbi.numOfOps);
            }
        }
   
        assert(0);
        
    }*/
    #if DEADLINE
	tg->setTaskOrderCurrent(roiStart);
    while (Task* currentTask = tg->getNextTask())
    {
        TaskId tid = currentTask->getTaskId();
        task_type tt = currentTask->getType();
        uint32_t lastBBId = 0;
        uint32_t firstBBId = 0;
        
        if (tt == task_type_basic_blocks)
        {
            ct_timestamp dur = currentTask->getEndTime() - currentTask->getStartTime();
            int8_t bRes, sRes;
            
            if (currentTask->getBBCount() != 0)
            {
                auto bbac = currentTask->getBasicBlockActions();
                lastBBId = lastBB[tid.getContextId()];
                uint32_t tBBId = 0;
                for (auto bbit = bbac.begin(), bbet = bbac.end(); bbit != bbet; ++bbit)
                {
                    BasicBlockAction bb = *bbit;
                    tBBId = (uint)bb.basic_block_id;
                    if (bbit == bbac.begin()) firstBBId = tBBId;
                }
                lastBB[tid.getContextId()] = tBBId;
            }
            
            float spd = pPS->getSpeedup(tid, &bRes, &sRes, RES_COUNT);
            
            ct_timestamp nTime = dur;
            if (spd > 1.0f) nTime = dur / spd;
            
            staticBBMap[lastBBId][firstBBId].v[sRes] += (dur - nTime);
        }
        
        delete currentTask;
    }
    
    
    for (auto it = staticBBMap.begin(), et = staticBBMap.end(); it != et; ++it)
    {
        for (auto fit = it->second.begin(), fet = it->second.end(); fit != fet; ++fit)
        {
            int p = 0, maxSavePos = -1;
            ct_timestamp maxSaving = 0;
            for (auto vit = fit->second.v.begin(), vet = fit->second.v.end(); vit != vet; ++vit, p++)
            {
                if (*vit > maxSaving)
                {
                    maxSaving = *vit;
                    maxSavePos = p;
                }
                *vit = 0;
            }
            fit->second.f = maxSavePos;
        }
    }
    #endif
    
    ct_timestamp roiStartLen = 0;
    int roiStartBB = 0;
    tg->resetTaskOrder();
    tg->setTaskOrderCurrent(roiStart);
    // Critical path is a search for longest path
	//   It should start at ROI end and work backward through the predecessors
	//      For each task, look at its length and compute the schedule
	//   For implementation, we will traverse and record these details
    while (Task* currentTask = tg->getNextTask())
	{
		TaskPathNode tpn;
		TaskId tid = currentTask->getTaskId();
        task_type tt = currentTask->getType();
        unsigned int bbCount = currentTask->getBBCount();
        
        tpn.pred = currentTask->getPredecessorTasks();
        tpn.tType = tt;
        uint32_t lastBBId = 0;
        uint32_t firstBBId = 0;
        
        // TODO: Model overlap times between tasks and dependencies
        ct_timestamp dur = currentTask->getEndTime();
		
        lastContextTime[tid.getContextId()] = dur;
        
        auto it = contextStart.find(tid.getContextId());
        if (it == contextStart.end())
        {
            contextStart[tid.getContextId()] = tid;
        }
        
        if (tt == task_type_basic_blocks && bbCount == 0) dur = 0;
        else if (tt == task_type_sync)
        {
            ct_timestamp start = currentTask->getStartTime();
            auto memOp = currentTask->getMemoryActions().begin();
            uint64_t lockAddr = memOp->data;
            start = max(start, lastLockAction[lockAddr]);
            if (start > dur) start = dur; // SKEW!
            dur -= start;
            lastLockAction[lockAddr] = currentTask->getEndTime();
        }
        else if (tt == task_type_join)
        {
            ct_timestamp joinStart = currentTask->getStartTime();
            for (TaskId p : tpn.pred)
            {
                if (p.getContextId() == tid.getContextId()) continue;
                ct_timestamp tempJT = lastContextTime[p.getContextId()];
                if (lastContextTime[p.getContextId()] > joinStart) joinStart = tempJT;
                
            }
            dur -= joinStart;
        }
        else  {
            //assert(dur > currentTask->getStartTime());
            dur -= currentTask->getStartTime();
        }
        
        if (tt == task_type_basic_blocks && bbCount != 0)
        {
            auto bbac = currentTask->getBasicBlockActions();
            lastBBId = lastBB[tid.getContextId()];
            uint32_t tBBId = 0;
            for (auto bbit = bbac.begin(), bbet = bbac.end(); bbit != bbet; ++bbit)
            {
                BasicBlockAction bb = *bbit;
                tBBId = (uint)bb.basic_block_id;
                if (bbit == bbac.begin()) firstBBId = tBBId;
            }
            lastBB[tid.getContextId()] = tBBId;
        }
        
        if (tid == roiStart) {roiStartTime = currentTask->getStartTime(); roiStartLen = dur; roiStartBB = currentTask->getBBCount();}
        else if (tid == roiEnd) {roiEndTime = currentTask->getStartTime(); dur = 0;} // ROI End is not part of ROI
        
        ct_timestamp dStart = dur;
        for (unsigned int i = 0; i < pathLimit; i++)
        {
            switch (i)
            {
                case BASE_PATH:
                {
                    tpn.duration[i] = dur;
                    tpn.length_bottle[i] = 0;
                    break;
                }
                case SPEEDUP_IDEAL_PATH:
                {
                    int8_t bRes, sRes;
                    float spd = pPS->getSpeedup(tid, &bRes, &sRes, RES_COUNT);
                    
                    if (tt != task_type_basic_blocks)
                        tpn.idealRes = 10; // NONE
                    else if (pPS->numConfigs > 1)
                        tpn.idealRes = sRes;
                    else
                        tpn.idealRes = bRes;
                    
                    // Due to converting between float and integer, 1.0f is not equality.
                    if (spd != 1.0f)
                    {
                        tpn.duration[i] = dur / spd; 
                        if (sRes != -1)
                        {
                            staticBBMap[lastBBId][firstBBId].v[sRes] += 1;
                            perContextConfUsage[tid.getContextId()].v[sRes] += 1;
                        }
                    }
                    else
                    {
                        tpn.duration[i] = dur;
                        if (sRes != -1)
                        {
                            staticBBMap[lastBBId][firstBBId].v[sRes] += 1;
                            perContextConfUsage[tid.getContextId()].v[sRes] += 1;
                        }
                    }
                    
                    //printf("%llu(%f,%d,%d)\t", tpn.duration[i], pPS->getSpeedup(tid, &bRes, &sRes), (uint32_t)bRes, (uint32_t)sRes) ;
                    
                    if (bRes == sRes && bRes != -1) tpn.length_bottle[i] = tpn.duration[i];
                    else tpn.length_bottle[i] = 0;
                    
                    tpn.bottleRes = bRes;
                    
                    break;
                }
                case SPEEDUP_BOTTLE_PATH:
                {
                    int8_t bRes, sRes;
                    float spd = pPS->getBottleSpeedup(tid);
                    
                    if (spd != 1.0f && spd != 0.0f)
                        tpn.duration[i] = ((float)dur) / spd;
                    else
                        tpn.duration[i] = dur;
                    tpn.length_bottle[i] = dur;
                    
                    spd = pPS->getSpeedupInv(tid, &bRes, &sRes);
                    if (spd < 1.0f && spd > 0.0f)
                    {
                        if (dur < smallSlowTask)
                        {
                            smallSlowTask = dur;
                            smallID = tid;
                            smallRes = sRes;
                        }
                    }
                    
                    //printf("%llu(%f)\t", tpn.duration[i], pPS->getBottleSpeedup(tid));
                    break;
                }
                case SPEEDUP_2X_PATH:
                {
                    int8_t bRes, sRes;
                    float spd = pPS->getSpeedup(tid, RES_COUNT + 1 - 1);
                    
                    // Due to converting between float and integer, 1.0f is not equality.
                    if (spd != 1.0f && spd != 0.0f)
                    {
                        tpn.duration[i] = dur / spd;
                    }
                    else
                    {
                        tpn.duration[i] = dur;
                    }
                    
                    tpn.length_bottle[i] = 0;
                    
                    break;
                }
                case SPEEDUP_BUFFER_PATH:
                {
                    int8_t bRes, sRes;
                    float spd = pPS->getSpeedup(tid, RES_COUNT + 2 - 1); // Get the Res + 2, but start counting at 0
                    
                    // Due to converting between float and integer, 1.0f is not equality.
                    if (spd != 1.0f && spd != 0.0f)
                    {
                        tpn.duration[i] = dur / spd; 
                    }
                    else
                    {
                        tpn.duration[i] = dur;
                    }
                    
                    tpn.length_bottle[i] = 0;
                    
                    break;
                }
                case SPEEDUP_STATIC_PATH:
                {
                    int pos = staticBBMap[lastBBId][firstBBId].f; 
                    float spd;

                    if (pos == -1)
                    {
                        int p = 0;
                        for (auto it = staticBBMap[lastBBId][firstBBId].v.begin(), et = staticBBMap[lastBBId][firstBBId].v.end(); 
                             it != et; ++it, p++)
                        {
                            if (*it != 0) {pos = p; break;}
                        }
                        staticBBMap[lastBBId][firstBBId].f = pos;
                    }
                    
                    spd = pPS->getSpeedup(tid, pos);
                    
                    if (spd != 1.0f && spd != 0.0f)
                    {
                        tpn.duration[i] = dur / spd; 
                    }
                    else
                    {
                        tpn.duration[i] = dur;
                    }
                    
                    tpn.length_bottle[i] = 0;
                }
                break;
                default:
                    fprintf(stderr, "ERROR: Unimplemented path at %d\n", __LINE__);break;
            }
            
            //
            // While basic block tasks with zero BBs are just placeholders, their durations are already set to zero
            // 
            if (currentTask->getType() != task_type_basic_blocks) 
            {
                tpn.syncTime[i] = dur;
                if (i == SPEEDUP_IDEAL_PATH)
                {
                    tpn.minSync = dur;
                    tpn.maxSync = dur;
                    tpn.numSync = 1;
                }
            }
            else 
            {
                tpn.syncTime[i] = 0;
                if (i == SPEEDUP_IDEAL_PATH)
                {
                    tpn.minSync = ~0x0;
                    tpn.maxSync = 0;
                    tpn.numSync = 0;
                }
            }
            
            ct_timestamp lpath = 0;
            ct_timestamp lbottle = 0;
            ct_timestamp slpath = 0;
            ct_timestamp spath = ~0x0;
            ct_timestamp synPath = 0;
            ct_timestamp bbCountSum = 0;
            TaskId cnode = 0;
            
            for (TaskId p : tpn.pred)
            {
                if (shadowGraph[p].length[i] > lpath) 
                {
                    lpath = shadowGraph[p].length[i]; 
                    lbottle = shadowGraph[p].length_bottle[i]; 
                    synPath = shadowGraph[p].syncTime[i];
                    slpath = shadowGraph[p].slack[i];
                    bbCountSum = shadowGraph[p].bbCount[i];
                    cnode = p;
                }
                
                if (shadowGraph[p].length[i] < spath && shadowGraph[p].length[i] > 0)
                {
                    spath = shadowGraph[p].length[i];
                    if (i == 1)
                    {
                        //printf("%s -> %s, %llu, ", tid.toString().c_str(), p.toString().c_str(), spath);
                    }
                }
            }
            
            if (i == SPEEDUP_IDEAL_PATH)
            {
                if (shadowGraph[cnode].minSync < tpn.minSync) tpn.minSync = shadowGraph[cnode].minSync;
                if (shadowGraph[cnode].maxSync > tpn.maxSync) tpn.maxSync = shadowGraph[cnode].maxSync;
                tpn.numSync += shadowGraph[cnode].numSync;
            }
            //if (i == 1)
            //    printf("%llu\n", lpath);
            
            tpn.bbCount[i] = bbCount + bbCountSum;
            tpn.length[i] = lpath + tpn.duration[i];
            assert(tpn.length[i] > lpath || dur == 0);
            tpn.length_bottle[i] += lbottle;
            if (spath < lpath)
                tpn.slack[i] = (lpath - spath) + slpath;
            else
                tpn.slack[i] = slpath;
            tpn.syncTime[i] += synPath;
            tpn.critNode[i] = cnode;
        }
        //assert(dStart == dur);
		shadowGraph[tid] = tpn;
		
        if (/*true*/tid.getContextId() == 1)
        {
           //printf("%s: %llu <-> %llu\n", tid.toString().c_str(), lastContextTime[tid.getContextId()], tpn.length[BASE_PATH]);
        }
        
		delete currentTask;
		if (tid == roiEnd) break;
	}
	
    delete tg;

    if (smallSlowTask != ~0x0)
        printf("SMALL, %s, %llu, %d\n", smallID.toString().c_str(), smallSlowTask, smallRes);
    else
        printf("SMALL, n/a\n");
    printf("SPEEDUP, %llu, %llu, %llu, %llu, %llu, %llu, %llu, %llu, ", (roiEndTime - roiStartTime),
                                 shadowGraph[roiEnd].length[BASE_PATH], 
                                 shadowGraph[roiEnd].length[SPEEDUP_IDEAL_PATH], 
                                 shadowGraph[roiEnd].length_bottle[SPEEDUP_IDEAL_PATH], 
                                 shadowGraph[roiEnd].slack[SPEEDUP_IDEAL_PATH],
                                 shadowGraph[roiEnd].syncTime[SPEEDUP_IDEAL_PATH],
                                 shadowGraph[roiEnd].length[SPEEDUP_BOTTLE_PATH],
                                 shadowGraph[roiEnd].length_bottle[SPEEDUP_BOTTLE_PATH]);
    printf("%llu, %llu, %llu, %llu, %d, %llu\n", shadowGraph[roiEnd].length[SPEEDUP_2X_PATH], 
                           shadowGraph[roiEnd].length[SPEEDUP_BUFFER_PATH],
                           shadowGraph[roiEnd].length[SPEEDUP_STATIC_PATH],
                           roiStartLen, roiStartBB,
                           shadowGraph[roiEnd].bbCount[SPEEDUP_IDEAL_PATH]);   

    printf("SYNC, %llu, %llu, %llu\n", shadowGraph[roiEnd].minSync, shadowGraph[roiEnd].maxSync,
                                       (shadowGraph[roiEnd].syncTime[SPEEDUP_IDEAL_PATH] / shadowGraph[roiEnd].numSync));
    
    // Plot 1: resource vs cycles of bottlenecked tasks
    // Plot 2: resource vs speedup of 
    // Plot 3: resource vs number of tasks
    
    ct_timestamp cyclesInBottleneck[RES_COUNT + 1] = {0};
    ct_timestamp cyclesSpeedup[RES_COUNT + 1] = {0};
    ct_timestamp cyclesBottleS[RES_COUNT + 1] = {0};
    ct_timestamp cycles2XS[RES_COUNT + 1] = {0};
    ct_timestamp cycles200XS[RES_COUNT + 1] = {0};
    ct_timestamp cyclesStatic[RES_COUNT + 1] = {0};
    uint32_t tasksBottleneck[RES_COUNT + 1] = {0};
    for (unsigned int i = 0; i < pathLimit; i++)
    {
        TaskId currentId = roiEnd;
        
        if (i == SPEEDUP_IDEAL_PATH)
        {
            printf("PATH_IDEAL");
        }
        
        while (currentId != TaskId(0))
        {
            int16_t resID = shadowGraph[currentId].idealRes;
            if (resID >= RES_COUNT || resID < 0) resID = RES_COUNT;
            switch(i)
            {
                case BASE_PATH:
                {
                    cyclesInBottleneck[resID] += shadowGraph[currentId].duration[i];
                    tasksBottleneck[resID]++;
                    if (resID == 0)
                    {
                        /*int8_t bRes, sRes;
                        printf("taskID: %s resID: %d TaskT: %d ", currentId.toString().c_str(), resID, shadowGraph[currentId].tType);
                        printf("Dur: %llu\n", shadowGraph[currentId].duration[i]);
                        printf("\tIDS: %llu(%f)\tBS: %llu(%f)\n", shadowGraph[currentId].duration[SPEEDUP_IDEAL_PATH], 
                                                              pPS->getSpeedup(currentId, &bRes, &sRes),
                                                              shadowGraph[currentId].duration[SPEEDUP_BOTTLE_PATH], 
                                                              pPS->getBottleSpeedup(currentId));*/
                    }
                }
                break;
                case SPEEDUP_IDEAL_PATH:
                {
                    ct_timestamp dur = shadowGraph[currentId].duration[SPEEDUP_IDEAL_PATH];
                    ct_timestamp start = shadowGraph[currentId].length[SPEEDUP_IDEAL_PATH] - dur;
                    
                    cyclesSpeedup[resID] += shadowGraph[currentId].duration[0] - shadowGraph[currentId].duration[i];
                    printf(", %s", currentId.toString().c_str());// TODO: MOVE
                    printf(", %llu, %llu, %d", start,
                                               dur,
                                               getSimpleClass(shadowGraph[currentId].idealRes));
                }
                break;
                case SPEEDUP_BOTTLE_PATH:
                {
                    cyclesBottleS[resID] += shadowGraph[currentId].duration[0] - shadowGraph[currentId].duration[i];
                }
                break;
                case SPEEDUP_2X_PATH:
                {
                    ct_timestamp dur = shadowGraph[currentId].duration[SPEEDUP_2X_PATH];
                    ct_timestamp start = shadowGraph[currentId].length[SPEEDUP_2X_PATH] - dur;
                    
                    cycles2XS[resID] += shadowGraph[currentId].duration[0] - shadowGraph[currentId].duration[i];
                    
                }
                break;
                case SPEEDUP_BUFFER_PATH:
                {
                    //cycles200XS[resID] += shadowGraph[currentId].duration[0] - shadowGraph[currentId].duration[i];
                }
                break;
                case SPEEDUP_STATIC_PATH:
                {
                    cyclesStatic[resID] += shadowGraph[currentId].duration[0] - shadowGraph[currentId].duration[i];
                }
                break;
                default: break;
            }
            
            if (currentId == roiStart) break;
            currentId = shadowGraph[currentId].critNode[i];
        }
        
        if (i == SPEEDUP_IDEAL_PATH)
        {
            printf("\n");
        }
    }
    
    for (unsigned int i = 0; i < (RES_COUNT + 1); i++)
    {
        printf("%llu, %llu, %d, %llu, %llu, %llu\n", cyclesInBottleneck[i], 
                                                     cyclesSpeedup[i], 
                                                     tasksBottleneck[i], 
                                                     cyclesBottleS[i],
                                                     cycles2XS[i],
                                                     //cycles200XS[i],
                                                     cyclesStatic[i]);
    }
    
    for (auto it = staticBBMap.begin(), et = staticBBMap.end(); it != et; ++it)
    {
        for (auto fit = it->second.begin(), fet = it->second.end(); fit != fet; ++fit)
        {
            printf("%d->%d", it->first, fit->first);
            for (unsigned int i = 0; i < (RES_COUNT + 1); i++)
            {
                printf(", %d", fit->second.v[i]);
            }
            
            printf("\n");
        }
    }
    printf("PERCONTEXT");
    for (auto it = perContextConfUsage.begin(), et = perContextConfUsage.end(); it != et; ++it)
    {
        uint32_t max = 0, sum = 0;
        for (unsigned int i = 0; i < (RES_COUNT + 1); i++)
        {
            uint32_t val = it->second.v[i];
            if (val > max) max = val;
            sum += val;
        }
        printf(", %f", ((float)max / (float)sum));
    }
    printf("\n");
    
    //roiStart
    int p = SPEEDUP_IDEAL_PATH;  // set to ideal
    for (unsigned int i = 0; i < numContexts; i++)
    {
        TaskId currentId;
        currentId = contextStart[ContextId(i)];
        //if (ContextId(i) == roiStart.getContextId()) currentId = roiStart;
        //else currentId = TaskId(i, 0);
    
        printf("C%d", i);
        auto it = shadowGraph.find(currentId);
        do 
        {
            ct_timestamp dur = it->second.duration[p];
            ct_timestamp start = it->second.length[p] - dur;
            printf(", %s, %llu, %llu, %d", currentId.toString().c_str(), 
                                       start,
                                       dur,
                                       getSimpleClass(it->second.idealRes));
            currentId = currentId.getNext();
        } while ((it = shadowGraph.find(currentId)) != shadowGraph.end());
        printf("\n");
    }
	
    fclose(taskGraphIn);
    delete pPS;
    
    return 0;
}
