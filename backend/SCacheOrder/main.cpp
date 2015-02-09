#include "simpleCache.hpp"
#include <stdio.h>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "%s <size of cache, log2> <taskgraph>\n", argv[0]);
        return 1;
    }
    
    #if 0
    if (argc == 3)
    {
        SimpleCacheBackend* bsc = new SimpleCacheBackend(atoi(argv[1]), 2, 1);
        contech::SimpleBackendWrapper* sbw = new contech::SimpleBackendWrapper(argv[2], bsc);
        
        sbw->runBackend();
        sbw->completeRun(stdout);
        delete sbw;
        delete bsc;
    }
    else
    {
        for (int c = 10; c <= 28; c++)
        {
            SimpleCacheBackend* bsc = new SimpleCacheBackend(c, 2, 0);
            contech::SimpleBackendWrapper* sbw = new contech::SimpleBackendWrapper(argv[1], bsc);
        
            sbw->runBackend();
            sbw->completeRun(stdout);
            delete sbw;
            delete bsc;
        }
    }
    
    #endif
    if (argc == 3)
    {
        TraceWrapper* tw = new TraceWrapper(argv[2]);
        SimpleCacheBackend* bsc = new SimpleCacheBackend(atoi(argv[1]), 2, 1);
        MemReqContainer mrc;
        
        while (tw->getNextMemoryRequest(mrc))
        {
            bsc->updateBackend(mrc);
        }
        
        bsc->completeBackend(stdout, tw->tg->getTaskGraphInfo());
        delete bsc;
        delete tw;
    }
    else
    {
        for (int c  = 10; c <= 28; c++)
        {
            TraceWrapper tw(argv[1]);
            SimpleCacheBackend bsc(c, 2, 0);
            
            MemReqContainer mrc;
            while (tw.getNextMemoryRequest(mrc))
            {
                bsc.updateBackend(mrc);
            }
            
            bsc.completeBackend(stdout, tw.tg->getTaskGraphInfo());
            fflush(stdout);
        }
    
        #if 0
        TraceWrapper* tw;
        
        MemReqContainer mrc;
        int c_start = 10, c_end = 29, c_pt;
        
        while (c_start < c_end)
        {
            vector<SimpleCacheBackend*> scbv;
            tw = new TraceWrapper(argv[1]);
            
            c_pt = c_start + 2;
            if (c_pt > c_end) c_pt = c_end;
            
            for (int c = c_start; c < c_pt; c++)
            {
                //printf("", c);
                //fflush(stdout);
                SimpleCacheBackend* bsc = new SimpleCacheBackend(c, 2, 0);
                scbv.push_back(bsc);
            }
            
            while (tw->getNextMemoryRequest(mrc))
            {
                for (auto bsc : scbv)
                {
                    bsc->updateBackend(mrc);
                }
            }
            
            for (auto bsc : scbv)
            {
                bsc->completeBackend(stdout, tw->tg->getTaskGraphInfo());
                delete bsc;
            }
            fflush(stdout);
            
            c_start += 2;
            delete tw;
        }
        #endif
    }
    
    return 0;
}