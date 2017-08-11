//===-- Execution.cpp - 
//  Stripped Execution engine uses Contech task graph to drive the dynamic analysis

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/IntrinsicLowering.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Pass.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <deque>

#include "llvm/IR/Module.h"
#include <cxxabi.h>
#include <glob.h>

//VCA
#include "DynamicAnalysis.h"
#include "Params.h"

#include <boost/dynamic_bitset.hpp>

using namespace std;
using namespace llvm;
using namespace contech;
using namespace boost;

static DynamicAnalysis* Analyzer;

#ifdef READ_BOTTLENECK
static DynamicAnalysis* RelaxAnalyzer;
#endif

STATISTIC(NumDynamicInsts, "Number of dynamic instructions executed");

#define __ctStrCmp(x, y) strncmp(x, y, sizeof(y) - 1)

class EnginePass : public ModulePass
{
    map <unsigned int, BasicBlock*> basicBlockMap;
    map <TaskId, float> taskCFRmap;
    map <TaskId, float> taskCFWmap;
    
    // Should have size n (# res to relax). The float is the corresponding bnk. Ordered map.
    deque<pair<unsigned, float>> relaxFU;

    bool populateCommFactors(const char* TaskGraphFileName);
    void configureRelaxation(DynamicAnalysis* RelaxAnalyzer, TaskId tid, DynamicAnalysis& BaseAnalyzer, bool reset);
    void doConfiguration(DynamicAnalysis* RelaxAnalyzer, DynamicAnalysis& BaseAnalyzer, bool reset, unsigned n, unsigned degree);
    
public:
    static char ID; // Pass identification, replacement for typeid
    EnginePass() : ModulePass(ID) {
        }
    virtual bool doInitialization(Module &M);
    virtual bool runOnModule(Module &M);
    bool internalSplitOnCall(BasicBlock &B, CallInst** tci, int* st);
};

char EnginePass::ID = 0;
static RegisterPass<EnginePass> X("EnginePass", "Engine Pass", false, false);

bool EnginePass::populateCommFactors(const char* TaskGraphFileName)
{
    string tgName = TaskGraphFileName;
    string name = tgName.replace(tgName.find("_16_simmedium"), string::npos, "*.json");
    name = name.replace(name.find_first_of("/"), name.find_last_of("/")+1, "");
    // TODO: change hardcoded path to parameter
    string cfPath = "/net/tinker/ssrikant/bottle/commFactor/task/" + name;

    glob_t globbuf;
    glob(cfPath.c_str(), GLOB_TILDE, NULL, &globbuf);
    assert (globbuf.gl_pathc == 1);

    string jsonName = globbuf.gl_pathv[0];

    globfree(&globbuf);
    
    ifstream ifs(jsonName);
    assert(ifs.is_open());
    string line;
    while (getline(ifs, line))
    {
        size_t pos = line.find("a");
        if (pos != string::npos)
        {
            pos += 4;  
            size_t pos2 = line.find_first_of(",");
            pos2 -= 2;
            string ctid = "";
            for (auto i = pos; i <= pos2; ++i)
            {
                ctid += line[i];
            }
            
            pos = line.find("b");
            pos += 4;
            pos2 = line.find_first_of(",", pos);
            pos2 -= 2;
            string sqid = "";
            for (auto i = pos; i <= pos2; ++i)
            {
                sqid += line[i];
            } 

            SeqId seqi(stoi(sqid));
            ContextId coni(stoi(ctid));
            TaskId tid(coni, seqi);

            pos = line.find("c");
            pos += 4;
            pos2 = line.find_first_of(",", pos);
            pos2 -= 2;
            string c = "";
            for (auto i = pos; i <= pos2; ++i)
            {
                c += line[i];
            } 

            pos = line.find("d");
            pos += 4;
            pos2 = line.find_first_of(",", pos);
            pos2 -= 2;
            string d = "";
            for (auto i = pos; i <= pos2; ++i)
            {
                d += line[i];
            } 
        
            pos = line.find("g");
            pos += 4;
            pos2 = line.find_first_of(",", pos);
            pos2 -= 2;
            string trr = "";
            for (auto i = pos; i <= pos2; ++i)
            {
                trr += line[i];
            } 

            pos = line.find("h");
            pos += 4;
            pos2 = line.find_first_of(",", pos);
            pos2 -= 2;
            string trw = "";
            for (auto i = pos; i <= pos2; ++i)
            {
                trw += line[i];
            } 

            if (stoi(c) == 0)
            {
                taskCFRmap[tid] = 0.0;
            }
            else 
            {
                taskCFRmap[tid] = stof(trr) * 1.0 / stoi(c); 
            }

            if (stoi(d) == 0)
            {
                taskCFWmap[tid] = 0.0;
            }
            else
            {
                taskCFWmap[tid] = stof(trw) * 1.0 / stoi(d);
            }

            //assert(stoi(trr) <= stoi(c));
            //assert(stoi(trw) <= stoi(d));
            if (stoi(trr) > stoi(c) || stoi(trw) > stoi(d)) errs() << "Warning: Check " << tid.toString() << " comm factors\n";
        }
    }
    ifs.close(); 
/*
    for (auto II = taskCFRmap.begin(), IE = taskCFRmap.end(); II != IE; ++II)
    {
        errs() << get<0>(*II).toString() << "\t" << get<1>(*II) << "\n" ;
    }

    for (auto II = taskCFWmap.begin(), IE = taskCFWmap.end(); II != IE; ++II)
    {
        errs() << get<0>(*II).toString() << "\t" << get<1>(*II) << "\n" ;
    }
*/
}

bool EnginePass::doInitialization(Module &M)
{

    Analyzer = new DynamicAnalysis(PerTaskCommFactor,
                                   CommFactorRead,
                                   CommFactorWrite,
                                   Microarchitecture, 
                                   MemoryWordSize, 
                                   CacheLineSize, 
                                   L1CacheSize, 
                                   L2CacheSize, 
                                   LLCCacheSize, 
                                   ExecutionUnitsLatency, 
                                   ExecutionUnitsThroughput, 
                                   ExecutionUnitsParallelIssue, 
                                   MemAccessGranularity, 
                                   IFB, 
                                   ReservationStation, 
                                   ReorderBuffer, 
                                   LoadBuffer, 
                                   StoreBuffer, 
                                   LineFillBuffer, 
                                   ReportOnlyPerformance);

    if (PerTaskCommFactor)
    {
        populateCommFactors(TaskGraphFileName.c_str());
    }                               

    #ifdef READ_BOTTLENECK
    RelaxAnalyzer = new DynamicAnalysis(PerTaskCommFactor,
                                   CommFactorRead,
                                   CommFactorWrite,
                                   Microarchitecture, 
                                   MemoryWordSize, 
                                   CacheLineSize, 
                                   L1CacheSize, 
                                   L2CacheSize, 
                                   LLCCacheSize, 
                                   ExecutionUnitsLatency, 
                                   ExecutionUnitsThroughput, 
                                   ExecutionUnitsParallelIssue, 
                                   MemAccessGranularity, 
                                   IFB, 
                                   ReservationStation, 
                                   ReorderBuffer, 
                                   LoadBuffer, 
                                   StoreBuffer, 
                                   LineFillBuffer, 
                                   ReportOnlyPerformance);
    #endif

    return true;
}

bool EnginePass::internalSplitOnCall(BasicBlock &B, CallInst** tci, int* st)
{
    *tci = NULL;
    bool isFirstAcquire = true;
    bool isFirstRelease = true;
    bool tmp = false;
    BasicBlock::iterator Iold = B.begin();
    BasicBlock::iterator IoldAcquire = B.begin();
    CallInst *ciBeforeMultipleRelease = NULL;
    
    for (BasicBlock::iterator I = B.begin(), E = B.end(); I != E; ++I)
    {
        if (CallInst *ci = dyn_cast<CallInst>(&*I)) 
        {
            *tci = ci;
            if (ci->isTerminator()) {*st = 1; return false;}
            if (ci->doesNotReturn()) {*st = 2; return false;}
            Function* f = ci->getCalledFunction();
            
            //
            // If F == NULL, then f is indirect
            //   O.w. this function may just be an annotation and can be ignored
            //
            if (f == NULL)
            {
                Value* v = ci->getCalledValue();
                f = dyn_cast<Function>(v->stripPointerCasts());
            }
            if (f != NULL)
            {
                const char* fn = f->getName().data();
                if (0 == __ctStrCmp(fn, "llvm.dbg") ||
                    0 == __ctStrCmp(fn, "llvm.lifetime") ||
                    0 == __ctStrCmp(fn, "__ct")) 
                {
                    if (0 == __ctStrCmp(fn, "__ctStoreBasicBlock")) 
                    {
                        ciBeforeMultipleRelease = ci;
                        if (tmp) 
                        {
#ifdef PRINT_ALL
                            errs() << "Splitting merged BB at:\n\t" << *Iold << "\n\t"<< *I << "\n";
#endif
                            tmp = false;
                            B.splitBasicBlock(Iold, "");
                            return true;
                        }
                    }
                    *st = 3;
                    continue;
                }
            
            }
            
            I++;
            
            // At this point, the call instruction returns and was not the last in the block
            //   If the next instruction is a terminating, unconditional branch then splitting
            //   is redundant.  (as splits create a terminating, unconditional branch)
            if (I->isTerminator())
            {
                if (BranchInst *bi = dyn_cast<BranchInst>(&*I))
                {
                    if (bi->isUnconditional()) {*st = 4; return false;}
                }
                else if (/* ReturnInst *ri = */ dyn_cast<ReturnInst>(&*I))
                {
                    *st = 5;
                    return false;
                }
            }
            B.splitBasicBlock(I, "");
            return true;
        }
        if (FenceInst *fi = dyn_cast<FenceInst>(&*I)) 
        {
            if (fi->getOrdering() == AtomicOrdering::Acquire) 
            {
                if (!isFirstAcquire) 
                {
                    //errs() << "Multiple Fence Acquire\n";
                    tmp = true;
                    Iold = I;
                } 
                else 
                {
                    //errs() << "First Fence Acquire\n";
                    IoldAcquire = I;
                    isFirstAcquire = false;
                }
            } 
            else if (fi->getOrdering() == AtomicOrdering::Release) 
            {
                if (!isFirstRelease) 
                {
                    //errs() << "Multiple Fence Release\n";
                    //assert(IoldAcquire);
                    Iold = IoldAcquire;
                    if (ciBeforeMultipleRelease != NULL) 
                    {
                        *tci = ciBeforeMultipleRelease;
                        //errs() << "Splitting merged BB with optimized acquire at:\n\t" << *Iold << "\n\t" << *ciBeforeMultipleRelease << "\n";
                        B.splitBasicBlock(Iold, "");
                        return true;
                    }
                } 
                else 
                {
                    //errs() << "First Fence Release\n";
                    //IoldAcquire = NULL;
                    isFirstRelease = false;
                }
            }
        }
    }

    return false;
}

bool EnginePass::runOnModule(Module &M)
{
    for (Module::iterator F = M.begin(), FE = M.end(); F != FE; ++F) 
    {
        // Split basic blocks, as the toolchain may combine basic blocks
        //   that were split for function calls
        // "Normalize" every basic block to have only one function call in it
        for (Function::iterator B = F->begin(), BE = F->end(); B != BE; ) 
        {
            BasicBlock &pB = *B;
            CallInst *ci;
            int status = 0;

            if (internalSplitOnCall(pB, &ci, &status) == false)
            {
                B++;
            }
            else 
            {
                
            }
        }
    
        for (Function::iterator itB = F->begin(), etB = F->end(); itB != etB; ++itB) 
        {
            BasicBlock &B = *itB;
            
            for (BasicBlock::iterator I = B.begin(), E = B.end(); I != E; ++I)
            {
                if (CallInst *ci = dyn_cast<CallInst>(&*I)) 
                {
                    Function *f = ci->getCalledFunction();
                    
                    // call is indirect
                    // TODO: add dynamic check on function called
                    if (f == NULL) { continue; }

                    int status;
                    const char* fmn = f->getName().data();
                    char* fdn = abi::__cxa_demangle(fmn, 0, 0, &status);
                    const char* fn = fdn;
                    if (status != 0) 
                    {
                        fn = fmn;
                    }
                    
                    // Find the store basic block calls to find the ID for this block
                    if (strcmp(fn, "__ctStoreBasicBlock") == 0)
                    {
                        Value* v = ci->getArgOperand(0);
                        if (Constant* cint = dyn_cast<Constant>(v))
                        {
                            unsigned int bbid = cint->getUniqueInteger().getLimitedValue((0x1 << 24));
                            basicBlockMap[bbid] = dyn_cast<BasicBlock>(&B);
#ifdef PRINT_ALL
                            errs() << "Found - " << bbid << "\n";
#endif
                            break;
                        }
                    }
                }
            }
        }
    }
    
    // All static basic blocks have been identified,
    //   now load the task graph
    
    TaskGraph* tg = TaskGraph::initFromFile(TaskGraphFileName.c_str());

    if (tg == NULL)
    {
        errs() << "Failed to open task graph - " << TaskGraphFileName << "\n";
        return false;
    }
    
    unsigned int bbcount = 0;
    bool inROI = true;
    // If ContextNumber == the Context of ROI, then jump to ROI
    //   else either
    //     1) ROI is before the threads are created
    //         Then, the thread starts within the ROI
    //         But is ROI before it exits?
    //     2) After the threads are created
    //         Then either the context in question exits before the ROI (see DynoGraph)
    //         Or the ROI is with a barrier, et cetera to provide the ordering with all threads
    //  All this to say, one could use tg->getTaskById(ContextId(ContextNumber, 0)) and increment
    //    the sequence id instead.

    TaskId startTaskId;
    if (ContextNumber == (uint32_t) tg->getROIStart().getContextId())
    {
        startTaskId = tg->getROIStart();
    }
    else
    {
        SeqId seqi(0);
        ContextId coni(ContextNumber);
        startTaskId = TaskId(coni, seqi);
    }

    while (Task* currentTask = tg->getTaskById(startTaskId))
    {
        TaskId taskId = currentTask->getTaskId();
        
        startTaskId = startTaskId.getNext();
        
        if (taskId == tg->getROIStart()) 
        {
            inROI = true;
        } 
        else if (taskId == tg->getROIEnd()) 
        {
            inROI = false;
        }
       
        if (!inROI) { delete currentTask; continue; }

        ContextId ctId(ContextNumber);
        if (currentTask->getContextId() != ctId) { delete currentTask; continue;}

        switch(currentTask->getType())
        {
            case task_type_basic_blocks:
            {
                if (Analyzer->PerTaskCommFactor)
                {
                    Analyzer->TaskCFR = taskCFRmap[taskId];
                    Analyzer->TaskCFW = taskCFWmap[taskId]; 
                }
                auto bba = currentTask->getBasicBlockActions();

                for (auto f = bba.begin(), e = bba.end(); f != e; f++)
                {
                    BasicBlockAction bb = *f;
                    
                    auto bbm = basicBlockMap.find((uint)bb.basic_block_id);
                    if (bbm == basicBlockMap.end())
                    {
                        errs() << "Failed to find - " << (uint)bb.basic_block_id << " - ";
                        auto bbi = &tg->getTaskGraphInfo()->getBasicBlockInfo((uint)bb.basic_block_id);
                        if (bbi != NULL)
                        {
                            errs() << bbi->fileName << ":" << bbi->lineNumber << " " << bbi->functionName << "\n";
                        }
                        return false;
                    }
                    else
                    {
                        // Send instructions from block bbm->second to DynamicAnalysis
                        BasicBlock* currentBB = bbm->second;
                        auto iMemOps = f.getMemoryActions().begin();
                        for (auto it = currentBB->begin(), et = currentBB->end(); it != et; ++it)
                        {
                            uint64_t addr = 0;
                            
                            // test for Contech instrumentation instructions
                            //   call store ...
                            //   fence ...
                            
                            if (LoadInst *li = dyn_cast<LoadInst>(&*it))
                            {
                                MemoryAction ma = *iMemOps;
                                addr = ma.addr;
                                ++iMemOps;
                            }
                            else if (StoreInst *si = dyn_cast<StoreInst>(&*it))
                            {
                                MemoryAction ma = *iMemOps;
                                addr = ma.addr;
                                ++iMemOps;
                            }
                            else if (FenceInst *feni = dyn_cast<FenceInst>(&*it))
                            {
                                continue;
                            }
                            else if (CallInst *ci = dyn_cast<CallInst>(&*it)) {
                                Function *f = ci->getCalledFunction();
                                
                                // call is indirect
                                // TODO: add dynamic check on function called
                                if (f == NULL) {  }
                                else
                                {

                                    int status;
                                    const char* fmn = f->getName().data();
                                    char* fdn = abi::__cxa_demangle(fmn, 0, 0, &status);
                                    const char* fn = fdn;
                                    if (status != 0) 
                                    {
                                        fn = fmn;
                                    }
                                    
                                    if (__ctStrCmp(fn, "__ct") == 0 ||
                                        __ctStrCmp(fn, "llvm.dbg") == 0)
                                    {
                                        continue;
                                    }
                                    else
                                    {
                                        //errs() << fn << "\n";
                                    }
                                }
                            }
                            //errs() << *it << "\n";
                            uint64_t cycB = Analyzer->analyzeInstruction(*it, addr);
                        }
                        bbcount ++;
                    }
                }
                
                // CacheLineIssueCycleMap helps carry reuse distance across tasks 
                bool CompareGroupSpans = false;
                Analyzer->finishAnalysis(taskId, !CompareGroupSpans, true);
                if (CompareGroupSpans)
                {
                    for (unsigned int i = 0; i < Analyzer->CGSFCache.size(); i++)
                    {
                        errs() << Analyzer->CGSFCache[i].size() << "(" << Analyzer->CGSFCache[i].count() << "+" << Analyzer->CGSFCache[i].find_first() << ")\t";
                        #ifdef DEBUG_PARALLEL_ANALYSIS  
                        errs() << AnalyzerSm->CGSFCache[i].size() << "(" << AnalyzerSm->CGSFCache[i].count() << "+" << AnalyzerSm->CGSFCache[i].find_first() << ")\n";
                        #endif
                    }
                    
                    vector<unsigned> compResources;
                    for (unsigned i = 0; i < SANDY_BRIDGE_COMP_NODES; i++)
                        compResources.push_back(i);
                    
                    dynamic_bitset<> BitM;
                    dynamic_bitset<> BitMS;
                    
                    BitM.resize(Analyzer->CGSFCache[0].size());
                    BitMS.resize(Analyzer->CGSFCache[0].size());
                    for (unsigned int j = 0; j < compResources.size(); j++)
                    {
                        BitM |= Analyzer->CGSFCache[compResources[j]];
                        #ifdef DEBUG_PARALLEL_ANALYSIS  
                        BitMS |= AnalyzerSm->CGSFCache[compResources[j]];
                        
                        Analyzer->CGSFCache[compResources[j]] ^= AnalyzerSm->CGSFCache[compResources[j]];
                        errs() << compResources[j] << "\t" << Analyzer->CGSFCache[compResources[j]].find_first() << "\n";
                        #endif
                    }
                    
                    BitM ^= BitMS;
                    errs() << BitM.size() << "(" << BitM.count() << "+" << BitM.find_first() << ")\n";
                    
                    uint64_t c = BitM.find_first();
                    while (c != BitM.npos)
                    {
                        errs() << c << "\t";
                        c = BitM.find_next(c);
                    }
                    errs() << "\n";
                }
            }
            break;
            default:
            break;
        }

        #ifdef READ_BOTTLENECK

        switch(currentTask->getType())
        {
            case task_type_basic_blocks:
            {
                configureRelaxation(RelaxAnalyzer, taskId, *Analyzer, false); 

                if (RelaxAnalyzer->PerTaskCommFactor)
                {
                    RelaxAnalyzer->TaskCFR = taskCFRmap[taskId];
                    RelaxAnalyzer->TaskCFW = taskCFWmap[taskId]; 
                }
                auto bba = currentTask->getBasicBlockActions();

                for (auto f = bba.begin(), e = bba.end(); f != e; f++)
                {
                    BasicBlockAction bb = *f;
                    
                    auto bbm = basicBlockMap.find((uint)bb.basic_block_id);
                    if (bbm == basicBlockMap.end())
                    {
                        errs() << "Failed to find - " << (uint)bb.basic_block_id << " - ";
                        auto bbi = tg->getTaskGraphInfo()->getBasicBlockInfo((uint)bb.basic_block_id);
                        errs() << bbi.fileName << ":" << bbi.lineNumber << " " << bbi.functionName << "\n";
                        return false;
                    }
                    else
                    {
                        // Send instructions from block bbm->second to DynamicAnalysis
                        BasicBlock* currentBB = bbm->second;
                        auto iMemOps = f.getMemoryActions().begin();
                        for (auto it = currentBB->begin(), et = currentBB->end(); it != et; ++it)
                        {
                            uint64_t addr = 0;
                            
                            // test for Contech instrumentation instructions
                            //   call store ...
                            //   fence ...
                            
                            if (LoadInst *li = dyn_cast<LoadInst>(&*it))
                            {
                                MemoryAction ma = *iMemOps;
                                addr = ma.addr;
                                ++iMemOps;
                            }
                            else if (StoreInst *si = dyn_cast<StoreInst>(&*it))
                            {
                                MemoryAction ma = *iMemOps;
                                addr = ma.addr;
                                ++iMemOps;
                            }
                            else if (FenceInst *feni = dyn_cast<FenceInst>(&*it))
                            {
                                continue;
                            }
                            else if (CallInst *ci = dyn_cast<CallInst>(&*it)) {
                                Function *f = ci->getCalledFunction();
                                
                                // call is indirect
                                // TODO: add dynamic check on function called
                                if (f == NULL) {  }
                                else
                                {

                                    int status;
                                    const char* fmn = f->getName().data();
                                    char* fdn = abi::__cxa_demangle(fmn, 0, 0, &status);
                                    const char* fn = fdn;
                                    if (status != 0) 
                                    {
                                        fn = fmn;
                                    }
                                    
                                    if (__ctStrCmp(fn, "__ct") == 0 ||
                                        __ctStrCmp(fn, "llvm.dbg") == 0)
                                    {
                                        continue;
                                    }
                                    else
                                    {
                                        //errs() << fn << "\n";
                                    }
                                }
                            }
                            //errs() << *it << "\n";
                            uint64_t cycB = RelaxAnalyzer->analyzeInstruction(*it, addr);
                        }
                        bbcount ++;
                    }
                }
                
                RelaxAnalyzer->finishAnalysis(taskId, true, false);

                configureRelaxation(RelaxAnalyzer, taskId, *Analyzer, true); 

            }
            break;
            default:
            break;

        }
        
        #endif

        delete currentTask;
    }
    errs() << "Done analyzing tasks for context " << ContextNumber << "\n";
    //Analyzer->dumpHistogram();
    //Analyzer->finishAnalysis();
    
    return false;
}

// Note: Infinite throughput not supported here
void EnginePass::configureRelaxation(DynamicAnalysis* RelaxAnalyzer, TaskId tid, DynamicAnalysis& BaseAnalyzer, bool reset)
{
    assert (tid == BaseAnalyzer.residingTask);

    if (reset)
    {
        doConfiguration(RelaxAnalyzer, BaseAnalyzer, true, 0, 0);
        relaxFU.clear();
        return;
    }
    
    const auto& bnkMat = BaseAnalyzer.BnkMat;

    // For now, all buffers are relaxed by degree, irrespective of n

    // Scheme: 2x Relax n tightest bottlenecks, but ignore latency column
    //      If we get consecuitive tights for the same FU, ignore it and choose the next 'distinct' tight.
    //          This is because 2x Single to 200x Single did not seem to have much impact.
    //          Separately, if one wishes to leverage this info about non-distinct consecuitive tights,
    //              an alternate scheme could consider increasing the degree of relaxation 
    unsigned n = 10;
    unsigned degree = 2;

    for (unsigned i = 0; i < RelaxAnalyzer->nExecutionUnits; ++i)
    {
        // Ignore *SHUF
        if (i == 3 || i == 7) { continue; }

        const auto& bnkVec = bnkMat[i];
        float mini = 1000.0;
        for (unsigned j = 0; j < RelaxAnalyzer->nBuffers + 2; ++j)
        {
            // Ignore LAT column
            if (j == 1) { continue; }

            const auto bnk = bnkVec[j];
            if (bnk == -1) { continue; }

            mini = min(mini, bnk);
        }

        for (unsigned nn = 0; nn < n; ++nn)
        {
            bool done = false;

            if (relaxFU.size() > nn)
            {
                for (auto I = relaxFU.begin(), Ie = relaxFU.end(); I != Ie; ++I)
                {
                    auto m = *I;
                    if (m.second > mini)
                    {
                        relaxFU.insert(I, make_pair(i, mini));
                        if (relaxFU.size() > n)
                        {
                            relaxFU.pop_back();
                        }
                        done = true;
                        break;
                    }
                }
            }
            else
            {
                relaxFU.push_back(make_pair(i, mini));
                break;
            }

            if (done) { break; }
        }

        //errs() << "\n";
    }

    doConfiguration(RelaxAnalyzer, BaseAnalyzer, false, n, degree);
}

void EnginePass::doConfiguration(DynamicAnalysis* RelaxAnalyzer, DynamicAnalysis& BaseAnalyzer, bool reset, unsigned n, unsigned degree)
{
    if (reset)
    {
        for (unsigned i = 0; i < RelaxAnalyzer->nExecutionUnits; ++i)
        {
            RelaxAnalyzer->ExecutionUnitsThroughput[i] = BaseAnalyzer.ExecutionUnitsThroughput[i];
            //RelaxAnalyzer->ExecutionUnitsParallelIssue[i] = BaseAnalyzer.ExecutionUnitsThroughput[i];
            RelaxAnalyzer->AccessWidths[i] = BaseAnalyzer.AccessWidths[i];
            RelaxAnalyzer->IssueCycleGranularities[i] = BaseAnalyzer.IssueCycleGranularities[i];
        }

        RelaxAnalyzer->ReservationStationSize = BaseAnalyzer.ReservationStationSize;
        RelaxAnalyzer->ReorderBufferSize = BaseAnalyzer.ReorderBufferSize;
        RelaxAnalyzer->LoadBufferSize = BaseAnalyzer.LoadBufferSize;
        RelaxAnalyzer->StoreBufferSize = BaseAnalyzer.StoreBufferSize;
        RelaxAnalyzer->LineFillBufferSize = BaseAnalyzer.LineFillBufferSize;

        return;
    }

    // EUPI is just weird
    bool manualL1 = false;
    for (unsigned i = 0; i < RelaxAnalyzer->nExecutionUnits; ++i)
    {
        // Ignore *SHUF
        if (i == 3 || i == 7) { continue; }

        for (auto m : relaxFU)
        {
            if (m.first == i)
            {
                errs() << RelaxAnalyzer->ResourcesNames[i] << "\t";
                RelaxAnalyzer->ExecutionUnitsThroughput[i] *= degree;

                // Warning: This is heavily dependent on baseline count and baselineThroughput
                // Current assumption: 
                //  baselineCount = [3, 1, 1, 1,
                //       1, 1, 1, 1,
                //       2, 1, 1, 1, 1]
                uint16_t count = 1;
                if (RelaxAnalyzer->ExecutionUnitsBaselineThroughput[i] >= 1)
                {
                    if (i == L1_LOAD_NODE)
                    { 
                        count = 2 * RelaxAnalyzer->ExecutionUnitsThroughput[i] / RelaxAnalyzer->ExecutionUnitsBaselineThroughput[i];
                    }
                    else if (i > L1_LOAD_NODE)
                    {
                        count = RelaxAnalyzer->ExecutionUnitsThroughput[i] / RelaxAnalyzer->ExecutionUnitsBaselineThroughput[i];
                    }
                    else 
                    {
                        count = RelaxAnalyzer->ExecutionUnitsThroughput[i];
                    }
                }
                else
                {
                    count  = RelaxAnalyzer->ExecutionUnitsThroughput[i] / RelaxAnalyzer->ExecutionUnitsBaselineThroughput[i];
                }
                //std::cerr << std::setprecision(3) << RelaxAnalyzer->ExecutionUnitsThroughput[i] * 1.0 / RelaxAnalyzer->ExecutionUnitsBaselineThroughput[i] << "\t";
                errs() << count << "\t";    

                // Baseline had EUPI of 2 for L1_LD
                if (i == 8)
                {
                    RelaxAnalyzer->ExecutionUnitsThroughput[i] *= 2;
                    manualL1 = true;
                } 
                break;
            }
        }
    }

    if (relaxFU.size() != 0)
    {
        errs() << "\n";
        RelaxAnalyzer->ReservationStationSize *= degree;
        RelaxAnalyzer->ReorderBufferSize *= degree;
        RelaxAnalyzer->LoadBufferSize *= degree;
        RelaxAnalyzer->StoreBufferSize *= degree;
        RelaxAnalyzer->LineFillBufferSize *= degree;

        for (unsigned i = 0; i < RelaxAnalyzer->nExecutionUnits; ++i)
        { 
            // If we start with baseline, EUPI wouldn't've been INF
            RelaxAnalyzer->ExecutionUnitsParallelIssue[i] = -1;
            //  
            if (i == 8 && !manualL1)
            {
                RelaxAnalyzer->ExecutionUnitsParallelIssue[i] = 2;
            }
        }
    }

    for (unsigned i = 0; i < RelaxAnalyzer->nExecutionUnits; ++i)
    {
    
        unsigned IssueCycleGranularity = 0;
        unsigned AccessWidth = 0;
    
        if (i < RelaxAnalyzer->nCompExecutionUnits)
        {
            AccessWidth = 1;
            // Computational units throughput must also be rounded
            if (RelaxAnalyzer->ExecutionUnitsThroughput[i] >= 1)
            {
                RelaxAnalyzer->ExecutionUnitsThroughput[i] = RelaxAnalyzer->roundNextMultiple(RelaxAnalyzer->ExecutionUnitsThroughput[i],
                                                                                 1);
            }
        }
        else
        {
            AccessWidth = RelaxAnalyzer->roundNextMultiple(MemoryWordSize, RelaxAnalyzer->AccessGranularities[i]);
            // Round throughput of memory resources to the next multiple of AccessWidth
            // (before it was MemoryWordSize)
            if (RelaxAnalyzer->ExecutionUnitsThroughput[i] < AccessWidth)
            {
                // if (this->ExecutionUnitsThroughput[i] < this->MemoryWordSize){
                if (RelaxAnalyzer->ExecutionUnitsThroughput[i] < 1)
                {
                    float Inverse =ceil(1/RelaxAnalyzer->ExecutionUnitsThroughput[i]);
                    float Rounded =RelaxAnalyzer->roundNextPowerOfTwo(Inverse);
                        
                    if (Inverse == Rounded) 
                    {
                        RelaxAnalyzer->ExecutionUnitsThroughput[i] = float(1)/float(Rounded);
                    } 
                    else
                    {
                        RelaxAnalyzer->ExecutionUnitsThroughput[i] = float(1)/float((Rounded/float(2)));
                    }
                }
                else
                {
                    RelaxAnalyzer->ExecutionUnitsThroughput[i] = RelaxAnalyzer->roundNextPowerOfTwo(ceil(RelaxAnalyzer->ExecutionUnitsThroughput[i]));
                }
            }
            else
            {
                // Round to the next multiple of AccessGranularities...
                //                    this->ExecutionUnitsThroughput[i] = RelaxAnalyzer->roundNextMultiple(this->ExecutionUnitsThroughput[i],this->MemoryWordSize);
                RelaxAnalyzer->ExecutionUnitsThroughput[i] = RelaxAnalyzer->roundNextMultiple(RelaxAnalyzer->ExecutionUnitsThroughput[i],
                                                                                              RelaxAnalyzer->AccessGranularities[i]);
            }
        }
    
        if (RelaxAnalyzer->ExecutionUnitsThroughput[i] > 0) 
        {
            IssueCycleGranularity = ceil(AccessWidth/RelaxAnalyzer->ExecutionUnitsThroughput[i]);
        }
        else
        {
            IssueCycleGranularity = 1;
        }
    
        RelaxAnalyzer->AccessWidths[i] = AccessWidth;
        RelaxAnalyzer->IssueCycleGranularities[i] = IssueCycleGranularity;
    }
}
