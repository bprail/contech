
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

	int BufferSizeAnalysis::getMemUsed(BasicBlock* bb)
	{
		int bb_val = blockHash(bb);
		bool isElide = blockElide[bb_val];
		int memCnt = blockMemOps[bb_val];

		return (6 * memCnt) + (isElide ? 0 : 3);
	}
	
	
	int BufferSizeAnalysis::getLoopPath(Loop* lp)
	{
		int cnt = 0;
		for (auto BIt = lp->block_begin(), BEIt = lp->block_end(); BIt != BEIt; ++BIt) {
			BasicBlock* B = *BIt;
			cnt += getMemUsed(B);
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
				if (!called_function->isDeclaration()) {
					return false;
				}
			}
		}

		return true;
	}

	int BufferSizeAnalysis::calculateLinePath(vector<BasicBlock*>& blockLines)
	{
		int sum = 0;
		for (BasicBlock* bb : blockLines) {
			sum += getMemUsed(bb);
		}
		return sum;
	}

	bool BufferSizeAnalysis::isPatternEntry(BasicBlock* bb)
	{
		int bb_val = blockHash(bb);
		return loopExits.find(bb_val) != loopExits.end();
	}

	void BufferSizeAnalysis::accumulatePath(BasicBlock* bb,
		vector<BasicBlock*>& patterns)
	{
		
		while (isValidBlock(bb)) {
			patterns.push_back(bb);
			if (bb->getTerminator()->getNumSuccessors() == 1) {
				bb = *succ_begin(bb);
			}
		}
		if (isPatternExit(bb)) {
			patterns.push_back(bb);
		}
	}


	int BufferSizeAnalysis::runAnalysis(Function* fblock)
	{
		// first check all straight line code
		// within a basic block
		// and see what is the maximum mem ops
		int default_buffer_size = 0;
		for (auto B = fblock->begin(); B != fblock->end(); ++B) {
			BasicBlock* bb = &*B;
			int bb_val = blockHash(bb);
			int bb_mem_used = getMemUsed(bb);
			if (default_buffer_size < bb_mem_used) {
				default_buffer_size = bb_mem_used;
			}
		}
		// second check all if after loop path 
		// length and get its maximum
		int max_loop_path = 0;
		for (auto B = fblock->begin(); B != fblock->end(); ++B) {
			BasicBlock* bb = &*B;
			int bb_val = blockHash(bb);
			vector<BasicBlock*> straightBlocks{};
			// bb is the begin block
			// this returns the longest straight basic blocks path
			// each block should only have one successor
			// b1 -> b2 -> ... -> bn
			if (isPatternEntry(bb)) {
				for (auto NB = succ_begin(bb); NB != succ_end(bb); ++NB) {
					BasicBlock* next_bb = *NB;
					if (isPatternExit(next_bb)) {
						straightBlocks.push_back(bb);
						straightBlocks.push_back(next_bb);
						break;
					}
					else if (isValidBlock(next_bb)) {
						straightBlocks.push_back(bb);
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
		
		return max(default_buffer_size, max_loop_path);
	}
}