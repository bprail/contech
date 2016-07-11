#include "TaskGraphFrontEnd.hpp"

using namespace std;
using namespace contech;

IFrontEnd* createTaskGraphFrontEnd(std::string basename, unsigned int cpuId)
{
    return new TaskGraphFrontEnd(basename, cpuId);
}

unsigned int TaskGraphFrontEnd::getCpuId() { return cpuId; }

bool TaskGraphFrontEnd::isMyTask(Task* task)
{
    // TODO More complex thread -> core mapping
    return task->getContextId() == cpuId;
}

void TaskGraphFrontEnd::TaskGraphRegisterCallback(void (*f)(long))
{
    this->callback = f;
}

TaskGraphFrontEnd::TaskGraphFrontEnd(string basename, unsigned int cpuId)
{
    this->cpuId = cpuId;
    // Load the task graph
    string taskGraphFileName(basename + ".taskgraph");
    taskGraphFile = fopen(taskGraphFileName.c_str(), "rb");
    if (taskGraphFile == NULL) { cerr << "Could not open " << taskGraphFileName << endl; exit(1); }
    if (DEBUG) cerr << "TaskGraphFrontEnd (cpuId=" << cpuId << "): Opened " << taskGraphFileName << endl;
    // Find my first task
    
    tg = TaskGraph::initFromFile(taskGraphFile);
    if (tg == NULL) {}

    TaskId firstId(cpuId, 0);
    currentTask = tg->getContechTask(firstId);
    
    if (currentTask == NULL) { cerr << "No tasks for core " << cpuId << endl; return; }

    // Load the first basic block
    blocksInTask = currentTask->getBasicBlockActions();
    currentBlockId = blocksInTask.begin();
    // Load the first memOp
    memOpsInBlock = currentBlockId.getMemOps();
    currentMemOp = memOpsInBlock.begin();

    // Load the marked code
    markedCode = markedCodeContainer::fromFile(basename + ".mo");
    if (DEBUG) cerr << "TaskGraphFrontEnd (cpuId=" << cpuId << "): Opened " << basename + ".mo" << endl;
    // Load the code for the first basic block
    uint bbId = ((BasicBlockAction)*currentBlockId).basic_block_id;
    currentBlock = markedCode->getBlock(bbId);
    cout << "starting block - " << bbId << endl;
    if (currentBlock == NULL) { cerr << "Error: Marked code not found for basic block " << bbId << endl; exit(1); }
    currentInstruction = currentBlock->begin();
}

bool TaskGraphFrontEnd::getNextInstruction(IFrontEnd::Instruction& inst)
{    
    // Return false after the trace has been exhausted
    if (currentTask == NULL) { return false; }

    // Prepare an instruction to return
    convert(*currentInstruction, inst);
    
    // Get memory ops from the task graph, if necessary
    // No memOps
    if (currentInstruction->reads == 0 && currentInstruction->writes == 0)
    {
        // Nothing to do
    }
    // Single read
    else if (currentInstruction->reads == 1 && currentInstruction->writes == 0)
    {
        if (currentMemOp != memOpsInBlock.end())
        {
            MemoryAction a = *currentMemOp;
            if (a.type == action_type_mem_read)
            {
                inst.readAddr[0] = a.addr;
                currentMemOp++;
            }
            //else { cerr << "Sequence Error: Did not see expected load memOp at " << getPositionString() << endl;}// << currentTask->toString() << endl;}
        }
    }
    // Single write
    else if (currentInstruction->reads == 0 && currentInstruction->writes == 1)
    {
        if (currentMemOp != memOpsInBlock.end())
        {
            MemoryAction a = *currentMemOp;
            if (a.type == action_type_mem_write)
            {
                inst.writeAddr[0] = a.addr;
                currentMemOp++;
            }
            //else {cerr << "Sequence Error: Did not see expected store memOp at " << getPositionString() << endl;}// << currentTask->toString() << endl;}
        }
    }
    // Complex logic for 0-2 reads and 0-2 writes
    else
    {
        // I haven't seen any like this yet, I want the code to stop when I do
        // cerr << "Saw instruction with " << currentInstruction->reads << " reads and " << currentInstruction->writes << " writes:  " << currentInstruction->toString() << endl;
        // assert(0 && "Saw complex memOp");
        
        uint readsRemaining = currentInstruction->reads;
        uint writesRemaining = currentInstruction->writes;
        assert(readsRemaining <= 2 && writesRemaining <= 2);
        while ((readsRemaining > 0 || writesRemaining > 0) && currentMemOp != memOpsInBlock.end())
        {
            MemoryAction a = *currentMemOp++;
            if (a.type == action_type_mem_read)
            {
                inst.readAddr[currentInstruction->reads - readsRemaining] = a.addr;
                readsRemaining--;
            }
            else if (a.type == action_type_mem_write)
            {
                inst.writeAddr[currentInstruction->writes - writesRemaining] = a.addr;
                writesRemaining--;

            } else {
                cerr << currentTask->toString() << endl;
                cerr << "Error: Fetched MemoryAction with bad type." << endl; assert(0);
            }
        }
        
    }

    // Move to the next instruction
    currentInstruction++;
    
    // If we are at the end of the current basic block, get the next one, continuing until we find one with instructions in it
    while (currentInstruction == currentBlock->end())
    {
        // Check to see if we used up all the memOps
        if (currentMemOp != memOpsInBlock.end())
        {
            cerr << "Warning: Was unable to match up all memOps to marked code at " << getPositionString() << endl;
            cerr << "\tDiscarded: ";
            for (;currentMemOp != memOpsInBlock.end(); currentMemOp++)
            {
                cerr << currentMemOp->toString() << " ";
            }
            cerr << endl;
        }

        currentBlockId++;

        // If we are at the end of the current task, get the next one, continuing until we find one with blocks in it
        while (currentBlockId == blocksInTask.end())
        {
            //**CLAY** Callback here **CLAY**
            if (this->callback != NULL)
            {
                this->callback((uint64_t)currentTask->getTaskId());
            }
            
            // Get the next task for this core
            TaskId nextId = currentTask->getTaskId().getNext();
            delete currentTask;
            currentTask = tg->getContechTask(nextId);

            // If there are no more tasks, we are done. The last instruction will return true, the next call will return false
            if (currentTask == NULL) { return true; }

            if (DEBUG) cerr << "Entering task " << currentTask->getTaskId() << endl;

            // Get the blocks for this task
            blocksInTask = currentTask->getBasicBlockActions();
            currentBlockId = blocksInTask.begin();
        }
        

        uint bbId = ((BasicBlockAction)*currentBlockId).basic_block_id;
        if (DEBUG) cerr << "Entering block " << bbId << endl;

        // Get the memOps for the block
        memOpsInBlock = currentBlockId.getMemOps();
        currentMemOp = memOpsInBlock.begin();

        // Look up the instructions for the block
        currentBlock = markedCode->getBlock(bbId);
        if (currentBlock == NULL) {
            cerr << "Error: Marked code not found for basic block " << bbId << endl;
            // Make an empty block
            currentBlock = new markedBlock();
            //exit(1);
        }
        currentInstruction = currentBlock->begin();
    }

    return true;
}

// Returns a string that represents the current position in the trace, mostly for debugging
string TaskGraphFrontEnd::getPositionString()
{
    std::ostringstream out;
    if (currentTask != NULL)
    {
        out << "task " << currentTask->getTaskId() << ", block " << ((BasicBlockAction)*currentBlockId).basic_block_id;
        // TODO Check block for invalid position
        markedBlock::iterator t = currentInstruction;
        while (t != currentBlock->end())
        {
            out << ", instruction " << hex << t->addr;
            ++t;
        }
    }
    else
    {
        out << "end of trace";
    }
    return out.str();
}

void TaskGraphFrontEnd::convert(const markedInstruction& m, IFrontEnd::Instruction& dst)
{
    dst.addr = m.addr;
    memcpy(dst.bytes, m.bytes, m.length * sizeof(dst.bytes[0]));
    dst.length = m.length;
    dst.reads = m.reads;
    dst.writes = m.writes;
    for(int i = 0; i < 16;i++){
        dst.opcode[i] = m.opcode[i];
    }
}
