#include "Task.hpp"

// Helper functions
Task* createTestTask();

// Tests
void regularTaskTest();
void compressedTaskTest();
void iterationTest();

int main(int argc, char* argv[])
{
    printf("\n==Testing iteration over memory actions==\n");
    iterationTest();

    printf("\n==Testing regular task==\n");
    regularTaskTest();

    printf("\n==Testing compressed task==\n");
    compressedTaskTest();
    return 0;
}

Task* createTestTask()
{
    Task* task = new Task(14, 15, task_type_basic_blocks);
    task->recordBasicBlockAction(16);
    task->recordMemOpAction(1, 1, 17);
    task->recordMemOpAction(1, 0, 18);
    task->recordBasicBlockAction(19);
    task->recordMemOpAction(1, 1, 20);
    task->recordMemOpAction(1, 1, 21);
    task->recordBasicBlockAction(22);
    task->recordMemOpAction(1, 1, 23);
    // TODO test malloc/free
    task->setChildTaskId(24);
    task->setContinuationTaskId(25);
    task->setParentTaskId(26);
    task->setPredecessorTaskId(27);
    task->setReqTime(28);
    cout << task->toString() << endl;
    return task;
}

void regularTaskTest(){
    printf("Creating a task...\n");
    Task* task = createTestTask();

    printf("Calling writeContechTask...\n");
    ct_file* out = create_ct_file_w("testTaskGraph.temp",false);
    Task::writeContechTask(*task, out);
    close_ct_file(out);

    printf("Calling readContechTask...\n");
    ct_file* in = create_ct_file_r("testTaskGraph.temp");
    Task* task2 = Task::readContechTask(in);

    printf("Checking that task data was recovered correctly...\n");
    assert(*task == *task2);

    printf("Checking for correct EOF handling...\n");
    assert(Task::readContechTask(in) == NULL);
    close_ct_file(in);

    delete task;
    delete task2;
    printf("Test PASSED\n");
}

void compressedTaskTest(){
    printf("Creating a task...\n");
    Task* task = createTestTask();

    printf("Calling writeContechTask...\n");
    ct_file* out = create_ct_file_w("compressedTestTaskGraph.temp",true);
    Task::writeContechTask(*task, out);
    close_ct_file(out);

    printf("Calling readContechTask...\n");
    ct_file* in = create_ct_file_r("testTaskGraph.temp");
    Task* task2 = Task::readContechTask(in);

    printf("Checking that task data was recovered correctly...\n");
    assert(*task == *task2);

    printf("Checking for correct EOF handling...\n");
    assert(Task::readContechTask(in) == NULL);
    close_ct_file(in);

    delete task;
    delete task2;
    printf("Test PASSED\n");
}

void iterationTest()
{
    printf("Creating a task...\n");
    Task* task = createTestTask();

    printf("Iterating over basic blocks:\n");
    for (BasicBlockAction bb : task->getBasicBlockActions())
    {
        printf("BB: %llu\n", bb.basic_block_id);
    }

    printf("Iterating over memory actions:\n");
    for (MemoryAction mem : task->getMemoryActions())
    {
        printf("Addr: %llu\n", mem.addr);
    }

    printf("Iterating over basic blocks, then memory actions\n");
    for (auto f = task->getBasicBlockActions().begin(), e = task->getBasicBlockActions().end(); f != e; f++)
    {
        BasicBlockAction bb = *f;
        printf("BB: %llu\n", bb.basic_block_id);
        for (MemoryAction mem : f.getMemoryActions())
        {
            printf("\tAddr: %llu\n", mem.addr);
        }
    }

}
