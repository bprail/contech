#include "heltech.hpp"

using namespace contech;

/**
 * This program takes in a taskgraph file, and searches for data races in it based on the
 * memory access patterns
 */


int racesReported = 0;
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
    //taskGraphIn is for fetching tasks and interating through all the memOps
    ct_file* taskGraphIn  = create_ct_file_r(filename.c_str());
    if (taskGraphIn == NULL) {
        cerr << "Error: Couldn't open input file" << endl;
        exit(1);
    }
    
    
    //run happened-before race detection algorithm
    cerr << "Starting Race Detection" << endl;
    hbRaceDetector(taskGraphIn);
    cerr << endl;
    
    printf("Done processing taskgraph file \n");

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
    Task* currentTask;
    while(currentTask = Task::readContechTask(taskGraphIn)){
            
        TaskId ctid = currentTask->getTaskId();
        if(printVerbose){
                cout << "Processing task with CTID:  " << ctid << endl; 
                cout << "Num Mem Actions: " << currentTask->getMemoryActions().size() << endl;
        }
        //Add to the taskgraph in an online fashion, as tasks come in.
        updateVecClock(vecClock, vecClockRef, currentTask);
        
        //For every basic block
        auto act = currentTask->getBasicBlockActions();
        for (auto f = act.begin(), e = act.end(); f != e; ++f){
            BasicBlockAction bb = *f;
            //Examine every memory operation in the current basic block
            int memOpIndex = 0;
            
            for (MemoryAction newMop : f.getMemoryActions()){
                Heltech_memory_op* existingMop = lastAccess[newMop.addr];
            
                //if this isn't the first access to that address
                if(existingMop != NULL){
                        
                    if (existingMop->ctid == ctid)
                    {
                        if (existingMop->mop.type == action_type_mem_write) continue;
                        if (newMop.type == action_type_mem_read) continue;
                        delete existingMop;
                        lastAccess[newMop.addr] = new Heltech_memory_op(newMop,ctid);
                        continue;
                    }
                    
                    
                    //note: concurrent reads don't have a problem running in parallel
                    if( (newMop.type == action_type_mem_write || 
                         existingMop->mop.type == action_type_mem_write) && //either access is a write
                       //TODO what about the case where you have a free/malloc
                        (raceAddresses.count(newMop.addr) == 0))   // and if a race hasn't already been found on this address
                    {
                        //Check if a path exists between the previous memory access and the one
                        //we're currently iterating through . This implies a "happened before" realtionship
                        // which is the basis of Helgrind's race detection algorithm.
                        
                        //check if path has been computed/cached
                        bool pathExists = false;

                        pathExists = hbPathCheck(vecClock, existingMop->ctid, ctid);
                        
                        if (!pathExists) 
                        {
                            reportRace(existingMop->mop,newMop,existingMop->ctid,ctid,bb,memOpIndex);
                            raceAddresses.insert(newMop.addr);
                            basicBlocks.insert(bb.basic_block_id);
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
                    lastAccess[newMop.addr] = new Heltech_memory_op(newMop,ctid);
                }
        
                //handle frees by clearing out that memory location from the last access
                //and then adding the free to the list of existing mops
                if(newMop.type == action_type_free){
                    delete lastAccess[newMop.addr];
                    lastAccess[newMop.addr] = NULL;
                    raceAddresses.erase(newMop.addr);
                }
                    
                memOpIndex++;
            }
        }

        delete currentTask;
    }
    
    if(racesReported == 0){
        cerr << "\nNo data races found!!!" << endl;
    } else {
        cerr << "\nERROR: " << racesReported << " races observed!!!" << endl;
        cerr << "Number of BB's containing races: " << basicBlocks.size() << endl;
    }
}


/**
 * Provides pretty formatting for the race being detected
 */
void reportRace(MemoryAction existingMop,
                MemoryAction newMop,
                TaskId existingCTID,
                TaskId newCTID,
                BasicBlockAction bb,
                int idx)
{
    
    //Increment the number of races found
    racesReported++;

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
        << existingCTID << ") and (" 
        << newCTID << ")\n" << endl;
    cerr << bb << endl;
    cerr << endl;
}

/**
 *Constructor for the Heltech_memory_op container
 */
Heltech_memory_op::Heltech_memory_op(MemoryAction mop,TaskId ctid){
    this->mop = mop;
    this->ctid = ctid;
}