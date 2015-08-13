#ifndef BBDELTA_HPP
#define BBDELTA_HPP

#include <Backend.hpp>
#include <vector>
#include <deque>
#include <map>
#include <cstdint>

class BBDeltaBackend  : public contech::Backend
{
    std::map <uint32_t, std::map<uint32_t, uint32_t> > bbHistogram;
    std::map <uint32_t, bool> bbInstCall;
    contech::TaskGraphInfo* tgi;
    uint64_t instTime, uninstTime;

public:
    virtual void initBackend(contech::TaskGraphInfo*);
    virtual void resetBackend();
    virtual void updateBackend(contech::Task*);
    virtual void completeBackend(FILE*, contech::TaskGraphInfo*);
    
    BBDeltaBackend();
};

#endif
