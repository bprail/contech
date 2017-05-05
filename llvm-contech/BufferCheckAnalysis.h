
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

namespace llvm {

class BufferCheckAnalysis
  {
  public:
    BufferCheckAnalysis(std::map<int, int>&, 
      std::map<int, bool>&,
      std::map<int, Loop*>&,
      std::map<int, Loop*>&,
      std::unordered_map<Loop*, int>&);
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
    int accumulateBranch(std::vector<int>&);
    void prettyPrint();

    std::map<int, bool> getNeedCheckAtBlock() const { return needCheckAtBlock; }

    bool hasStateChange(std::map<int, int>&, 
      std::map<int, int>&);
    std::map<int, std::map<int, int>> getStateAfter() const { return stateAfter; }
  private:
    // analysis parameter
    static const int DEFAULT_SIZE{ 1024 * 1024 };
    static const int FUNCTION_REMAIN{ 0 };
    static const int LOOP_EXIT_REMAIN{ 1024 };

    std::hash<BasicBlock*> blockHash;

    std::map<int, bool> needCheckAtBlock;

    std::map<int, int> blockMemOps;
    std::map<int, bool> blockElide;
    std::map<int, Loop*> loopExits;
    std::map<int, Loop*> loopBelong;
    std::unordered_map<Loop*, int> loopEntry;
    std::map<int, std::map<int, int>> stateAfter;
  };

}

#endif