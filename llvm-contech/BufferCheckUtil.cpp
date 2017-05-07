

#include "BufferCheckUtil.h"

using namespace llvm;
using namespace std;

namespace llvm {

	BufferCheckUtil::BufferCheckUtil(map<int, int>& blockMemOps_,
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


	int BufferCheckUtil::getMemUsed(BasicBlock* bb)
	{
		int bb_val = blockHash(bb);
		bool isElide = blockElide[bb_val];
		int memCnt = blockMemOps[bb_val];

		return (6 * memCnt) + (isElide ? 0 : 3);
	}

	int BufferCheckUtil::getLoopPath(Loop* lp)
	{
		int cnt = 0;
		for (auto BIt = lp->block_begin(), BEIt = lp->block_end(); BIt != BEIt; ++BIt) {
			BasicBlock* B = *BIt;
			cnt += getMemUsed(B);
		}

		return cnt;
	}

}