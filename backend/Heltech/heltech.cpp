#include "heltech.hpp"

using namespace contech;

/**
 * This program takes in a taskgraph file, and searches for data races in it based on the
 * memory access patterns
 */


uint64_t racesReported = 0;
uint64_t memAccesses = 0;
int printVerbose = false;

int main(int argc, char const *argv[])
{
    //input check
    if (argc != 2 && argc != 3)
    {
        cerr << "Usage: " << argv[0] << " [-v]" << " taskGraphInputFile" << endl;
        exit(1);
    }
    
    //Set flag for verbose printing
    string filename;
    if (argc == 3 && strcmp(argv[1],"-v") == 0)
    {
        printVerbose = true;
        filename = argv[2];
    } else {
        filename = argv[1];
    }

    
    //Create file handles for input and output
    //taskGraphIn is for fetching tasks and iterating through all the memOps
    ct_file* taskGraphIn  = create_ct_file_r(filename.c_str());
    if (taskGraphIn == NULL) {
        cerr << "Error: Couldn't open input file" << endl;
        exit(1);
    }
    
    
    //run happened-before race detection algorithm
    hbRaceDetector(taskGraphIn);

    //cleanup
    close_ct_file(taskGraphIn);
}

/*
 * updateVecClock
 *
 *   Updates the vector clock tracking structure with the current task
 *     Also update the ref count so that the tracking structure can be prunned
 */
void updateVecClock(map<ContextId, map<SeqId, map<ContextId, SeqId> > > &vecClock, 
                    map<TaskId, uint> &vecClockRef, Task* t)
{
    TaskId tid = t->getTaskId();
    ContextId ctid = tid.getContextId();
    SeqId seqid = tid.getSeqId();
    auto p = t->getPredecessorTasks();
    map<ContextId, SeqId> predSet;
    
    for (TaskId pid : p)
    {
        predSet[pid.getContextId()] = pid.getSeqId();
    }
    predSet[ctid] = seqid;
    vecClockRef[tid] = t->getSuccessorTasks().size();
    
    for (TaskId pid : p)
    {
        map<ContextId, SeqId> subSet = vecClock[pid.getContextId()][pid.getSeqId()];
        for (auto it = subSet.begin(), et = subSet.end(); it != et; ++it)
        {
            auto ps = predSet.find(it->first);
            if (ps != predSet.end())
            {
                if (it->second > ps->second)
                {
                    predSet[it->first] = it->second;
                }
            }
            else
            {
                predSet[it->first] = it->second;
            }
        }
        vecClockRef[pid] --;
        if (vecClockRef[pid] == 0)
        {
            vecClock[pid.getContextId()].erase(pid.getSeqId());
        }
    }
    
    vecClock[ctid][seqid] = predSet;
}

/*
 * hbPathCheck
 *   return true if a path exists, else false
 *
 *   Uses the vector clock counting to determine whether src is before dst
 */
bool hbPathCheck(map<ContextId, map<SeqId, map<ContextId, SeqId> > > &vecClock, TaskId src, TaskId dst)
{
    auto a = vecClock.find(dst.getContextId());
    if (a != vecClock.end())
    {
        auto b = a->second.find(dst.getSeqId());
        if (b != a->second.end())
        {
            auto c = b->second.find(src.getContextId());
            if (c != b->second.end())
            {
                if (c->second >= src.getSeqId()) return true;
            }
        }
    }
    return false;
}

void hbRaceDetector(ct_file* taskGraphIn)
{
    
    //map of the last access to a specific address, read or write
    map<uint64_t,Heltech_memory_op*> lastAccess;
    
    //Set of addresses where a race currently exists. This serves to reduce redundant race
    //notifications. If a race occurs at an address, then it shouldn't be displayed again
    // until some condition resets the race state
    //TODO think this through, the issue is when the race ends, how to know? I don't think this matters b/c a race is a race.
    //TODO do we only care about addresses or should we be allowed to find races for an address once per contech?
    set<uint64_t> raceAddresses;
    
    set<uint64_t> basicBlocks;
    
    //
    // This structure is like a vector clock, in that in maintains the identity of the newest
    //   task that happens before each task.  When the ref count goes to zero, then all
    //   successor tasks have been processed and the task is no longer part of the frontier
    //   and can be deleted.
    //
    map<ContextId, map<SeqId, map<ContextId, SeqId> > > vecClock;
    map<TaskId, uint> vecClockRef;
    
    // Process the task graph
    TaskGraph* tg = TaskGraph::initFromFile(taskGraphIn);
    if (tg == NULL) {}
    TaskGraphInfo* tgi = tg->getTaskGraphInfo();
    map <uint64_t, uint32_t> allocSize;
    TaskId roiStart = tg->getROIStart(), roiEnd = tg->getROIEnd();
    bool inROI = false;
    uint64_t numRaceAccesses = 0;

    while(Task* currentTask = tg->readContechTask())
    {
            
        TaskId tid = currentTask->getTaskId();
        if (tid == roiStart) {inROI = true;}
        if (tid == roiEnd) {delete currentTask; break;}
        if(printVerbose){
                cout << "Processing task with CTID:  " << tid << endl; 
                cout << "Num Mem Actions: " << currentTask->getMemoryActions().size() << endl;
        }
        //Add to the taskgraph in an online fashion, as tasks come in.
        updateVecClock(vecClock, vecClockRef, currentTask);
        
        //For every basic block
        auto act = currentTask->getBasicBlockActions();
        for (auto f = act.begin(), e = act.end(); f != e; ++f)
        {
            BasicBlockAction bb = *f;
            //Examine every memory operation in the current basic block
            int memOpIndex = 0;
            uint64_t mallocAddr = 0;
            
            for (MemoryAction bMop : f.getMemoryActions())
            {
                unsigned int nBytes = (0x1 << bMop.pow_size);
                bool isRaceAccess = false;
                
                if (bMop.type == action_type_malloc)
                {
                    mallocAddr = bMop.addr;
                    continue;
                }
                
                if (mallocAddr != 0)
                {
                    assert(bMop.type == action_type_size);
                    uint32_t asz = bMop.addr;
                    allocSize[mallocAddr] = asz;
                    mallocAddr = 0;
                    continue;
                }
                
                if (bMop.type == action_type_free)
                {
                    nBytes = allocSize[bMop.addr];
                    allocSize.erase(bMop.addr);
                }
                else
                {
                    memAccesses++;
                }
                
                for (unsigned int sz = 0; sz < nBytes; sz++)
                {
                    MemoryAction newMop(bMop.addr + sz, 0, (action_type)bMop.type);
                    Heltech_memory_op* existingMop = lastAccess[newMop.addr];
            
                    //if this isn't the first access to that address
                    if (existingMop != NULL)
                    {        
                        if (existingMop->tid == tid)
                        {
                            if (existingMop->mop.type == action_type_mem_write) continue;
                            if (newMop.type == action_type_mem_read) continue;
                            delete existingMop;
                            lastAccess[newMop.addr] = new Heltech_memory_op(newMop, bb.basic_block_id, tid);
                            continue;
                        }
                        
                        
                        //note: concurrent reads don't have a problem running in parallel
                        if( (newMop.type == action_type_mem_write || 
                             existingMop->mop.type == action_type_mem_write) && //either access is a write
                            (raceAddresses.count(newMop.addr) == 0))   // and if a race hasn't already been found on this address
                        {
                            //Check if a path exists between the previous memory access and the one
                            //we're currently iterating through . This implies a "happened before" relationship
                            // which is the basis of Helgrind's race detection algorithm.
                            
                            //check if path has been computed/cached
                            bool pathExists = false;

                            pathExists = hbPathCheck(vecClock, existingMop->tid, tid);
                            
                            if (!pathExists && inROI == true) 
                            {
                                reportRace(existingMop->mop,
                                           newMop,
                                           existingMop->tid,
                                           tid,
                                           bb,
                                           existingMop->bbid,
                                           memOpIndex, 
                                           tgi);
                                raceAddresses.insert(newMop.addr);
                                basicBlocks.insert(bb.basic_block_id);
                                isRaceAccess = true;
                            }
                        }
                        
                        if (newMop.type == action_type_mem_write)
                        {
                            delete existingMop;
                            existingMop = NULL;
                        }
                    }
                    
                    //
                    //  We only update the lastAccess on a write, such that the following cases hold:
                    //
                    //  If X writes to A, then Y reads from A and Z reads from A
                    //      This will treat as either XYZ or XZY, but if there is only a HB
                    //      between X and Y, then there is a race to detect a race.
                    //
                    //
                    //  If X writes to A, then X reads from A, and Y reads from A
                    //      This will not be a race, even if there is no HB between X and Y
                    //
                    if (existingMop == NULL)
                    {
                        lastAccess[newMop.addr] = new Heltech_memory_op(newMop, bb.basic_block_id, tid);
                    }
            
                    //handle frees by clearing out that memory location from the last access
                    //and then adding the free to the list of existing mops
                    if (newMop.type == action_type_free)
                    {
                        delete lastAccess[newMop.addr];
                        lastAccess[newMop.addr] = NULL;
                        raceAddresses.erase(newMop.addr);
                    }
                }
                memOpIndex++;
                
                if (isRaceAccess == true)
                {
                    numRaceAccesses++;
                }
            }
        }

        delete currentTask;
    }
    delete tg;
    
    printf("Race Bytes, Accesses, Race Accesses, Basic Blocks with Races\n");
    printf("%llu, %llu, %llu, %llu\n", racesReported, memAccesses, numRaceAccesses, (basicBlocks.size()));
}


/**
 * Provides pretty formatting for the race being detected
 */
void reportRace(MemoryAction existingMop,
                MemoryAction newMop,
                TaskId existingTID,
                TaskId newTID,
                BasicBlockAction bb,
                unsigned int srcBBID,
                int idx,
                TaskGraphInfo* tgi)
{
    
    //Increment the number of races found
    racesReported++;

    if (!printVerbose) return;
    
    string raceReason = "";
    // write and write
    if((newMop.type == action_type_mem_write && existingMop.type == action_type_mem_write)){
        raceReason = "There are concurrent writes, without any ordering constraints, that could overwrite each other";
    } else if(existingMop.type == action_type_mem_write || newMop.type == action_type_mem_write){
        raceReason = "There are possibly concurrent reads and writes without any ordering constraints.";
    }
    
    cerr << "**************There may be a race!!**************" << endl;
    cerr << "Reason: " << raceReason << endl;
    cerr << "Conflicting access address: " << hex << newMop.addr << dec << "(Idx:" << idx << ")" << " in (Contech:Task) -- ("
        << existingTID << ") and (" 
        << newTID << ")\n" << endl;
    cerr << bb << endl;
    {
        auto bbi = tgi->getBasicBlockInfo(srcBBID);
        fprintf(stderr, "%u, %s, %s:%u\n",
                                         srcBBID, 
                                         bbi.functionName.c_str(), 
                                         bbi.fileName.c_str(),
                                     bbi.lineNumber);
    }
    {
        unsigned int bbid = bb.basic_block_id;
        auto bbi = tgi->getBasicBlockInfo(bbid);
        fprintf(stderr, "%u, %s, %s:%u\n",
                                         bbid, 
                                         bbi.functionName.c_str(), 
                                         bbi.fileName.c_str(),
                                     bbi.lineNumber);
    }                             
    cerr << endl;
}

/**
 *Constructor for the Heltech_memory_op container
 */
Heltech_memory_op::Heltech_memory_op(MemoryAction mop, unsigned int b, TaskId tid){
    this->mop = mop;
    this->bbid = b;
    this->tid = tid;
}