#ifndef MARKED_INSTRUCTION_H
#define MARKED_INSTRUCTION_H

#include <inttypes.h>
#include <string>
#include <sstream>
#include <iomanip>

class markedInstruction
{
public:
    uint32_t addr;
    uint length;
    uint bytes[15];
    uint reads;
    uint writes;
    char opcode[16];

    static markedInstruction fromString(std::string s);
    std::string toString();
};

#endif
