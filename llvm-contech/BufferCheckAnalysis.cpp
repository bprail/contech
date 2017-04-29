

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
	BufferCheckAnalysis::BufferCheckAnalysis(std::map<std::string, int>& blockMemOps_,
		std::map<std::string, bool>& blockElide_, 
		std::map<std::string, Loop*>& loopExits_,
		std::map<std::string, Loop*>& loopBelong_,
      	std::unordered_map<Loop*, std::string>& loopEntry_) :

		blockMemOps{ blockMemOps_ },
		blockElide{ blockElide_ },
		loopBelong{ loopBelong_ },
		loopEntry{ loopEntry_ }
	{
		for (auto kvp : loopExits_) {
			loopExits_[kvp.first] = kvp.second;
		}
	}

	// initialize all basic blocks with 0 memOps
	int BufferCheckAnalysis::blockInitialization()
	{
		return DEFAULT_SIZE;
	}

	// initialize the entry as 0
	// and accumulate throughout
	int BufferCheckAnalysis::entryInitialization()
	{
		return DEFAULT_SIZE;
	}

	int BufferCheckAnalysis::copy(int from)
	{
		return from;
	}

	int BufferCheckAnalysis::merge(int src1, int src2)
	{
		return std::min(src1, src2);
	}

	int BufferCheckAnalysis::accumulateBranch(std::vector<int>& srcs)
	{
		int ret = srcs[0];
		for (int i = 1; i < srcs.size(); ++i) {
			ret = merge(ret, srcs[i]);
		}

		outs() << "accumlate = " << ret << "\n";

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
		// TODO: to the control flow branch into consideration
		int cnt = 0;
		for (auto BIt = lp->block_begin(), BEIt = lp->block_end(); BIt != BEIt; ++BIt) {
			BasicBlock* B = *BIt;
			cnt += getMemUsed(B);
		}

		outs() << "get loop path = " << cnt << "\n";

		return cnt;
	}

	// flow function runs on basic block
	// this function returns the memOps
	// and calculates the bytes used in this basic block
	int BufferCheckAnalysis::flowFunction(int curr, BasicBlock* bb)
	{
		static const int FUNCTION_REMAIN{ 0 };
		static const int LOOP_EXIT_REMAIN{ 30 };
		// it should distinguish whether the terminator is a function
		// or is the end of a loop
		// or is a normal branch 
		auto last = bb->end();
		std::string bb_name{ bb->getName().str() };
		if (isa<CallInst>(last)) {
			outs() << "case func\n";
			// if it is a function, return 0
			// TODO: distinguish library function call
			return FUNCTION_REMAIN;
		}
		else if (loopExits.find(bb_name) != loopExits.end()) {
			outs() << "case loop with " << bb_name << "\n";
			// the block is one exit of a loop
			return LOOP_EXIT_REMAIN - getLoopPath(loopExits[bb_name]);
		}
		else {
			// if it is a jump, return the bytes
			return curr - getMemUsed(bb);
		}
	}

	void BufferCheckAnalysis::prettyPrint()
	{
		std::set<std::string> allBlocks{};
		for (auto kvp : blockMemOps) {
			allBlocks.insert(kvp.first);
		}

		for (auto bb_name : allBlocks) {
			outs() << bb_name << " has " << blockMemOps[bb_name] << " memOps, "
				<< blockElide[bb_name] << " elide\n";
		}
		outs() << "\n";


		// outs() << "loop exits\n";
		// for (std::pair<std::string, Loop*> kvp : loopExits) {
		// 	outs() << kvp.first << "\n";
		// 	Loop* motherLoop = kvp.second;
		// 	outs() << "loop is " << (motherLoop->getLoopDepth()) << "\n";
		// }
		// outs() << "\n";
	}


	// this runs the dataflow analysis
	// it distinguishes two locations:
	// (1) function calls, assume return with 0
	// (2) loop, assume return with 1024 - loop path
	void BufferCheckAnalysis::runAnalysis(Function* fblock)
	{
		std::string entry_name{fblock->begin()->getName().str()};
		// recording the state of the last iteration
		std::map<std::string, int> lastFlowAfter{}, lastFlowBefore{};
		// recording the state of the current iteration
		std::map<std::string, int> currFlowAfter{}, currFlowBefore{};
		// initialize
		for (auto bb = fblock->begin(); bb != fblock->end(); ++bb) {
			std::string bb_name{bb->getName().str()};
			lastFlowAfter[bb_name] = entryInitialization();
			currFlowAfter[bb_name] = entryInitialization();
			lastFlowBefore[bb_name] = DEFAULT_SIZE;
			currFlowBefore[bb_name] = DEFAULT_SIZE;
		}

		bool change = true;
		while (change) {
			change = false;


			lastFlowBefore = currFlowBefore;
			lastFlowAfter = currFlowAfter;

			for (auto B = fblock->begin(); B != fblock->end(); ++B) {
				BasicBlock* bb = &*B;
				// the name of current basic block
				std::string bb_name{ bb->getName().str() };
				// collect all previous states
				// prepare to merge
				std::vector<int> pred_bb_states{};
				for (auto pred_bb = pred_begin(bb);
					pred_bb != pred_end(bb); ++pred_bb) {
					pred_bb_states.push_back(currFlowAfter[(*pred_bb)->getName().str()]);
				}
				// get the initial current state
				int currState{};
				if (bb_name == entry_name || pred_bb_states.size() == 1) {
					// we do not need to merge
					currState = currFlowBefore[bb_name];
				}
				else {
					// we merge all previous branches
					currState = accumulateBranch(pred_bb_states);
				}

				outs() << "currState = " << currState << "\n"; 


				// get the state for this iteration
				// and update the curr flow map
				int nextState = flowFunction(currState, bb);
				currFlowAfter[bb_name] = nextState;

				outs() << "print currFlow\n";
				for (auto kvp : currFlowAfter) {
					outs() << "currFlow[" << kvp.first << "] = " << kvp.second << "\n";
				}
				outs() << "print lastFlow\n";
				for (auto kvp : lastFlowAfter) {
					outs() << "lastFlow[" << kvp.first << "] = " << kvp.second << "\n";
				}
				outs() << "flow size = " << currFlowAfter.size() << "\n";



				// see if the state changes
				if ((lastFlowAfter[bb_name] != currFlowAfter[bb_name]) || 
					(lastFlowBefore[bb_name] != currFlowBefore[bb_name])) {
					// if it changes, we update the flow
					change = true;
				}
				// pass the updated state to all its successors
				// to prepare for next basic block flow
				for (auto next_bb = succ_begin(bb); next_bb != succ_end(bb); ++next_bb) {
					std::string next_bb_name{ next_bb->getName().str() };
					currFlowBefore[next_bb_name] = copy(nextState);
				}


				outs() << "before next iteration\n";
				outs() << "print currFlow\n";
				for (auto kvp : currFlowAfter) {
					outs() << "currFlow[" << kvp.first << "] = " << kvp.second << "\n";
				}
				outs() << "print lastFlow\n";
				for (auto kvp : lastFlowAfter) {
					outs() << "lastFlow[" << kvp.first << "] = " << kvp.second << "\n";
				}
				outs() << "flow size = " << currFlowAfter.size() << "\n";
				outs() << "\n";
			}
		}

		stateAfter = currFlowAfter;
	}
}