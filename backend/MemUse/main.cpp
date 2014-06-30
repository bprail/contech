#include "../../common/taskLib/Backend.hpp"
#include "memUse.hpp"

using namespace contech;

int main(int argc, char** argv)
{
    if (argc == 1)
    {
        fprintf(stderr, "Usage: %s <taskgraph file>\n", argv[0]);
        return 1;
    }   

    BackendMemUse* bmu = new BackendMemUse();
    SimpleBackendWrapper* sbw = new SimpleBackendWrapper(argv[1], bmu);
    
    sbw->runBackend();
    sbw->completeRun(stdout);
    delete sbw;
    delete bmu;

    return 0;
}
