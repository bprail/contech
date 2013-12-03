#include "markedInstruction.hpp"
#include <iostream>

markedInstruction markedInstruction::fromString(std::string s)
{
    markedInstruction inst;
    // Format of line:
    // reads writes addr char[] byte byte byte byte ... byte
    std::istringstream stream(s);
    stream >> std::hex >> inst.reads >> std::hex >> inst.writes >> std::hex >> inst.addr;
    stream.get();
    stream.get(inst.opcode,16,' ');
    for(int i = 0; i < 16; i++){
        inst.opcode[i] = tolower(inst.opcode[i]);
    }
    
    for (inst.length = 0; stream >> std::hex >> inst.bytes[inst.length]; inst.length++);

    return inst;
}

std::string markedInstruction::toString()
{
    std::ostringstream out;
    out << reads << " " << writes << " " << length << " " << std::hex << addr << " ";
    for (uint i = 0; i < length; i++)
    {
        out << std::hex << std::setfill('0') << std::setw(2) << bytes[i] << " ";
    }
    return out.str();
}
