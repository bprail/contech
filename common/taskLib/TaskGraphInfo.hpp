#ifndef TASK_GRAPH_INFO_HPP
#define TASK_GRAPH_INFO_HPP

#include "TaskId.hpp"
#include "Action.hpp"
#include "ct_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <sstream>
#include <assert.h>
#include <vector>
#include <map>
#include <algorithm>
#include <inttypes.h>

using namespace std;
namespace contech {

class TaskGraphInfo
{
public:
    TaskGraphInfo(ct_file*);
};

}

#endif