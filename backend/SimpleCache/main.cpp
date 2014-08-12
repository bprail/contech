#include "simpleCache.hpp"
#include <stdio.h>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "%s <size of cache, log2> <taskgraph>\n", argv[0]);
        return 1;
    }
    
    if (argc == 3)
    {
        SimpleCacheBackend* bsc = new SimpleCacheBackend(atoi(argv[1]), 3);
        contech::SimpleBackendWrapper* sbw = new contech::SimpleBackendWrapper(argv[2], bsc);
        
        sbw->runBackend();
        sbw->completeRun(stdout);
        delete sbw;
        delete bsc;
    }
    else
    {
        for (int c = 10; c < 17; c++)
        {
            SimpleCacheBackend* bsc = new SimpleCacheBackend(c, 3);
            contech::SimpleBackendWrapper* sbw = new contech::SimpleBackendWrapper(argv[1], bsc);
        
            sbw->runBackend();
            sbw->completeRun(stdout);
            delete sbw;
            delete bsc;
        }
    }
    
    return 0;
}