
#ifndef __BUFFER_CHECK_UTIL__
#define __BUFFER_CHECK_UTIL__

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
	
	class BufferCheckUtil
	{
	public:
		BufferCheckUtil(map<int, int>& blockMemOps,
			map<int, bool>& blockElide,
			map<int, Loop*>& loopExits,
			map<int, Loop*>& loopBelong,
			unordered_map<Loop*, int>& loopEntry);
		int getMemUsed(BasicBlock*);
		int getLoopPath(Loop*);
	private:
		std::hash<BasicBlock*> blockHash;
		map<int, int> blockMemOps;
		map<int, bool> blockElide;
		map<int, Loop*> loopExits;
		map<int, Loop*> loopBelong;
		unordered_map<Loop*, int> loopEntry;
	};

}


#endif
