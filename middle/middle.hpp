#include "../common/eventLib/ct_event.h"
#include "../common/taskLib/TaskGraph.hpp"

#include "Context.hpp"
#include "BarrierWrapper.hpp"
#include "eventQ.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

#include <map>
#include <deque>
#include <iostream>
#include <bitset>
#include <queue>
// Maximum number of contechs (thread contexts)
#define MAX_CONTECHS 1024

#define MAX_BLOCK_THRESHOLD 10000000

using namespace contech;

void checkContextId(ContextId id);
void eventDebugPrint(TaskId first, string verb, TaskId second, ct_tsc_t start, ct_tsc_t end);

extern ct_tsc_t totalCycles;

class first_compare
{
public:
    bool operator()(pair<ct_tsc_t,TaskId> n1,pair<ct_tsc_t,TaskId> n2)
    {

      if(n1.first>n2.first)
      return true;
      else
      return false;

    }
    
    bool operator()(pair<ct_tsc_t, pair<TaskId, uint64> > n1,pair<ct_tsc_t,pair<TaskId, uint64> > n2)
    {

      if(n1.first>n2.first)
      return true;
      else
      return false;

    }
};

struct mpi_recv_req
{
    int comm_rank;
    int tag;
    ct_addr_t buf_ptr;
    size_t buf_size;
};
