#include "bbDelta.hpp"
#include <vector>
#include <algorithm>

using namespace std;
using namespace contech;

BBDeltaBackend::BBDeltaBackend() 
{
    resetBackend();
}

void BBDeltaBackend::initBackend(TaskGraphInfo* tgi)
{
    this->tgi = tgi;
}

void BBDeltaBackend::updateBackend(Task* currentTask)
{
    instTime += currentTask->getEndTime() - currentTask->getStartTime();
    //if (currentTask->getContextId() == 0) return;
    switch(currentTask->getType())
    {
        case task_type_basic_blocks:
        {
            uint32_t bbid = 0, lastBBID = 0;
            uint64_t prevTime = 0, currTime = 0, delta = 0;
            bool lastBlockCall = false, hasUninstCall = false , hasUninstTime = false;
            string lastFuncName;
            auto bba = currentTask->getBasicBlockActions();
            for (auto f = bba.begin(), e = bba.end(); f != e; ++f)
            {
                currTime = 0;
                BasicBlockAction bb = *f;
                
                bbid = (uint)bb.basic_block_id;
                auto bbCall = bbInstCall.find(bbid);
                
                if (lastBlockCall || bbCall == bbInstCall.end())
                {
                    auto bbi = tgi->getBasicBlockInfo(bbid);
                    if (lastBlockCall)
                    {
                        // There was a function call in the last basic block
                        //   If the next block is in the same function, then
                        //   the function was not instrumented, or recursion?
                        if (lastFuncName == bbi.functionName)
                        {
                            bbInstCall[lastBBID] = false;
                        }
                        else
                        {
                            bbInstCall[lastBBID] = true;
                        }
                        lastBlockCall = false;
                    }
                    if (bbCall == bbInstCall.end())
                    {
                        if((bbi.flags & BBI_FLAG_CONTAIN_CALL) == BBI_FLAG_CONTAIN_CALL)
                        {
                            lastBlockCall = true;
                            lastFuncName = bbi.functionName;
                        }
                        else
                        {
                            // This block is followed by an instrumented block
                            bbInstCall[bbid] = true;
                        }
                    }
                }

                for (MemoryAction mem : f.getMemoryActions())
                {
                    if (currTime == 0) {currTime = mem.addr;}
                    if (Action(mem).isMemOp() == false) 
                    {
                        bbInstCall[bbid] = true;
                        hasUninstCall = false;
                    }
                }
                if (prevTime == 0)
                {
                    // Math from start Time?
                    prevTime = currTime;
                    lastBBID = bbid;
                    continue;
                }
                //if (currTime <
                assert(currTime >= prevTime);
                delta = currTime - prevTime;
                
                if (bbInstCall.find(lastBBID) != bbInstCall.end() && bbInstCall[lastBBID] == false)
                {
                    uninstTime += delta;
                    bbHistogram[lastBBID][0] += delta;
                }
                
                if (bbInstCall[bbid] == false && hasUninstCall == true)
                {
                    hasUninstTime = true;
                }
                
                
                //if (bbid == 190)
                //    printf("%u\n", (uint32_t)delta);
                prevTime = currTime;
                lastBBID = bbid;
            }
        }
        break;
        default:
        {
            
        }
        break;
    }
}

void BBDeltaBackend::resetBackend()
{
    bbHistogram.clear();
    instTime = 0;
    uninstTime = 0;
}

void BBDeltaBackend::completeBackend(FILE* f, contech::TaskGraphInfo* tgi)
{
    fprintf(f, "%llu, %llu, %lf\n", instTime, uninstTime, 1.0 - ((double)uninstTime)/
                                                           ((double)(instTime)));
    for (auto it = bbHistogram.begin(), et = bbHistogram.end(); it != et; ++it)
    {
        fprintf(f, "%u, ",it->first);
        auto bbi = tgi->getBasicBlockInfo(it->first);
        auto bbic = bbInstCall.find(it->first);
        
        fprintf(f, "%u, %u, %u, %u, %u, ", bbi.flags, (!bbic->second), bbi.numOfMemOps, bbi.numOfOps, bbi.critPathLen);
        
        for (auto itv = it->second.begin(), etv = it->second.end(); itv != etv; ++itv)
        {
            fprintf(f, "%u, %u, ", itv->first, itv->second);
        }
        fprintf(f, "%s:%d\n", bbi.fileName.c_str(), bbi.lineNumber);
    }
    fflush(f);
}
