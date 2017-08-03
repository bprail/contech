#include <vector>
#include "../../common/taskLib/Task.hpp"
#include <iostream>
#include <fstream>
#include "graphviz/gvc.h"
#include <inttypes.h>

using namespace std;

class BasicBlock
{

private:
    uint32_t id;
    uint64_t runCount;
    contech::task_type type;
    Agnode_t* node;
    vector<uint> successors;

public:

    BasicBlock(uint32_t id);

    uint32_t getID();

    uint64_t getRunCount();
    void setRunCount(uint64_t count);
    void incrementRunCount();

    contech::task_type getType();
    void setType(contech::task_type type);

    void setNode(Agnode_t* node);
    Agnode_t* getNode();

    vector<uint32_t> getSuccessors();
    void addSuccessor(uint32_t id);

    static BasicBlock* readBasicBlock(FILE* in);
};
