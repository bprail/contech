

#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/IR/CFG.h"

#include "BufferCheckAnalysis.h"

using namespace llvm;
using namespace std;

namespace llvm 
{

	// the constructor
	// we need the memOps of all basic blocks && 
	// elide situation for all basic blocks
	BufferCheckAnalysis::BufferCheckAnalysis(map<int, llvm_inst_block>& blockInfo_,
		map<int, Loop*>& loopExits_,
		map<int, Loop*>& loopBelong_,
      	unordered_map<Loop*, int>& loopEntry_, 
      	int bufferCheckSize_) :

		blockInfo{ blockInfo_ },
		loopExits{ loopExits_ },
		loopBelong{ loopBelong_ },
		loopEntry{ loopEntry_ },
		FUNCTION_REMAIN{ bufferCheckSize_ },
		LOOP_EXIT_REMAIN{ bufferCheckSize_ }
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
		return min(src1, src2);
	}

	bool BufferCheckAnalysis::hasStateChange(map<int, int>& last, map<int, int>& curr)
	{
		return last != curr;
	}

	// accumulate the state from multiple branches
	int BufferCheckAnalysis::accumulateBranch(vector<int>& srcs)
	{
		int ret = srcs[0];
		for (int i = 1; i < srcs.size(); ++i) 
        {
			ret = merge(ret, srcs[i]);
		}
		return ret;
	}

	// uses the data
	// (1) the map locating the number of memops
	// (2) whether the basic block id can be elided
	// to calculate the mem used
	int BufferCheckAnalysis::getMemUsed(BasicBlock* bb)
	{
		int bb_val = blockHash(bb);
		return blockInfo.find(bb_val)->second.cost;
	}

	// calculate the overall byte used in one 
	// iteration of the loop
	int BufferCheckAnalysis::getLoopPath(Loop* lp)
	{
		int cnt = 0;
		for (auto BIt = lp->block_begin(), BEIt = lp->block_end(); BIt != BEIt; ++BIt) 
        {
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
		// it should distinguish whether the terminator is a function
		// or is the end of a loop
		// or is a normal branch 
		Instruction* last = &*bb->end();
		int bb_val = blockHash(bb);
		if (CallInst* CI = dyn_cast<CallInst>(last)) 
        {
			Function* called_function = CI->getCalledFunction();
			// if it is a function, return default value
			if (called_function == nullptr ||
				!called_function->isDeclaration()) 
            {
				return FUNCTION_REMAIN;
			}
		}

		// if it is a jump, return the bytes
		// otherwise just calculate normally
		return curr - getMemUsed(bb);
	}

	void BufferCheckAnalysis::prettyPrint()
	{
		set<int> allBlocks{};
		for (auto kvp : blockInfo) 
        {
			allBlocks.insert(kvp.first);
		}

		for (int bb_val : allBlocks) 
        {
			outs() << bb_val << " has " << blockInfo.find(bb_val)->second.cost << "cost.\n";
		}
		outs() << "\n";

		outs() << "loop exits\n";
		for (pair<int, Loop*> kvp : loopExits) 
        {
			outs() << kvp.first << ": ";
			Loop* motherLoop = kvp.second;
			motherLoop->print(outs());
		}

		outs() << "loop belongs\n";
		for (pair<int, Loop*> kvp : loopBelong) 
        {
			outs() << kvp.first << ": ";
			Loop* motherLoop = kvp.second;
			motherLoop->print(outs());
		}

		outs() << "\n";
	}


	// this runs the dataflow analysis
	// it distinguishes two locations:
	// (1) function calls, assume return with default size
	// (2) loop, assume return with default size - loop path
	void BufferCheckAnalysis::runAnalysis(Function* fblock)
	{
		BasicBlock* entry_bb = &*fblock->begin();
		int entry_val = blockHash(entry_bb);
		// recording the state of the last iteration
		map<int, map<int, int>> lastFlowAfter{}, lastFlowBefore{};
		// recording the state of the current iteration
		map<int, map<int, int>> currFlowAfter{}, currFlowBefore{};
		// initialize
		for (auto B = fblock->begin(); B != fblock->end(); ++B) 
        {
			BasicBlock* bb = &*B;
			int bb_val = blockHash(bb);
			// previous branches
			for (auto PB = pred_begin(bb); PB != pred_end(bb); ++PB) 
            {
					BasicBlock* prev_bb = *PB;
					int prev_bb_val = blockHash(prev_bb);
					lastFlowBefore[bb_val][prev_bb_val] = DEFAULT_SIZE;
					currFlowBefore[bb_val][prev_bb_val] = DEFAULT_SIZE;
			}
			// coming branches
			for (auto NB = succ_begin(bb); NB != succ_end(bb); ++NB) 
            {
				BasicBlock* next_bb = *NB;
				int next_bb_val = blockHash(next_bb);
				lastFlowAfter[bb_val][next_bb_val] = DEFAULT_SIZE;
				currFlowAfter[bb_val][next_bb_val] = DEFAULT_SIZE;
			}	
		}
		bool change = true;
		while (change) 
        {
			change = false;
			// we are going to change the state
			lastFlowBefore = currFlowBefore;
			lastFlowAfter = currFlowAfter;

			for (auto B = fblock->begin(); B != fblock->end(); ++B) 
            {
				BasicBlock* bb = &*B;
				// the name of current basic block
				int bb_val = blockHash(bb);
			
				bool isInsideLoop = loopBelong.find(bb_val) != loopBelong.end();
				Loop* currLoop = isInsideLoop ? loopBelong[bb_val] : nullptr;
				// see if the previous branches have
				// outside loop edges
				bool hasBlockOutside = false;
				if (isInsideLoop) 
                {
					for (auto PB = pred_begin(bb); PB != pred_end(bb); ++PB) 
                    {
						BasicBlock* prev_bb = *PB;
						if (!currLoop->contains(prev_bb)) 
                        {
							hasBlockOutside = true;
						}
					}
				}
				// collect all previous states
				// prepare to merge
				// only mergeee blocks with outside loop edges
				vector<int> pred_bb_states{};
				for (auto PB = pred_begin(bb); PB != pred_end(bb); ++PB) 
                {
					BasicBlock* prev_bb = *PB;
					int prev_bb_val = blockHash(prev_bb);
					if (!hasBlockOutside || 
						(hasBlockOutside && !currLoop->contains(prev_bb))) 
                    {	
						pred_bb_states.push_back(currFlowBefore[bb_val][prev_bb_val]);
					}
				}
				// get the initial current state
				int currState{};
				if (bb_val == entry_val) 
                {
					// we do not need to merge
					currState = DEFAULT_SIZE;
				}
				else 
                {
					currState = accumulateBranch(pred_bb_states);
				}

				vector<BasicBlock*> allSuccessors{};
				for (auto NB = succ_begin(bb); NB != succ_end(bb); ++NB) 
                {
					BasicBlock* next_bb = *NB;
					allSuccessors.push_back(next_bb);
				}

				map<int, int> nextStates{};
				bool isLoopExit = loopExits.find(bb_val) != loopExits.end();
				// update the state according to the current block
				int next = 0;
				if (isLoopExit)
                {
					// the current block is loop exit
					// need multiple dispatch
					for (BasicBlock* next_bb : allSuccessors) 
                    {
						int next_bb_val = blockHash(next_bb);
						if (currLoop->contains(next_bb)) 
                        {
							// inside the loop just follow the flow function
							next = flowFunction(currState, bb);
						}
						else 
                        {
							// outside loop set to buffer size
							next = LOOP_EXIT_REMAIN - getLoopPath(loopExits[bb_val]);
						}
						// the state becomes nonpositve
						// that means we need to add a check here
						if (next <= 0) 
                        { 
							next = DEFAULT_SIZE; 
							needCheckAtBlock[bb_val] = true;
						}
						nextStates[next_bb_val] = next;
					}
				}
				else 
                {
					// just dispatch to all its successors
					for (BasicBlock* next_bb : allSuccessors)
                    {
						int next_bb_val = blockHash(next_bb);
						int next = flowFunction(currState, bb);
						if (next <= 0) 
                        {
							next = DEFAULT_SIZE;
							needCheckAtBlock[bb_val] = true;
						}
						nextStates[next_bb_val] = next;
					}
				}
				// get the state for this iteration
				// and update the curr flow map
				currFlowAfter[bb_val] = nextStates;

				// pass the updated state to all its successors
				// to prepare for next basic block flow
				for (auto NB = succ_begin(bb); NB != succ_end(bb); ++NB) 
                {
					BasicBlock* next_bb = *NB;
					int next_bb_val = blockHash(next_bb);
					currFlowBefore[next_bb_val][bb_val] = 
						copy(nextStates[next_bb_val]);
				}
				// see if the state changes
				if (hasStateChange(currFlowAfter[bb_val], lastFlowAfter[bb_val])) 
                {
					// if it changes, we update the flow
					change = true;
				}
			}
		}
		// update the global state
		stateAfter = currFlowAfter;
	}
}