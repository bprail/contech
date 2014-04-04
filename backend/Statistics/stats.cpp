#include "../../common/taskLib/TaskGraph.hpp"
#include <algorithm>
#include <iostream>
#include <set>

using namespace std;
using namespace contech;

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

    uint64 totalTasks = 0;
    uint64 totalBasicBlocks = 0;
    uint64 totalMemOps = 0;

    double averageBasicBlocksPerTask = 0;
    uint maxBasicBlocksPerTask = 0;

    double averageMemOpsPerTask = 0;
    uint maxMemOpsPerTask = 0;

    double averageMemOpsPerBasicBlock = 0;
    uint maxMemOpsPerBasicBlock = 0;

    uint syncCount = 0;
    uint barrCount = 0;
    uint createCount = 0;
    uint joinCount = 0;

    set<uint> uniqueBlocks;

    TaskGraph* tg = TaskGraph::initFromFile(taskGraphIn);
    if (tg == NULL) {}

    while(Task* currentTask = tg->readContechTask()){

        totalTasks++;
        uint basicBlocksInTask = 0;
        uint memOpsInTask = 0;

        switch(currentTask->getType())
        {
            case task_type_basic_blocks:
                for (auto f = currentTask->getBasicBlockActions().begin(), e = currentTask->getBasicBlockActions().end(); f != e; f++)
                {
                    BasicBlockAction bb = *f;
                    uniqueBlocks.insert((uint)bb.basic_block_id);
                    totalBasicBlocks++;
                    basicBlocksInTask++;

                    uint memOpsInBlock = 0;
                    for (MemoryAction mem : f.getMemoryActions())
                    {
                        totalMemOps++;
                        memOpsInTask++;
                        memOpsInBlock++;
                    }

                    maxMemOpsPerBasicBlock = max(maxMemOpsPerBasicBlock, memOpsInBlock);
                }

                maxBasicBlocksPerTask = max(maxBasicBlocksPerTask, basicBlocksInTask);
                maxMemOpsPerTask = max(maxMemOpsPerTask, memOpsInTask);

                break;

            case task_type_sync:
                syncCount++;
                break;

            case task_type_barrier:
                barrCount++;
                break;

            case task_type_create:
                createCount++;
                break;

            case task_type_join:
                joinCount++;
                break;

        }

        delete currentTask;
    }

    delete tg;

    averageMemOpsPerBasicBlock = ((double)totalMemOps / totalBasicBlocks);
    averageBasicBlocksPerTask = ((double)totalBasicBlocks / totalTasks);
    averageMemOpsPerTask = ((double)totalMemOps / totalTasks);


    printf("\n");
    printf("Statistics for %s\n", argv[1]);
    printf("----------------------------------\n");
    printf("Total Tasks: %llu\n", totalTasks);
    printf("\n");
    printf("Unique Basic Blocks: %llu\n", uniqueBlocks.size());
    printf("Average Basic Blocks per Task: %f\n", averageBasicBlocksPerTask);
    printf("Max Basic Blocks per Task: %u\n", maxBasicBlocksPerTask);
    printf("\n");
    printf("Total MemOps: %llu\n", totalMemOps);
    printf("Average MemOps per Task: %f\n", averageMemOpsPerTask);
    printf("Max MemOps per Task: %u\n", maxMemOpsPerTask);
    printf("Average MemOps per Basic Block: %f\n", averageMemOpsPerBasicBlock);
    printf("Max MemOps per Basic Block: %u\n", maxMemOpsPerBasicBlock);
    printf("\n");
    printf("Sync tasks: %u\n", syncCount);
    printf("Barrier tasks: %u\n", barrCount);
    printf("Create tasks: %u\n", createCount);
    printf("Join tasks: %u\n", joinCount);
    printf("\n");


    close_ct_file(taskGraphIn);
    return 0;
}
