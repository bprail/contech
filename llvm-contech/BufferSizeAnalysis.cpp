
#include "BufferSizeAnalysis.h"

using namespace llvm;
using namespace std;

namespace llvm {

	BufferSizeAnalysis::BufferSizeAnalysis(map<int, int>& blockMemOps_,
		map<int, bool>& blockElide_,
		map<int, Loop*>& loopExits_,
		map<int, Loop*>& loopBelong_,
		unordered_map<Loop*, int>& loopEntry_) :
		blockMemOps{ blockMemOps_ },
		blockElide{ blockElide_ },
		loopExits{ loopExits_ },
		loopBelong{ loopBelong_ },
		loopEntry{ loopEntry_ }
	{}

	int BufferSizeAnalysis::getMemUsed(int bb_val)
	{
		bool isElide = blockElide[bb_val];
		int memCnt = blockMemOps[bb_val];

		return (6 * memCnt) + (isElide ? 0 : 3);
	}
	
	
	int BufferSizeAnalysis::getLoopPath(Loop* lp)
	{
		int cnt = 0;
		for (auto BIt = lp->block_begin(), BEIt = lp->block_end(); BIt != BEIt; ++BIt) {
			BasicBlock* B = *BIt;
			int bb_val = blockHash(B);
			cnt += getMemUsed(bb_val);
		}

		return cnt;
	}

	bool BufferSizeAnalysis::isPatternExit(BasicBlock* bb)
	{
		int bb_val = blockHash(bb);
		return (bb->getTerminator()->getNumSuccessors() > 1) &&
			loopBelong.find(bb_val) == loopBelong.end();
	}

	bool BufferSizeAnalysis::isValidBlock(BasicBlock* bb)
	{
		int bb_val = blockHash(bb);

		if (bb->getTerminator()->getNumSuccessors() > 1) {
			return false;
		}

		if (loopBelong.find(bb_val) != loopBelong.end()) {
			return false;
		}

		Instruction* last = &*(bb->end());
		for (auto I = bb->begin(); I != bb->end(); ++I) {
			Instruction* inst = &*I;
			if (CallInst* CI = dyn_cast<CallInst>(inst)) {
				Function* called_function = CI->getCalledFunction();
				if (called_function != nullptr && 
					!called_function->isDeclaration()) {
					return false;
				}
			}
		}

		return true;
	}

	int BufferSizeAnalysis::calculateLinePath(vector<int>& blockLines)
	{
		int sum = 0;
		for (int bb_val : blockLines) {
			sum += getMemUsed(bb_val);
		}
		return sum;
	}

	bool BufferSizeAnalysis::isPatternEntry(BasicBlock* bb)
	{
		int bb_val = blockHash(bb);
		return loopExits.find(bb_val) != loopExits.end();
	}

	void BufferSizeAnalysis::accumulatePath(BasicBlock* bb,
		vector<int>& patterns)
	{
		while (isValidBlock(bb) && bb->getTerminator()->getNumSuccessors() > 0) {
			int bb_val = blockHash(bb);
			patterns.push_back(bb_val);
			if (bb->getTerminator()->getNumSuccessors() == 1) {
				bb = *succ_begin(bb);
			}
		}
		if (isPatternExit(bb) || bb->getTerminator()->getNumSuccessors() == 0) {
			int bb_val = blockHash(bb);
			patterns.push_back(bb_val);
		}
	}


	int BufferSizeAnalysis::runAnalysis(Function* fblock)
	{
		// check all if after loop path 
		// length and get its maximum
		int max_loop_path = 0;
		for (auto B = fblock->begin(); B != fblock->end(); ++B) {
			BasicBlock* bb = &*B;
			int bb_val = blockHash(bb);
			//vector<BasicBlock*> straightBlocks{};
			vector<int> straightBlocks{};
			// bb is the begin block
			// this returns the longest straight basic blocks path
			// each block should only have one successor
			// b1 -> b2 -> ... -> bn
			if (isPatternEntry(bb)) {
				for (auto NB = succ_begin(bb); NB != succ_end(bb); ++NB) {
					BasicBlock* next_bb = *NB;
					int next_bb_val = blockHash(next_bb);
					if (isPatternExit(next_bb)) {
						straightBlocks.push_back(bb_val);
						straightBlocks.push_back(next_bb_val);
						break;
					}
					else if (isValidBlock(next_bb)) {
						straightBlocks.push_back(bb_val);
						accumulatePath(next_bb, straightBlocks);
						break;
					}
				}
			}
			int line_path = calculateLinePath(straightBlocks);
			if (max_loop_path < line_path) {
				max_loop_path = line_path;
			}
		}
		return max_loop_path;
	}
}