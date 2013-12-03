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
	if(argc != 2 && argc != 3){
		cerr << "Usage: " << argv[0] << " [-v]" << " taskGraphInputFile" << endl;
		exit(1);
	}
	
	//Set flag for verbose printing
	string filename;
	if(argc == 3 && strcmp(argv[1],"-v")==0){
		printVerbose = true;
		filename = argv[2];
	} else {
		filename = argv[1];
	}

	
	//Create file handles for input and output
	//taskGraphIn is for fetching tasks and interating through all the memOps
	ct_file* taskGraphIn  = create_ct_file_r(filename.c_str());
	if(isClosed(taskGraphIn)){
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

void hbRaceDetector(ct_file* taskGraphIn){
	
	//Minimal graph map that only establishes relationships in the graph. This avoids having to
	//read and write large tasks repeatedly to/from disk/memory.
	unordered_map<TaskId,vector<TaskId>> graphMap;
	
	//map of the last access to a specific address, read or write
	map<uint64_t,Heltech_memory_op*> lastAccess;
	
	//Set of addresses where a race currently exists. This serves to reduce redundant race
	//notifications. If a race occurs at an address, then it shouldn't be displayed again
	// until some condition resets the race state
	//TODO think this through, the issue is when the race ends, how to know? I don't think this matters b/c a race is a race.
	//TODO do we only care about addresses or should we be allowed to find races for an address once per contech?
	set<uint64_t> raceAddresses;
	
	set<uint64_t> basicBlocks;
    
	//caching paths already decided
	unordered_map<TaskId,unordered_map<TaskId,bool>> pathCache;
	
	// Process the task graph
	Task* currentTask;
	while(currentTask = Task::readContechTask(taskGraphIn)){
		
		TaskId ctid = currentTask->getTaskId();
		if(printVerbose){
			cout << "Processing task with CTID:  " << ctid << endl; 
			cout << "Num Mem Actions: " << currentTask->getMemoryActions().size() << endl;
		}
		//Add to the taskgraph in an online fashion, as tasks come in.
		addTaskToGraphMap(graphMap,currentTask);
		
		//For every basic block
		for (auto f = currentTask->getBasicBlockActions().begin(), e = currentTask->getBasicBlockActions().end(); f != e; f++){
			BasicBlockAction bb = *f;
			//Examine every memory operation in the current basic block
			int memOpIndex = 0;
			for (MemoryAction newMop : f.getMemoryActions()){
				Heltech_memory_op* existingMop = lastAccess[newMop.addr];
			
				//if this isn't the first access to that address
				if(existingMop != NULL){
					
					if(existingMop->ctid == ctid &&
					   existingMop->mop.type == newMop.type){
						continue;
					}
					
					
					//note: concurrent reads don't have a problem running in parallel
					if( (newMop.type == action_type_mem_write || existingMop->mop.type == action_type_mem_write) && //either access is a write
					   //TODO what about the case where you have a free/malloc
					    (raceAddresses.count(newMop.addr) == 0))   // and if a race hasn't already been found on this address
					{
						//Check if a path exists between the previous memory access and the one
						//we're currently iterating through . This implies a "happened before" realtionship
						// which is the basis of Helgrind's race detection algorithm.
						
						//check if path has been computed/cached
						bool pathExists = false;
						if(pathCache[existingMop->ctid][ctid] == true){
						    pathExists = true;
						} else {
						    //TODO optimization in distinguishing between false and yet-to-be-computed
						    pathExists = hbPathExists(graphMap,existingMop->ctid,ctid);
						    
						    pathCache[existingMop->ctid][ctid] = pathExists;
						}
						
						if(!pathExists){
						    reportRace(existingMop->mop,newMop,existingMop->ctid,ctid,bb,memOpIndex);
						    raceAddresses.insert(newMop.addr);
						    basicBlocks.insert(bb.basic_block_id);
						}
						
					}
					
					delete existingMop;
				}
				
				lastAccess[newMop.addr] = new Heltech_memory_op(newMop,ctid);
			
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
 * Checks whether a path exists between the start and end CTID's in the task graph.
 * If a path exists between the two tasks, then we can safely say that 'start' happened before 'end'
 */
bool hbPathExists(unordered_map<TaskId,vector<TaskId>>& graphMap, TaskId start, TaskId end){
    
    bool pathExists = false;
    
    //Queue of tasks to visit
    queue<TaskId> q;
    
    //Already visited nodes
    set<TaskId> history;
    history.clear();
    
    //Add the first node to visit
    q.push(start);
    
    Task* nextTask = NULL;
    while(!q.empty()){
        TaskId next = q.front();
	
	//If we've already visisted this node, skip it
	if(history.count(next) != 0){
            q.pop();
            continue;
        }
        
        if(next == end){ //we've found a path that exists
            pathExists = true;
            break;
        } else { // otherwise add the children to be checked.
            
	    //Add to history
	    history.insert(next);
	    
            for(int i=0;i<graphMap[next].size();i++){
                //graphmap will return an uninitialized TaskId as having all its fields set to zero
		if(graphMap[next][i] != 0){
                    q.push(graphMap[next][i]);
                }
            }
            
            q.pop();
        }
        
        
	}
    return pathExists;
}


/**
 * Provides pretty formatting for the race being detected
 */
void reportRace(MemoryAction existingMop,MemoryAction newMop,TaskId existingCTID,TaskId newCTID,BasicBlockAction bb,int idx){
    
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
 * For a task, add that task and its successor edges to the graphMap
 */
void addTaskToGraphMap(unordered_map<TaskId,vector<TaskId>>& graphMap, Task* t){
	vector<TaskId>& children = t->getSuccessorTasks();
        graphMap[t->getTaskId()] = children;
}


/**
 *Constructor for the Heltech_memory_op container
 */
Heltech_memory_op::Heltech_memory_op(MemoryAction mop,TaskId ctid){
    this->mop = mop;
    this->ctid = ctid;
}