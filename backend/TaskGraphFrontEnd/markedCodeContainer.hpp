#ifndef MARKED_CODE_CONTAINER_H
#define MARKED_CODE_CONTAINER_H

#include "markedInstruction.hpp"
#include "markedBlock.hpp"

#include <iostream>
#include <fstream>
#include <map>

class markedCodeContainer
{
public:
    markedBlock* getBlock(unsigned int id);
    static markedCodeContainer* fromFile(std::string in);

private:
    std::map<unsigned int, markedBlock*> blocks;
};

#endif
