#include "BasicBlock.hpp"

using namespace contech;

BasicBlock::BasicBlock(uint32_t id) { this->id = id; this->runCount = 0; this->type = task_type_basic_blocks; }

uint32_t BasicBlock::getID() { return id; }
uint64_t BasicBlock::getRunCount() { return runCount; }
vector<uint32_t> BasicBlock::getSuccessors() { return successors; }
void BasicBlock::addSuccessor(uint32_t id) { successors.push_back(id); }
void BasicBlock::setRunCount(uint64_t count) { runCount = count; }
void BasicBlock::incrementRunCount() { runCount++; }

void BasicBlock::setNode(Agnode_t* node) { this->node = node; }
Agnode_t* BasicBlock::getNode() { return node; }

task_type BasicBlock::getType() { return type; }
void BasicBlock::setType(task_type type) { this->type = type; }

BasicBlock* BasicBlock::readBasicBlock(FILE* in)
{
    if (feof(in) || ferror(in)) return NULL;
    uint32_t id;
    uint32_t typeInt;
    int succ1;
    int succ2;
    if (4 == fscanf(in, "%u,%u,%i,%i", &id, &typeInt, &succ1, &succ2))
    {
        BasicBlock* b = new BasicBlock((uint32_t)id);
        b->setType((task_type)typeInt);
        if (succ1 != -1) b->addSuccessor((uint32_t)succ1);
        if (succ2 != -1) b->addSuccessor((uint32_t)succ2);
        return b;
    } else {
        return NULL;
    }
}
