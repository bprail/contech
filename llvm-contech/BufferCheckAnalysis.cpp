

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
	BufferCheckAnalysis::BufferCheckAnalysis(std::map<int, int>& blockMemOps_,
		std::map<int, bool>& blockElide_, 
		std::map<int, Loop*>& loopExits_,
		std::map<int, Loop*>& loopBelong_,
      	std::unordered_map<Loop*, int>& loopEntry_) :

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

	bool BufferCheckAnalysis::hasStateChange(std::map<int, int>& last, 
		std::map<int, int>& curr)
	{
		return last != curr;
	}

	int BufferCheckAnalysis::accumulateBranch(std::vector<int>& srcs)
	{

		// outs() << "accumulate status: size = " << srcs.size() << "\n";
		// for (int n : srcs) {
		// 	outs() << n << " ";
		// }
		// outs() << "\n";

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
		int bb_val = blockHash(bb);
		bool isElide = blockElide[bb_val];
		int memCnt = blockMemOps[bb_val];
	
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
		BasicBlock::iterator last = bb->end();
		int bb_val = blockHash(bb);
		if (isa<CallInst>(last)) {
			//outs() << "case func\n";
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
		std::set<int> allBlocks{};
		for (auto kvp : blockMemOps) {
			allBlocks.insert(kvp.first);
		}

		for (int bb_val : allBlocks) {
			outs() << bb_val << " has " << blockMemOps[bb_val] << " memOps, "
				<< blockElide[bb_val] << " elide\n";
		}
		outs() << "\n";

		outs() << "loop exits\n";
		for (std::pair<int, Loop*> kvp : loopExits) {
			outs() << kvp.first << ": ";
			Loop* motherLoop = kvp.second;
			motherLoop->print(outs());
		}

		outs() << "loop belongs\n";
		for (std::pair<int, Loop*> kvp : loopBelong) {
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
		BasicBlock* entry_bb = &*fblock->begin();
		int entry_val = blockHash(entry_bb);
		//outs() << "entry = " << entry_val << "\n";
		// recording the state of the last iteration
		std::map<int, std::map<int, int>> lastFlowAfter{}, lastFlowBefore{};
		// recording the state of the current iteration
		std::map<int, std::map<int, int>> currFlowAfter{}, currFlowBefore{};
		// initialize
		for (auto B = fblock->begin(); B != fblock->end(); ++B) {
			BasicBlock* bb = &*B;
			int bb_val = blockHash(bb);

			for (auto PB = pred_begin(bb); PB != pred_end(bb); ++PB) {
					BasicBlock* prev_bb = *PB;
					int prev_bb_val = blockHash(prev_bb);
					lastFlowBefore[bb_val][prev_bb_val] = DEFAULT_SIZE;
					currFlowBefore[bb_val][prev_bb_val] = DEFAULT_SIZE;
			}

			for (auto NB = succ_begin(bb); NB != succ_end(bb); ++NB) {
				BasicBlock* next_bb = *NB;
				int next_bb_val = blockHash(next_bb);
				lastFlowAfter[bb_val][next_bb_val] = DEFAULT_SIZE;
				currFlowAfter[bb_val][next_bb_val] = DEFAULT_SIZE;
			}	
		}

		bool change = true;
		while (change) {
			change = false;
			// we are going to change the state
			lastFlowBefore = currFlowBefore;
			lastFlowAfter = currFlowAfter;

			for (auto B = fblock->begin(); B != fblock->end(); ++B) {
				BasicBlock* bb = &*B;
				// the name of current basic block
				int bb_val = blockHash(bb);
			
				bool isInsideLoop = loopBelong.find(bb_val) != loopBelong.end();
				Loop* currLoop = isInsideLoop ? loopBelong[bb_val] : nullptr;

				bool hasBlockOutside = false;
				if (isInsideLoop) {
					for (auto PB = pred_begin(bb); PB != pred_end(bb); ++PB) {
						BasicBlock* prev_bb = *PB;
						if (!currLoop->contains(prev_bb)) {
							hasBlockOutside = true;
						}
					}
				}


				// collect all previous states
				// prepare to merge
				std::vector<int> pred_bb_states{};
				for (auto PB = pred_begin(bb); PB != pred_end(bb); ++PB) {
					BasicBlock* prev_bb = *PB;
					int prev_bb_val = blockHash(prev_bb);

					if (!hasBlockOutside || 
						(hasBlockOutside && !currLoop->contains(prev_bb))) {	
						//outs() << "at " << bb_val << " need merge "  << prev_bb_val << "\n";
						pred_bb_states.push_back(currFlowBefore[bb_val][prev_bb_val]);
					}
				}
				// get the initial current state
				int currState{};
				if (bb_val == entry_val) {
					// we do not need to merge
					currState = DEFAULT_SIZE;
				}
				else {
					// we merge all previous branches
					// outs() << "accumulate at " << bb_val << "\n";
					// outs() << bb_val << " contains :\n";
					// for (auto ins = bb->begin(); ins != bb->end(); ++ins) {
					// 	ins->print(outs());
					// 	outs() << "\n";
					// } 
					currState = accumulateBranch(pred_bb_states);
				}

				std::vector<BasicBlock*> allSuccessors{};
				for (auto NB = succ_begin(bb); NB != succ_end(bb); ++NB) {
					BasicBlock* next_bb = *NB;
					allSuccessors.push_back(next_bb);
				}

				std::map<int, int> nextStates{};
				bool isLoopExit = loopExits.find(bb_val) != loopExits.end();
				// update the state according to the current block
				if (isLoopExit) {
					// the current block is loop exit
					// need multiple dispatch
					for (BasicBlock* next_bb : allSuccessors) {
						int next_bb_val = blockHash(next_bb);
						int next = 0;
						if (currLoop->contains(next_bb)) {
							// inside the loop just follow the flow function
							next = flowFunction(currState, bb);
						}
						else {
							// outside loop set to buffer size
							next = LOOP_EXIT_REMAIN - getLoopPath(loopExits[bb_val]);
						}
						//if (next <= 0) { next = DEFAULT_SIZE; }
						nextStates[next_bb_val] = next;
					}
				}
				else {
					// just dispatch to all its successors
					for (BasicBlock* next_bb : allSuccessors) {
						int next_bb_val = blockHash(next_bb);
						nextStates[next_bb_val] = flowFunction(currState, bb);
					}
				}

				// outs() << "next states:\n";
				// for (auto currb : nextStates) {
				// 	outs() << "nextStates[" << currb.first << "] = " 
				// 		<< currb.second << "\n";
				// }

				// get the state for this iteration
				// and update the curr flow map
				currFlowAfter[bb_val] = nextStates;

				// pass the updated state to all its successors
				// to prepare for next basic block flow
				for (auto NB = succ_begin(bb); NB != succ_end(bb); ++NB) {
					BasicBlock* next_bb = *NB;
					int next_bb_val = blockHash(next_bb);
					currFlowBefore[next_bb_val][bb_val] = 
						copy(nextStates[next_bb_val]);
				}


				// outs() << "lastflow after:\n";
				// for (auto currbm : lastFlowAfter) {
				// 	for (auto src : currbm.second) {
				// 		outs() << "lastFlowAfter[" << currbm.first << "][" << src.first << "]" 
				// 			<< " = " << src.second << "\n";
				// 	}
				// }
				// outs() << "currflow after:\n";
				// for (auto currbm : currFlowAfter) {
				// 	for (auto src : currbm.second) {
				// 		outs() << "currFlowAfter[" << currbm.first << "][" << src.first << "]" 
				// 			<< " = " << src.second << "\n";
				// 	}
				// }

				// see if the state changes
				if (hasStateChange(currFlowAfter[bb_val], lastFlowAfter[bb_val])) {
					// if it changes, we update the flow
					change = true;
				}

				//outs() << "iteration finish\n\n";
			}
		}

		stateAfter = currFlowAfter;
	}
}