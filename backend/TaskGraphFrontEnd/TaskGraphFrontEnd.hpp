#ifndef TASKGRAPH_FRONTEND_H
#define TASKGRAPH_FRONTEND_H

#include "../../common/taskLib/TaskGraph.hpp"
#include "markedCodeContainer.hpp"
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include "IFrontEnd.hpp"

#define DEBUG true

class TaskGraphFrontEnd : public IFrontEnd
{
public:
    TaskGraphFrontEnd(std::string basename, unsigned int cpuId);

    virtual unsigned int getCpuId();
    virtual bool getNextInstruction(IFrontEnd::Instruction& inst);
    virtual string getPositionString();
    virtual void TaskGraphRegisterCallback(void (*param)(long));

private:
    
    unsigned int cpuId;
    bool isMyTask(contech::Task* task);
    void (*callback)(long) = NULL;
    void convert(const markedInstruction& m, IFrontEnd::Instruction& dst);

    // Maintain position within the task graph
    FILE* taskGraphFile;
    contech::TaskGraph* tg;
    contech::Task* currentTask;
    contech::Task::basicBlockActionCollection blocksInTask;
    contech::Task::basicBlockActionCollection::iterator currentBlockId;
    contech::Task::memOpCollection memOpsInBlock;
    contech::Task::memOpCollection::iterator currentMemOp;

    // Maintain position within the marked code container
    markedCodeContainer* markedCode;
    markedBlock* currentBlock;
    markedBlock::iterator currentInstruction;

};


#endif
