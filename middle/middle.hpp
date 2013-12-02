#include "../common/eventLib/ct_event.h"
#include "../common/taskLib/Task.hpp"

#include "Context.hpp"
#include "BarrierWrapper.hpp"

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

using namespace contech;

int main(int argc, char* argv[]);
void checkContextId(ContextId id);
void eventDebugPrint(TaskId first, string verb, TaskId second, ct_tsc_t start, ct_tsc_t end);

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
};
