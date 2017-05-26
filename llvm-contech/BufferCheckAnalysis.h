
#ifndef __BUFFER_CHECK_ANALYSIS_H__
#define __BUFFER_CHECK_ANALYSIS_H__

#include <stdio.h>
#include <iostream>
#include <queue>
#include <list>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <functional>

#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/IR/CFG.h"


#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Casting.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Transforms/Utils/LoopUtils.h"


using namespace llvm;
using namespace std;

namespace llvm {

class BufferCheckAnalysis
  {
  public:
    BufferCheckAnalysis(map<int, int>&, 
      map<int, bool>&,
      map<int, Loop*>&,
      map<int, Loop*>&,
      unordered_map<Loop*, int>&, int);
    ~BufferCheckAnalysis() {}
    // the flow analysis components
    int blockInitialization();
    int entryInitialization();
    int copy(int);
    int merge(int, int);
    int flowFunction(int, BasicBlock*);
    // the analysis usage
    void runAnalysis(Function*);
    // helper function
    int getMemUsed(BasicBlock*);
    int getLoopPath(Loop*);
    int accumulateBranch(vector<int>&);
    void prettyPrint();

    map<int, bool> getNeedCheckAtBlock() const { return needCheckAtBlock; }

    bool hasStateChange(map<int, int>&, 
      map<int, int>&);
    map<int, map<int, int>> getStateAfter() const { return stateAfter; }
  private:
    // analysis parameter
    const int DEFAULT_SIZE{ 1024 * 1024 };
    const int FUNCTION_REMAIN;
    const int LOOP_EXIT_REMAIN;

    hash<BasicBlock*> blockHash;

    map<int, bool> needCheckAtBlock;

    map<int, int> blockMemOps;
    map<int, bool> blockElide;
    map<int, Loop*> loopExits;
    map<int, Loop*> loopBelong;
    unordered_map<Loop*, int> loopEntry;
    map<int, map<int, int>> stateAfter;
  };

}

#endif