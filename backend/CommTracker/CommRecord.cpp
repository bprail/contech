#include "CommRecord.hpp"

using namespace contech;

CommRecord::CommRecord(TaskId sender, uint64_t address, TaskId receiver, uint srcBlock, uint srcPos, uint dstBlock, uint dstPos)
{
    this->sender = sender;
    this->address = address;
    this->receiver = receiver;

    this->srcBlock = srcBlock;
    this->srcMemOpPos = srcPos;
    this->dstBlock = dstBlock;
    this->dstMemOpPos = dstPos;
}

std::ostream& operator<<(std::ostream& out, const CommRecord& rhs){
    std::string q = "\"";
    std::string c = ",";
    std::string o = ":";

    out

    << "{"

    << q << "src" << q << o
    << q
    << rhs.sender
    << q

    << c

    << q << "addr" << q << o
    << q
    << "0x" << std::hex << rhs.address << std::dec
    << q

    << c

    << q << "dst" << q << o
    << q
    << rhs.receiver
    << q

    << c

    << q << "srcB" << q << o
    << q
    << rhs.srcBlock
    << q

    << c

    << q << "dstB" << q << o
    << q
    << rhs.dstBlock
    << q

    << "}";

    return out;
}
