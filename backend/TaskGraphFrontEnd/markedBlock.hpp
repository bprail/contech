#ifndef MARKED_BLOCK_H
#define MARKED_BLOCK_H

#include "markedInstruction.hpp"
#include <vector>

class markedBlock
{
    // markedCodeContainer basically does all the construction of this class by inserting instructions
    friend class markedCodeContainer;

public:
    typedef std::vector<markedInstruction>::iterator iterator;

    iterator begin();
    iterator end();
private:

    std::vector<markedInstruction> instructions;
};

#endif
