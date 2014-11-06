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
#include <string>
#include <inttypes.h>

using namespace std;
namespace contech {

#define BBI_FLAG_CONTAIN_CALL 0x1
#define BBI_FLAG_CONTAIN_GLOBAL_ACCESS 0x2

class BasicBlockInfo
{
public:
    uint lineNumber;
    uint numOfMemOps;
    uint numOfOps;
    uint critPathLen;
    uint flags;
    string functionName;
    string fileName;
    //vector <uint> typeOfMemOps;
};

class FunctionInfo
{
public:
    string functionName;
    vector <uint> typeOfArguments;
    uint returnType;
};

class TypeInfo
{
    string typeName;
    size_t sizeOfType;
};

class TaskGraphInfo
{
private:
    map <uint, BasicBlockInfo> bbInfo;
    //map <uint, TypeInfo> tyInfo;
    //map <uint, FunctionInfo> funInfo;
    //map <uint, string> fileName;
    
public:    
    void initTaskGraphInfo(ct_file*);
    TaskGraphInfo();
    
    void addRawBasicBlockInfo(uint bbid, uint flags, uint lineNum, uint numMemOps, uint numOps, uint critPathLen, string function, string file);
    void writeTaskGraphInfo(ct_file*);
    
    BasicBlockInfo& getBasicBlockInfo(uint bbid);
};

}

#endif