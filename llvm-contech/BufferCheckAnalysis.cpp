

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
		loopExits{ loopExits_ },
		loopBelong{ loopBelong_ },
		loopEntry{ loopEntry_ }
	{}

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

	bool BufferCheckAnalysis::hasStateChange(std::map<std::string, int>& last, 
		std::map<std::string, int>& curr)
	{
		return last != curr;
	}

	int BufferCheckAnalysis::accumulateBranch(std::vector<int>& srcs)
	{
		int ret = srcs[0];
		for (int i = 1; i < srcs.size(); ++i) {
			ret = merge(ret, srcs[i]);
		}

		//outs() << "accumlate = " << ret << "\n";

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

		//outs() << "get loop path = " << cnt << "\n";

		return cnt;
	}

	// flow function runs on basic block
	// this function returns the memOps
	// and calculates the bytes used in this basic block
	int BufferCheckAnalysis::flowFunction(int curr, BasicBlock* bb)
	{
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
		// else if (loopExits.find(bb_name) != loopExits.end()) {
		// 	outs() << "case loop with " << bb_name << "\n";
		// 	// the block is one exit of a loop
		// 	return LOOP_EXIT_REMAIN - getLoopPath(loopExits[bb_name]);
		// }
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

		outs() << "loop exits\n";
		for (std::pair<std::string, Loop*> kvp : loopExits) {
			outs() << kvp.first << ": ";
			Loop* motherLoop = kvp.second;
			motherLoop->print(outs());
		}

		outs() << "loop belongs\n";
		for (std::pair<std::string, Loop*> kvp : loopBelong) {
			outs() << kvp.first << ": ";
			Loop* motherLoop = kvp.second;
			motherLoop->print(outs());
		}

		outs() << "\n";
	}


	// this runs the dataflow analysis
	// it distinguishes two locations:
	// (1) function calls, assume return with 0
	// (2) loop, assume return with 1024 - loop path
	void BufferCheckAnalysis::runAnalysis(Function* fblock)
	{
		std::string entry_name{fblock->begin()->getName().str()};
		// recording the state of the last iteration
		std::map<std::string, std::map<std::string, int>> lastFlowAfter{}, lastFlowBefore{};
		// recording the state of the current iteration
		std::map<std::string, std::map<std::string, int>> currFlowAfter{}, currFlowBefore{};
		// initialize
		for (auto B = fblock->begin(); B != fblock->end(); ++B) {
			BasicBlock* bb = &*B;
			std::string bb_name{bb->getName().str()};

			for (auto pred_bb = pred_begin(bb);
					pred_bb != pred_end(bb); ++pred_bb) {
					std::string prev_bb_name{ (*pred_bb)->getName().str() };
					lastFlowBefore[bb_name][prev_bb_name] = DEFAULT_SIZE;
					currFlowBefore[bb_name][prev_bb_name] = DEFAULT_SIZE;
			}

			for (auto next_bb = succ_begin(bb); next_bb != succ_end(bb); ++next_bb) {
				std::string next_bb_name{ next_bb->getName().str() };
				lastFlowAfter[bb_name][next_bb_name] = DEFAULT_SIZE;
				currFlowAfter[bb_name][next_bb_name] = DEFAULT_SIZE;
			}	
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
				bool isLoopExit = loopExits.find(bb_name) != loopExits.end();
				Loop* currLoop = isLoopExit ? loopBelong[bb_name] : nullptr;
				// collect all previous states
				// prepare to merge
				std::vector<int> pred_bb_states{};
				for (auto PB = pred_begin(bb);
					PB != pred_end(bb); ++PB) {
					BasicBlock* prev_bb = *PB;
					std::string prev_bb_name{ prev_bb->getName().str() };

					if (!isLoopExit || 
						(isLoopExit && !currLoop->contains(prev_bb))) {	
						outs() << "at " << bb_name << " need merge "  << prev_bb_name << "\n";
						pred_bb_states.push_back(currFlowAfter[bb_name][prev_bb_name]);
					}
				}
				// get the initial current state
				int currState{};
				if (bb_name == entry_name) {
					// we do not need to merge
					currState = DEFAULT_SIZE;
				}
				else {
					// we merge all previous branches
					currState = accumulateBranch(pred_bb_states);
				}

				std::vector<BasicBlock*> allSuccessors{};
				for (auto NB = succ_begin(bb); NB != succ_end(bb); ++NB) {
					BasicBlock* next_bb = *NB;
					allSuccessors.push_back(next_bb);
				}

				std::map<std::string, int> nextStates{};
				// update the state according to the current block
				if (isLoopExit) {
					// the current block is loop exit
					// need multiple dispatch
					for (BasicBlock* next_bb : allSuccessors) {
						std::string next_bb_name{ next_bb->getName().str() };
						int next = 0;
						if (currLoop->contains(next_bb)) {
							// inside the loop just follow the flow function
							next = flowFunction(currState, bb);
						}
						else {
							// outside loop set to buffer size
							next = LOOP_EXIT_REMAIN - getLoopPath(loopExits[bb_name]);
						}
						if (next < 0) { next = DEFAULT_SIZE; }
						nextStates[next_bb_name] = next;
					}
				}
				else {
					// just dispatch to all its successors
					for (BasicBlock* next_bb : allSuccessors) {
						std::string next_bb_name{ next_bb->getName().str() };
						nextStates[next_bb_name] = flowFunction(currState, bb);
					}
				}

				outs() << "next states:\n";
				for (auto currb : nextStates) {
					outs() << "nextStates[" << currb.first << "] = " 
						<< currb.second << "\n";
				}

				// get the state for this iteration
				// and update the curr flow map
				currFlowAfter[bb_name] = nextStates;

				// pass the updated state to all its successors
				// to prepare for next basic block flow
				for (auto next_bb = succ_begin(bb); next_bb != succ_end(bb); ++next_bb) {
					std::string next_bb_name{ next_bb->getName().str() };
					currFlowBefore[next_bb_name][bb_name] = 
						copy(nextStates[next_bb_name]);
				}


				outs() << "lastflow after:\n";
				for (auto currbm : lastFlowAfter) {
					for (auto src : currbm.second) {
						outs() << "lastFlowAfter[" << currbm.first << "][" << src.first << "]" 
							<< " = " << src.second << "\n";
					}
				}
				outs() << "currflow after:\n";
				for (auto currbm : currFlowAfter) {
					for (auto src : currbm.second) {
						outs() << "currFlowAfter[" << currbm.first << "][" << src.first << "]" 
							<< " = " << src.second << "\n";
					}
				}

				// see if the state changes
				if (hasStateChange(currFlowAfter[bb_name], lastFlowAfter[bb_name])) {
					// if it changes, we update the flow
					change = true;
				}

				outs() << "iteration finish\n\n";
			}
		}

		stateAfter = currFlowAfter;
	}
}