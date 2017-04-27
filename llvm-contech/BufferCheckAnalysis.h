
#ifndef __BUFFER_CHECK_ANALYSIS_H__
#define __BUFFER_CHECK_ANALYSIS_H__

#include <stdio.h>
#include <iostream>
#include <queue>
#include <list>
#include <vector>
#include <map>
#include <set>

#include "llvm/IR/Instructions.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/IR/CFG.h"

using namespace llvm;

namespace llvm {

class BufferCheckAnalysis
  {
  public:
    BufferCheckAnalysis(std::map<std::string, int>&&, 
      std::map<std::string, bool>&&,
      std::map<std::string, Loop*>&&);
    ~BufferCheckAnalysis() {}
    // the flow analysis components
    int blockInitialization();
    int entryInitialization();
    int copy(int);
    int merge(int, int);
    int flowFunction(int, BasicBlock&);
    // the analysis usage
    void runAnalysis(Function&);
    // helper function
    int getMemUsed(BasicBlock&);
    int getLoopPath(Loop*);
    int accumulateBranch(std::vector<int>&);
  private:
    std::map<std::string, int> blockMemOps;
    std::map<std::string, bool> blockElide;
    std::map<std::string, Loop*> loopExits;
  };

}

#endif