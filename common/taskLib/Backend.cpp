#include "Backend.hpp"
#include "TaskGraph.hpp"

using namespace contech;

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

void SimpleBackendWrapper::completeRun(FILE* f)
{
    backend->completeBackend(f, tg->getTaskGraphInfo());
}

SimpleBackendWrapper::~SimpleBackendWrapper()
{
    delete tg;
}