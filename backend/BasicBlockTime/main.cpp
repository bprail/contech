#include "bbDelta.hpp"
#include <stdio.h>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "%s <taskgraph>\n", argv[0]);
        return 1;
    }

    BBDeltaBackend* bsc = new BBDeltaBackend();
    contech::SimpleBackendWrapper* sbw = new contech::SimpleBackendWrapper(argv[1], bsc);
    
    sbw->initBackend();
    sbw->runBackend();
    sbw->completeRun(stdout);
    delete sbw;
    delete bsc;
    
    return 0;
}