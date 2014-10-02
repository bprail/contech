#ifndef CONTECH_BACKEND_HPP
#define CONTECH_BACKEND_HPP

#include "TaskGraph.hpp"

namespace contech
{

class Backend
{
public:
    virtual void initBackend(contech::TaskGraphInfo*);
    virtual void resetBackend() = 0;
    virtual void updateBackend(contech::Task*) = 0;
    virtual void completeBackend(FILE*, contech::TaskGraphInfo*) = 0;
};

class SimpleBackendWrapper
{
private:
    TaskGraph* tg;
    Backend* backend;

public:
    SimpleBackendWrapper(char*, contech::Backend*);
    ~SimpleBackendWrapper();
    void runBackend();
    void initBackend();
    void completeRun(FILE*);
};

};

#endif
