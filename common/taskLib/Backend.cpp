#include "Backend.hpp"
#include "TaskGraph.hpp"

using namespace contech;

void Backend::initBackend(TaskGraphInfo*) {}

SimpleBackendWrapper::SimpleBackendWrapper(char* f, Backend* b) : backend(b)
{
    tg = TaskGraph::initFromFile(f);
}

void SimpleBackendWrapper::runBackend()
{
    Task* t = NULL;
    
    while ((t = tg->getNextTask()) != NULL)
    {
        backend->updateBackend(t);
        delete t;
    }
}

void SimpleBackendWrapper::initBackend()
{
    backend->initBackend(tg->getTaskGraphInfo());
}

void SimpleBackendWrapper::completeRun(FILE* f)
{
    backend->completeBackend(f, tg->getTaskGraphInfo());
    fflush(f);
}

SimpleBackendWrapper::~SimpleBackendWrapper()
{
    delete tg;
}