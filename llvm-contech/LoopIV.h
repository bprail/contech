#ifndef _LOOP_H
#define _LOOP_H

#include "llvm/IR/Function.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/ADT/SmallVector.h"

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"

#include "ContechDef.h"

using namespace llvm;
using namespace std;

namespace llvm {
	typedef vector<Instruction *> SmallInstructionVector;

	class LoopIV {

	public:
 		LoopIV(Contech* _ctThis)  {
            ctThis = _ctThis;
        }
		void collectPossibleIVs(Loop *L);
		void collectDerivedIVs(Loop *L, SmallInstructionVector IVs, SmallInstructionVector *DerivedIvs);
		//virtual bool runOnLoop(Loop *L, LPPassManager &LPM);
		virtual bool 	runOnFunction (Function &F);
		SmallInstructionVector getLoopMemoryOps();

	private:
        Contech* ctThis;
		bool isLoopControlIV(Loop *L, Instruction *IV);
        void iterateOnLoop(Loop *L);
		const SCEVConstant *getIncrmentFactorSCEV(ScalarEvolution *SE, const SCEV *SCEVExpr, Instruction &IV); 
		int collectPossibleMemoryOps(GetElementPtrInst* gepAddr, SmallInstructionVector IVs, bool is_derived);
	};
}
#endif 