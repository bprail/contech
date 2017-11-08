// 15-745 Project
// Group:
////////////////////////////////////////////////////////////////////////////////

#include "LoopIV.h"

using namespace llvm;
using namespace std;


#include "llvm/ADT/SmallSet.h"
static cl::opt<unsigned> MaxInc("max-reroll-increment1", cl::init(2048), 
                                cl::Hidden, cl::desc("The maximum increment for loop rerolling"));

namespace llvm{
    bool isCompareUsedByBranch(Instruction *I) {
        auto *TI = I->getParent()->getTerminator();
        if (!isa<BranchInst>(TI) || !isa<CmpInst>(I))
            return false;
        return I->hasOneUse() && TI->getOperand(0) == I;
    }

    DenseMap<Instruction *, int64_t> IVToIncMap;

    vector<Instruction*>invar;
    vector<Value*>def;
    ScalarEvolution *SE;
    Instruction* LoopControlIV;
    SmallInstructionVector PossibleIVs;
    SmallInstructionVector NonLinearIvs;
    SmallInstructionVector DerivedLinearIvs;
    SmallInstructionVector DerivedNonlinearIvs;
    LlvmLoopIVBlockVector LoopMemoryOps;

    int cnt_GetElementPtrInst = 0;
    int cnt_elided = 0;
    int cnt_future_elided = 0;

    // Check if it is a compare-like instruction whose user is a branch
    bool LoopIV::isLoopControlIV(Loop *L, Instruction *IV) 
    {
        unsigned IVUses = IV->getNumUses();
        if (IVUses != 2 && IVUses != 1)
        {
            return false;
        }

        for (auto *User : IV->users()) 
        {
            int32_t IncOrCmpUses = User->getNumUses();
            bool IsCompInst = isCompareUsedByBranch(cast<Instruction>(User));

            // User can only have one or two uses.
            if (IncOrCmpUses != 2 && IncOrCmpUses != 1)
            {
                return false;
            }

            // Case 1
            if (IVUses == 1) 
            {
              // The only user must be the loop increment.
              // The loop increment must have two uses.
                if (IsCompInst || IncOrCmpUses != 2)
                    return false;
            }

            // Case 2
            if (IVUses == 2 && IncOrCmpUses != 1)
            {
                return false;
            }

            // The users of the IV must be a binary operation or a comparison
            if (auto *BO = dyn_cast<BinaryOperator>(User)) 
            {
                if (BO->getOpcode() == Instruction::Add) 
                {
                    // Loop Increment
                    // User of Loop Increment should be either PHI or CMP
                    for (auto *UU : User->users()) {
                        if (PHINode *PN = dyn_cast<PHINode>(UU)) 
                        {
                            if (PN != IV)
                            {
                                return false;
                            }
                        }
                          // Must be a CMP or an ext (of a value with nsw) then CMP
                        else {
                            Instruction *UUser = dyn_cast<Instruction>(UU);
                            // Skip SExt if we are extending an nsw value
                            // TODO: Allow ZExt too
                            if (BO->hasNoSignedWrap() && UUser && UUser->getNumUses() == 1 &&
                                isa<SExtInst>(UUser))
                            {
                                UUser = dyn_cast<Instruction>(*(UUser->user_begin()));
                            }
                            if (!isCompareUsedByBranch(UUser))
                            {
                                return false;
                            }
                        }
                    }
                } 
                else
                {
                    return false;
                }
              // Compare : can only have one use, and must be branch
            } 
            else if (!IsCompInst)
            {
                return false;
            }
        }
        
        return true;
    }

    const SCEVConstant *LoopIV::getIncrmentFactorSCEV(ScalarEvolution *SE,
        const SCEV *SCEVExpr,
        Instruction &IV) 
    {
        const SCEVMulExpr *MulSCEV = dyn_cast<SCEVMulExpr>(SCEVExpr);

      // If StepRecurrence of a SCEVExpr is a constant (c1 * c2, c2 = sizeof(ptr)),
      // Return c1.
        if (!MulSCEV && IV.getType()->isPointerTy())
        {
            if (const SCEVConstant *IncSCEV = dyn_cast<SCEVConstant>(SCEVExpr)) 
            {
                const PointerType *PTy = cast<PointerType>(IV.getType());
                Type *ElTy = PTy->getElementType();
                const SCEV *SizeOfExpr = SE->getSizeOfExpr(SE->getEffectiveSCEVType(IV.getType()), ElTy);
                
                if (IncSCEV->getValue()->getValue().isNegative()) 
                {
                    const SCEV *NewSCEV = SE->getUDivExpr(SE->getNegativeSCEV(SCEVExpr), SizeOfExpr);
                    return dyn_cast<SCEVConstant>(SE->getNegativeSCEV(NewSCEV));
                } 
                else 
                {
                    return dyn_cast<SCEVConstant>(SE->getUDivExpr(SCEVExpr, SizeOfExpr));
                }
            }
        }

        if (!MulSCEV)
        {
            return nullptr;
        }

      // If StepRecurrence of a SCEVExpr is a c * sizeof(x), where c is constant,
      // Return c.
        const SCEVConstant *CIncSCEV = nullptr;
        for (const SCEV *Operand : MulSCEV->operands()) 
        {
            if (const SCEVConstant *Constant = dyn_cast<SCEVConstant>(Operand)) 
            {
                CIncSCEV = Constant;
            } 
            else if (const SCEVUnknown *Unknown = dyn_cast<SCEVUnknown>(Operand)) 
            {
                Type *AllocTy;
                if (!Unknown->isSizeOf(AllocTy)) break;
            } 
            else 
            {
                return nullptr;
            }
        }
        return CIncSCEV;
    }

    void LoopIV::collectPossibleIVs(Loop *L) 
    {
        int cnt = 0;
        PossibleIVs.clear();
        NonLinearIvs.clear();
        BasicBlock *Header = L->getHeader();
        for (BasicBlock::iterator I = Header->begin(),
            IE = Header->getFirstInsertionPt(); I != IE; ++I, cnt++ ) 
        {

            //errs() << " INST " << *I << "\n";
            if (!isa<PHINode>(I)) 
            {
                continue;
            }

            if (!I->getType()->isIntegerTy() && !I->getType()->isPointerTy()) 
            {
                //errs() << "IS NOT INTEGEER OR POINTER TYPE\n";
                continue;
            }

            const SCEV *S = SE->getSCEV(&*I);
            const SCEVAddRecExpr *SARE = dyn_cast<SCEVAddRecExpr>(S);
            if (SARE != NULL) 
            {
                const Loop *CurLoop = SARE->getLoop();
                if (CurLoop == L) 
                {
                    if(SARE->getNumOperands() > 2) 
                    {
                        NonLinearIvs.push_back(&*I);
                        //errs() << "\nNON_LINEAR " << *I << " = " << *SARE << "\n\n" ;
                        //errs() << "Num of operands:" << SARE->getNumOperands() << "\n";
                        for (auto count = SARE->op_begin(); count != SARE->op_end(); ++count)
                        {
                            if(dyn_cast<SCEVConstant>(*count)) 
                            {
                                //errs() << "## " << *dyn_cast<SCEVConstant>(*count) << "\n";
                            }
                        }
                        continue;
                    }
                }
            }
            //errs() << "SCEV " << *SE->getSCEV(&*I) << "\n";
            if (const SCEVAddRecExpr *PHISCEV = dyn_cast<SCEVAddRecExpr>(SE->getSCEV(&*I))) 
            {
                if (PHISCEV->getLoop() != L) 
                {
                    //errs() << "GETLOOP NOT EQUAL L\n";
                    continue;
                }
                if (PHISCEV->isQuadratic()) 
                {
                    //errs() << *I << " = " << *PHISCEV << "IS QUADRATIC\n";
                }

                if (!PHISCEV->isAffine()) 
                {
                    //errs() << "IS NOT AFFINEE\n";
                    continue;
                }
                
                const SCEVConstant *IncSCEV = nullptr;
                if (I->getType()->isPointerTy()) 
                {
                    IncSCEV = getIncrmentFactorSCEV(SE, PHISCEV->getStepRecurrence(*SE), *I);
                    //errs() << "SCEV isPointerTy " << *I << "\n";
                }
                else 
                {
                    IncSCEV = dyn_cast<SCEVConstant>(PHISCEV->getStepRecurrence(*SE));
                    //errs() << "SCEV consant " << *IncSCEV << "\n";
                }

                if (IncSCEV) 
                {
                    const APInt &AInt = IncSCEV->getValue()->getValue().abs();
                    if (IncSCEV->getValue()->isZero() || AInt.uge(MaxInc))
                        continue;
                    IVToIncMap[&*I] = IncSCEV->getValue()->getSExtValue();

                    //errs() << "LRR: Possible IV: " << *I << " = " << *PHISCEV << "\n";
                    
                    if (isLoopControlIV(L, &*I)) 
                    {
                          //assert(!LoopControlIV && "Found two loop control only IV");
                          //LoopControlIV = &(*I);
                         //errs() << "LRR: Possible loop control only IV: " << *I << " = "
                          //             << *PHISCEV << "\n";
                    } 
                    else 
                    {
                      //errs() << "PossibleIVs " << *I << "\n";
                        PossibleIVs.push_back(&*I);
                    }
                }
            }
        }
    }

    void LoopIV::collectDerivedIVs(Loop *L, SmallInstructionVector IVs, SmallInstructionVector *DerivedIvs)
    {
        DerivedIvs->clear();
        
        for (Loop::block_iterator bb = L->block_begin(); bb != L->block_end(); ++bb) 
        {
            BasicBlock* b = (*bb);

            for (BasicBlock::iterator I = b->begin(); I != b->end(); ++I)
            {
                if (!I->getType()->isIntegerTy() && !I->getType()->isPointerTy()) 
                {
                    //errs() << "IS NOT INTEGEER OR POINTER TYPE\n";
                    continue;
                }
                
                if (std::find(PossibleIVs.begin(), PossibleIVs.end(), (&*I)) != PossibleIVs.end()) 
                {
                    continue;
                }
                if (std::find(NonLinearIvs.begin(), NonLinearIvs.end(), (&*I)) != NonLinearIvs.end()) 
                {
                    continue;
                }

                if (!isa<BinaryOperator>(*I)) 
                {
                    continue;
                }

                Instruction* temp = cast<Instruction>(&*I);
                unsigned cnt = 0;
                bool is_derived = false;
                for (unsigned i = 0; i < temp->getNumOperands(); i++) 
                {
                    Instruction* nl = dyn_cast<Instruction>(&*temp->getOperand(i));
                    if (isa<Argument>(temp->getOperand(i))) 
                    {
                        break;
                    }
                    if (isa<Constant>(temp->getOperand(i)))     //if operand if const
                    {
                        cnt++;
                    }
                    else if (L->isLoopInvariant(temp->getOperand(i)))     //or it is loop invariant
                    {
                        cnt++;
                    }
                    else if (std::find(IVs.begin(), IVs.end(), temp->getOperand(i)) != IVs.end())     //or it is an IV
                    {
                        cnt++;
                        is_derived = true;
                    }
                    else if (nl != NULL) 
                    {
                        //if derived from non-linear
                        for (unsigned j = 0; j < nl->getNumOperands(); j++)
                        {
                            if (std::find(IVs.begin(), IVs.end(), nl->getOperand(j)) != IVs.end())
                            {
                                cnt++;
                                is_derived = true;
                            }
                        }
                    }
                }
                if (is_derived && cnt == temp->getNumOperands()) 
                {
                    DerivedIvs->push_back(&*I);
                    //get SCEV
                }
            }
        }
    }
    
    Value* LoopIV::isAddOrPHIConstant(Value* v, bool retFin)
    {
        bool zeroOrOneConstant = true;
    
        do 
        {
            Instruction* inst = dyn_cast<Instruction>(v);
            if (inst == NULL) break;
            
            switch(inst->getOpcode())
            {
                case Instruction::Add:
                case Instruction::Sub:
                case Instruction::Mul:
                case Instruction::Shl:
                {
                    Value* vOp;
                    zeroOrOneConstant = false;
                    for (int i = 0; i < 2; i++)
                    {
                        vOp = inst->getOperand(i);
                        if (ConstantInt* ci = dyn_cast<ConstantInt>(vOp))
                        {
                            zeroOrOneConstant = true;
                        }
                        else
                        {
                            v = vOp;
                        }
                    }
                }
                break;
                case Instruction::PHI:
                {
                    return inst;
                }
                break;
                default:
                {
                    zeroOrOneConstant = false;
                }
                break;
            }
            
        } while (zeroOrOneConstant);
        
        if (retFin == true) return v;
        return NULL;
    }

    // For the case where the base address is the IV.
    Value* LoopIV::inferredMemoryOps(GetElementPtrInst* gepAddr, bool is_derived, Loop* L, llvm_loopiv_block &llb)
    {
        if (gepAddr == NULL) return NULL;
        llb.addtComponents.clear();
        
        Value* baseAddr = gepAddr->getPointerOperand();
        if (PHINode* pn = dyn_cast<PHINode>(baseAddr))
        {
            llb.startIV = NULL;
            for (int i = 0; i < pn->getNumIncomingValues(); i++)
            {
                BasicBlock* pnBB = pn->getIncomingBlock(i);
                Value* v = pn->getIncomingValue(i);
                
                if (L->contains(pnBB) == true)
                {
                    Instruction* inst = dyn_cast<Instruction>(v);
                    if (inst != NULL)
                    {
                        llb.stepBlock = inst->getParent();
                    }
                    else
                    {
                        llb.stepBlock = pnBB;
                    }
                }
                else
                {
                    llb.startIV = v;
                }
            }
            if (llb.startIV == NULL) return NULL;
        }
        else
        {
            return NULL;
        }
        
        for (auto itG = gep_type_begin(gepAddr), etG = gep_type_end(gepAddr); itG != etG; ++itG) 
        {
            Value* gepI = itG.getOperand();
            Instruction* temp = dyn_cast<Instruction>(gepI);
            if (dyn_cast<ConstantInt>(gepI)) 
            {
                continue;
            }
            else if (temp == NULL)
            {
                if (dyn_cast<Argument>(gepI)) {llb.addtComponents.push_back(gepI); continue;}
                continue;
            }
            else
            {
                Value* basePhiV = isAddOrPHIConstant(gepI, false);
                if (basePhiV == NULL) return NULL;
                
                Instruction* basePhi = dyn_cast<Instruction>(basePhiV);
                if (basePhi == NULL) return NULL;
                
                if (L->isLoopInvariant(gepI) == false) return NULL;
                Value* v = isAddOrPHIConstant(gepI, true);
                if (v != NULL) {llb.addtComponents.push_back(v);}
                else return NULL;
            }
        }
        
        llb.memOp = NULL;
        llb.canElide = true;
        llb.stepIV = IVToIncMap[dyn_cast<Instruction>(baseAddr)];
        
        errs() << "HIT " << *gepAddr << "\t" << llb.stepIV << "\n";
        
        return gepAddr;
    }
    
    // TODO: the value is always an instruction, adjust the calls to return precisely
    Value* LoopIV::collectPossibleMemoryOps(GetElementPtrInst* gepAddr, SmallInstructionVector IVs, bool is_derived, Loop* L, vector<Value*>& ac)
    {
        Value* cnt_elided = NULL;
        bool non_const = false;
        ac.clear();
        
        if (gepAddr != NULL) 
        {
            for (auto itG = gep_type_begin(gepAddr), etG = gep_type_end(gepAddr); itG != etG; ++itG) 
            {
                Value* gepI = itG.getOperand();
                Instruction* temp = dyn_cast<Instruction>(gepI);
                
                if (dyn_cast<ConstantInt>(gepI)) 
                {
                    continue;
                }
                else if (temp == NULL)
                {
                    if (dyn_cast<Argument>(gepI)) {ac.push_back(gepI); continue;}
                    continue;
                }
                else 
                {
                    Value* basePhiV = isAddOrPHIConstant(gepI, false);
                    if (basePhiV == NULL) return NULL;
                    
                    Instruction* basePhi = dyn_cast<Instruction>(basePhiV);
                    if (basePhi == NULL) return NULL;
                    
                    bool iv_set = false;
                    if (L->isLoopInvariant(gepI) == false &&
                        std::find(IVs.begin(), IVs.end(), gepI) != IVs.end()) 
                    {
                        //errs() << "THERE: " << *gepAddr << "\n";    //TODO:remove
                        if (cnt_elided != NULL) return NULL;
                        cnt_elided = gepI;
                        iv_set = true;
                    }
                    //traverse the operands
                    else if (!isa <Argument>(gepI)) //isa<CastInst>(gepI)
                    {
                        Value* gepID = temp->getOperand(0);
                        
                        if (StructType *STy = dyn_cast<StructType>(*itG))
                        {
                            if (L->isLoopInvariant(gepI) == false) return NULL;
                            Value* v = isAddOrPHIConstant(gepI, true);
                            if (v != NULL) {ac.push_back(v);}
                            else return NULL;
                        }
                        
                        if (L->isLoopInvariant(gepI) == false &&
                            std::find(IVs.begin(), IVs.end(), gepID) != IVs.end()) 
                        {
                            //errs() << "FOUND IT 1: " << *gepAddr << "\n";
                            if (cnt_elided != NULL) return NULL;
                            cnt_elided = gepID;
                            iv_set = true;
                        }

                        else if (!isa <Argument>(gepID))  //any other operation involving IV and COnst or LI Inst
                        {
                            Instruction* tempID = dyn_cast<Instruction>(gepID);
                            bool is_safe = true, is_found = false;
                            if (tempID == NULL) 
                            {
                                // The parent instruction gepI could be safe to use
                                //   as an additional component, if it is loop invariant.
                            }
                            else if (tempID->getNumOperands() == 2) 
                            {
                                int op = 0;
                                for (unsigned i = 0; i < tempID->getNumOperands(); i++)
                                {    //assuming 2 operands in 3 add inst
                                    if (tempID->getOperand(i) == tempID->getOperand(1-i)) 
                                    {
                                        is_safe = false;
                                    }
                                    
                                    if (std::find(IVs.begin(), IVs.end(), tempID->getOperand(i)) != IVs.end()) 
                                    {
                                        is_found = true;
                                        op = i;
                                    }
                                }
                                
                                if (is_safe && is_found)     //check for other operand 
                                {
                                    const SCEVAddRecExpr *S = dyn_cast<SCEVAddRecExpr>(SE->getSCEV(&*tempID->getOperand(1-op)));
                                    if (S != NULL) 
                                    {
                                        if (!is_derived) 
                                        {
                                            if (cnt_elided != NULL) return NULL;
                                            cnt_elided = gepID;
                                            iv_set = true;
                                            //errs() << "FOUND IT 2 : " << *gepAddr << "\n";
                                        }
                                    }
                                }
                            }
                            else if (tempID->getNumOperands() == 1) 
                            {
                                if (std::find(IVs.begin(), IVs.end(), tempID->getOperand(0)) != IVs.end()) 
                                {
                                    if (cnt_elided != NULL) return NULL;
                                    cnt_elided = gepID;
                                    iv_set = true;
                                    //errs() << "FOUND IT 3: " << *gepAddr << "\n";
                                }
                            }
                            // case with >= 2
                        }
                    }
                    
                    // Is the memIV (i.e., cnt_elided) the same instruction as
                    //   the PHI that the GEP chains back to.
                    // In some loops, the GEP may use a PHI of the memIV and something else.
                    if (cnt_elided != NULL &&
                        basePhi != dyn_cast<Instruction>(cnt_elided)) return NULL;
                        
                    if (iv_set == false)
                    {
                        if (L->isLoopInvariant(gepI) == false) return NULL;
                        Value* v = isAddOrPHIConstant(gepI, true);
                        if (v != NULL) {ac.push_back(v);}
                        else return NULL;
                    }
                }
            }
        }
        return cnt_elided;
    }

    void LoopIV::iterateOnLoop(Loop *L)
    {
        for (auto subL = L->begin(), subLE = L->end(); subL != subLE; ++subL)
        {
            iterateOnLoop(*subL);
        }
        
        unsigned iterCount = SE->getSmallConstantTripCount(L);
        if (iterCount != 0 && iterCount < 4) return ;
        
        /*if (!SE->hasLoopInvariantBackedgeTakenCount(L)) 
        {
            errs() << "BACK -- " << **(L->block_begin()) << "\n";
            return;
        }*/
        
        llvm_loopiv_block tempLoopMemoryOps;
        
        tempLoopMemoryOps.canElide = false;
        tempLoopMemoryOps.headerBlock = L->getLoopPredecessor();
        L->getExitBlocks(tempLoopMemoryOps.exitBlocks);
        tempLoopMemoryOps.wasElide = false;

        collectPossibleIVs(L);
        collectDerivedIVs(L, PossibleIVs, &DerivedLinearIvs);
        collectDerivedIVs(L, NonLinearIvs, &DerivedNonlinearIvs);
        for(Loop::block_iterator bb = L->block_begin(); bb != L->block_end(); ++bb) 
        {
            BasicBlock* b = (*bb);
            for(BasicBlock::iterator I = b->begin(); I != b->end(); ++I) 
            {
                GetElementPtrInst *gepAddr = NULL;

                if (LoadInst *li = dyn_cast<LoadInst>(&*I))    
                {
                    gepAddr = dyn_cast<GetElementPtrInst>(li->getPointerOperand());
                    cnt_GetElementPtrInst++;
                    
                    if (gepAddr == NULL)
                    {
                        Value* v = ctThis->castWalk(li->getPointerOperand());
                        if (v != NULL) gepAddr = dyn_cast<GetElementPtrInst>(v);
                    }
                }
                else if (StoreInst *si = dyn_cast<StoreInst>(&*I)) 
                {
                    gepAddr = dyn_cast<GetElementPtrInst>(si->getPointerOperand());
                    cnt_GetElementPtrInst++;
                    
                    if (gepAddr == NULL)
                    {
                        Value* v = ctThis->castWalk(si->getPointerOperand());
                        if (v != NULL) gepAddr = dyn_cast<GetElementPtrInst>(v);
                    }
                }
                else 
                {
                    continue;
                }
                
                if (gepAddr == NULL) continue;
                
                Value* memIV = NULL;
                if (std::find(PossibleIVs.begin(), PossibleIVs.end(), gepAddr->getPointerOperand()) != PossibleIVs.end())
                {
                    memIV = inferredMemoryOps(gepAddr, false, L, tempLoopMemoryOps);
                    if (memIV == NULL) continue;
                    tempLoopMemoryOps.memOp = &*I;
                }
                
                // Is the base address of the memory operation
                //   invariant in this loop.
                if (gepAddr != NULL &&
                    SE->isLoopInvariant(SE->getSCEV(gepAddr->getPointerOperand()), L) == false) 
                {
                    continue;
                }
                
                
                if ((memIV = collectPossibleMemoryOps(gepAddr, PossibleIVs, false, L, tempLoopMemoryOps.addtComponents)) != NULL) 
                {
                    cnt_elided++;
                    tempLoopMemoryOps.memIV = dyn_cast<Instruction>(memIV);
                    
                    tempLoopMemoryOps.memOp = &*I;
                    tempLoopMemoryOps.canElide = true;
                    tempLoopMemoryOps.stepIV = IVToIncMap[tempLoopMemoryOps.memIV];
                    
                    PHINode* pn = dyn_cast<PHINode>(memIV);
                    if (pn == NULL ||
                        L->contains(tempLoopMemoryOps.memIV) == false)
                    {
                        memIV = NULL;
                        continue;
                    }
                    // errs() << "LE: " << *&*I << "\n";
                    // errs() << "LED: " << *memIV << "\t" << tempLoopMemoryOps.addtComponents.size() << "\n";
                    for (auto it = tempLoopMemoryOps.addtComponents.begin(), et = tempLoopMemoryOps.addtComponents.end(); it != et; ++it)
                    {
                        // errs() << "\t" << **it << "\n";
                    }
                    for (int i = 0; i < pn->getNumIncomingValues(); i++)
                    {
                        BasicBlock* pnBB = pn->getIncomingBlock(i);
                        Value* v = pn->getIncomingValue(i);
                        
                        if (L->contains(pnBB) == true)
                        {
                            Instruction* inst = dyn_cast<Instruction>(v);
                            if (inst != NULL)
                            {
                                tempLoopMemoryOps.stepBlock = inst->getParent();
                            }
                            else
                            {
                                tempLoopMemoryOps.stepBlock = pnBB;
                            }
                        }
                        else
                        {
                            tempLoopMemoryOps.startIV = v;
                        }
                    }
                    
                }
                else if ((memIV = collectPossibleMemoryOps(gepAddr, NonLinearIvs, false, L, tempLoopMemoryOps.addtComponents)) != NULL) 
                {
                    cnt_future_elided++;
                    tempLoopMemoryOps.memIV = dyn_cast<Instruction>(memIV);
                    tempLoopMemoryOps.memOp = &*I;
                    tempLoopMemoryOps.canElide = false;  //TODO: future work
                }
                else if ((memIV = collectPossibleMemoryOps(gepAddr, DerivedLinearIvs, true, L, tempLoopMemoryOps.addtComponents)) != NULL) 
                {
                    cnt_future_elided++;
                    tempLoopMemoryOps.memIV = dyn_cast<Instruction>(memIV);
                    tempLoopMemoryOps.memOp = &*I;
                    tempLoopMemoryOps.canElide = false;  //TODO: future work
                }
                else if ((memIV = collectPossibleMemoryOps(gepAddr, DerivedNonlinearIvs, true, L, tempLoopMemoryOps.addtComponents)) != NULL) 
                {
                    cnt_future_elided++;
                    tempLoopMemoryOps.memIV = dyn_cast<Instruction>(memIV);
                    tempLoopMemoryOps.memOp = &*I;
                    tempLoopMemoryOps.canElide = false;  //TODO: future work
                }

                // Only if one of the types was found, should this op be added to the list.
                if (memIV != NULL)
                {
                    llvm_loopiv_block* t = new llvm_loopiv_block;
                    *t = tempLoopMemoryOps;
                    LoopMemoryOps.push_back(t);
                }
            }
        }    
    }
    
    //bool LoopIV::runOnLoop(Loop *L, LPPassManager &LPM) {
    bool LoopIV::runOnFunction (Function &F) 
    {
        if (!F.isDeclaration()) 
        {
          LoopMemoryOps.clear();
          cnt_GetElementPtrInst = 0;
          cnt_elided = 0;
          
          LoopInfo &LI = *ctThis->getAnalysisLoopInfo(F);
          SE = ctThis->getAnalysisSCEV(F);
          for (LoopInfo::iterator i = LI.begin(), e = LI.end(); i!=e; ++i) 
          {
            Loop *L = *i;
            
            // Iterate on subloops of Loop L
            iterateOnLoop(L);
            
    #if 0
            outs() << "----------- Print summary for the loop --------------\n";
            
            outs() << "LRR: F[" << F.getName() <<
            "] Loop %" << (*(L->block_begin()))->getName() << " (" <<
            L->getNumBlocks() << " block(s))\n";

            outs() << "LINEAR INDUCTION VARIABLES:\n";
            for(auto iv = PossibleIVs.begin(); iv != PossibleIVs.end(); ++iv) {
                outs() << **iv << "\t***inc is : " << IVToIncMap[*iv] << "\n";
            }
            
            outs() << "NON-LINEAR INDUCTION VARIABLES:\n";
            for(auto iv = NonLinearIvs.begin(); iv != NonLinearIvs.end(); ++iv) {
                outs() << **iv << "\n";
            }
            
            outs() << "POSSIBLE DERIVED LINEAR INDUCTION VARIABLES:\n";
            for(auto iv = DerivedLinearIvs.begin(); iv != DerivedLinearIvs.end(); ++iv) {
                outs() << **iv << "\n";
            }

            outs() << "POSSIBLE DERIVED NON-LINEAR INDUCTION VARIABLES:\n";
            for(auto iv = DerivedNonlinearIvs.begin(); iv != DerivedNonlinearIvs.end(); ++iv) {
                outs() << **iv << "\n";
            }

            outs() << "cnt_GetElementPtrInst\t" << cnt_GetElementPtrInst << "\n";
            outs() << "cnt_elided\t" << cnt_elided << "\n";

            outs() << "-----------------------------------------------------\n\n\n";
    #endif        
            }
        }
        return false;
    }

    LlvmLoopIVBlockVector LoopIV:: getLoopMemoryOps() 
    {
        outs() << "cnt_GetElementPtrInst\t" << cnt_GetElementPtrInst << "\n";
        outs() << "cnt_elided\t" << cnt_elided << "\n";
        outs() << "future_elided\t" << cnt_future_elided << "\n";
        return LoopMemoryOps;
    }
}
