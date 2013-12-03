#ifndef COMM_RECORD_H
#define COMM_RECORD_H

#include "../../common/taskLib/Task.hpp"

class CommRecord
{
public:
    contech::TaskId sender;
    uint64_t address;
    contech::TaskId receiver;
    uint srcBlock;
    uint srcMemOpPos;
    uint dstBlock;
    uint dstMemOpPos;
    CommRecord(contech::TaskId sender, uint64_t address, contech::TaskId receiver, uint srcBlock, uint srcPos, uint dstBlock, uint dstPos);
    friend std::ostream& operator<<(std::ostream&, const CommRecord&);
};



#endif
