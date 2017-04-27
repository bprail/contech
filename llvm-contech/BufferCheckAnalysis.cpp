

#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/IR/CFG.h"

#include "BufferCheckAnalysis.h"

using namespace llvm;

namespace llvm {

	// the constructor
	// we need the memOps of all basic blocks && 
	// elide situation for all basic blocks
	BufferCheckAnalysis::BufferCheckAnalysis(std::map<std::string, int>&& blockMemOps_,
		std::map<std::string, bool>&& blockElide_, 
		std::map<std::string, Loop*>&& loopExits_) :
		blockMemOps{ blockMemOps_ },
		blockElide{ blockElide_ },
		loopExits{loopExits_}
	{
		outs() << "check\n";
	}

	// initialize all basic blocks with 0 memOps
	int BufferCheckAnalysis::blockInitialization()
	{
		return 0;
	}

	// initialize the entry as 0
	// and accumulate throughout
	int BufferCheckAnalysis::entryInitialization()
	{
		return 0;
	}

	int BufferCheckAnalysis::copy(int from)
	{
		return from;
	}

	int BufferCheckAnalysis::merge(int src1, int src2)
	{
		return std::max(src1, src2);
	}

	int BufferCheckAnalysis::accumulateBranch(std::vector<int>& srcs)
	{
		int ret = srcs[0];
		for (int i = 1; i < srcs.size(); ++i) {
			ret = std::max(ret, srcs[i]);
		}
		return ret;
	}

	// uses the data
	// (1) the map locating the number of memops
	// (2) whether the basic block id can be elided
	// to calculate the mem used
	int BufferCheckAnalysis::getMemUsed(BasicBlock* bb)
	{
		std::string bb_name{ bb->getName().str() };
		bool isElide = blockElide[bb_name];
		int memCnt = blockMemOps[bb_name];
	
		return (6 * memCnt) + (isElide ? 0 : 3);
	}

	// calculate the overall byte used in one 
	// iteration of the loop
	int BufferCheckAnalysis::getLoopPath(Loop* lp)
	{
		int cnt = 0;
		for (auto BIt = lp->block_begin(), BEIt = lp->block_end(); BIt != BEIt; ++BIt) {
			BasicBlock* B = *BIt;
			cnt += getMemUsed(B);
		}
		return cnt;
	}

	// flow function runs on basic block
	// this function returns the memOps
	// and calculates the bytes used in this basic block
	int BufferCheckAnalysis::flowFunction(int curr, BasicBlock* bb)
	{
		static const int FUNCTION_REMAIN{ 0 };
		static const int LOOP_EXIT_REMAIN{ 1024 };
		// it should distinguish whether the terminator is a function
		// or is the end of a loop
		// or is a normal branch 
		auto last = bb->end();
		std::string bb_name{ bb->getName().str() };
		if (isa<CallInst>(last)) {
			// if it is a function, return 0
			// TODO: distinguish library function call
			return FUNCTION_REMAIN;
		}
		else if (loopExits.find(bb_name) != loopExits.end()) {
			// the block is one exit of a loop
			return LOOP_EXIT_REMAIN - getLoopPath(loopExits[bb_name]);
		}
		else {
			// if it is a jump, return the bytes
			return curr - getMemUsed(bb);
		}
	}


	// this runs the dataflow analysis
	// it distinguishes two locations:
	// (1) function calls, assume return with 0
	// (2) loop, assume return with 1024 - loop path
	void BufferCheckAnalysis::runAnalysis(Function& fblock)
	{

		outs() << "buffer check\n";

		// recording the state of the last iteration
		std::map<std::string, int> lastFlow{};
		// recording the state of the current iteration
		std::map<std::string, int> currFlow{};

		bool change = true;
		while (change) {
			change = false;
			for (auto B = fblock.begin(); B != fblock.end(); ++B) {
				BasicBlock* bb = &*B;
				// the name of current basic block
				std::string bb_name{ bb->getName().str() };
				// collect all previous states
				// prepare to merge
				std::vector<int> pred_bb_states{};
				for (auto pred_bb = pred_begin(bb);
					pred_bb != pred_end(bb); ++pred_bb) {
					pred_bb_states.push_back(getMemUsed(*pred_bb));
				}
				// get the initial current state
				int currState{};
				if (bb_name == "TODO" || pred_bb_states.size() == 1) {
					// we do not need to merge
					currState = currFlow[bb_name];
				}
				else {
					// we merge all previous branches
					currState = accumulateBranch(pred_bb_states);
				}
				// get the state for this iteration
				// and update the curr flow map
				int nextState = flowFunction(currState, bb);
				currFlow[bb_name] = nextState;
				// see if the state changes
				if (lastFlow[bb_name] != currFlow[bb_name]) {
					// if it changes, we update the flow
					change = true;
				}
				// pass the updated state to all its successors
				// to prepare for next basic block flow
				for (auto next_bb = succ_begin(bb); next_bb != succ_end(bb); ++next_bb) {
					std::string next_bb_name{ next_bb->getName().str() };
					currFlow[next_bb_name] = copy(nextState);
				}
			}
		}
	}
}