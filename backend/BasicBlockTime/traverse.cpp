// Traverses task graph and prints source info for each node
// Input: Taskgraph
// Warning: Do not use with DEBUG for large graphs. In any case, cout may or may not be manageable.

#include "../../common/taskLib/TaskGraph.hpp"
#include "../../common/taskLib/TaskGraphInfo.hpp"
#include <iostream>
#include <fstream>
#include <cmath>

//#define DEBUG 
#ifdef DEBUG 
    #define DBG(s) cerr << s << "\n"
#else
    #define DBG(s)
#endif

//#define DEBUG2 
#ifdef DEBUG2 
    #define DBG2(s) cerr << s << "\n"
#else
    #define DBG2(s)
#endif

// If binSize is 10, then each bin stores mean +/- 10%
#define binSizePercent 10

using namespace std;
using namespace contech;

int main(int argc, char const *argv[]) { 

    // input check 
    if(argc != 2){
        cout << "Usage: " << argv[0] << " taskGraphInputFile" << endl;
        exit(1);
    }

    // open
    ofstream out("dump.txt", ofstream::out);
    ofstream outBins("dumpBins.txt", ofstream::out);
    
    // init
    TaskGraph *tg = TaskGraph::initFromFile(argv[1]);
    assert(tg);

    TaskGraphInfo *tgi = tg->getTaskGraphInfo();
    assert(tgi);

    // Maintain old timestamps for each Context
    map<ContextId, uint64_t> oldTime;
    
    // Maintain previous BBID because delta actually measures its delta
    map<ContextId, uint> oldBbid;

    // Maintain overall timestamps for each Context
    map<ContextId, uint64_t> tick;
    map<ContextId, uint64_t> tock;
    map<ContextId, uint64_t> sumDelta;

    // Seems to be skew in absolute times for non-BB events
    // The offset doesn't add up, temp. fix:
    // Ignore delta calculation for BB successor of Sync, Join, Barrier and Create
    map<ContextId, bool> hasTime;

    typedef map<uint, ContextId> bb_contextPair; 
    // Mean values of bins for each pair
    map<bb_contextPair, vector<uint64_t>> bins;
    // #occurences in each bin for each pair 
    map<bb_contextPair, vector<uint>> binCount;

    // iterate and collect info
    while (Task *currTask = tg->readContechTask()) {
        auto taskID = currTask->getTaskId();
        auto contextID = currTask->getContextId();
        switch (currTask->getType()) {
            case task_type_basic_blocks: {
                auto bbas = currTask->getBasicBlockActions();
                uint64_t oldTime = 0;
                uint32_t oldBBID = 0;
                for (auto I = bbas.begin(), Iend = bbas.end(); I != Iend; ++I) {
                    BasicBlockAction bba = *I;
                    uint bbid = bba.basic_block_id;
                    auto memAct = I.getMemoryActions();
                    auto it = memAct.begin();
                    uint64_t timestamp = ((MemoryAction)(*it)).addr;
                    if (tick.count(contextID) == 0) {
                        tick[contextID] = timestamp;
                    } else if (timestamp > tock[contextID]) {
                        tock[contextID] = timestamp;
                    }
                    uint64_t delta = 0;
                    if (oldTime != 0)
                        delta = timestamp - oldTime;
                    else
                    {
                        oldTime = timestamp;
                        oldBBID = bbid;
                        continue;
                    }
                    /*if (oldTime.count(contextID) == 1) {
                        if (hasTime.count(contextID) == 1 && hasTime[contextID] == true) {
                            delta = 0;
                        } else {
                            delta = timestamp - oldTime[contextID];
                        }
                        assert(delta >= 0);
                        if (sumDelta.count(contextID) == 0) {
                            sumDelta[contextID] = delta;
                        } else {
                            sumDelta[contextID] += delta;
                        }
                    }*/
                    BasicBlockInfo newBbi = tgi->getBasicBlockInfo(bbid);
                    if (true || oldBbid.count(contextID) == 1) {
                        //BasicBlockInfo bbi = tgi->getBasicBlockInfo(oldBbid[contextID]);
                        if (delta != 0) {
                            bb_contextPair bc;
                            //bc[oldBbid[contextID]] = contextID;
                            bc[oldBBID] = contextID;
                            //auto b = bins[bc];
                            if (bins.count(bc) == 0) {
                                DBG2(oldBbid[contextID] << " " << contextID << " First instance of bc pair");
                                vector<uint64_t> v(1, delta);
                                bins[bc] = v;
                                vector<uint> vc(1, 1);
                                binCount[bc] = vc;
                                DBG2("Initialized bins and binCount:\t" << bins[bc][0] << "\t" << binCount[bc][0] << "\n");
                                assert(bins[bc].size() == binCount[bc].size());
                            } else {
                                auto means = bins[bc];
                                // TODO: Rearrange this so that search is binary search if #bins large
                                bool found = false;
                                for (uint64_t i = 0, iend = means.size(); i < iend; i++) {
                                    uint64_t mean = means[i];
                                    uint64_t error = binSizePercent*mean/100;
                                    if (delta <= mean + error &&
                                        delta >= mean - error) {
                                        assert(bins[bc].size() == binCount[bc].size());
                                        DBG2(oldBbid[contextID] << " " << contextID << " Found bin at index " << i);
                                        auto num = binCount[bc][i];
                                        uint64_t newMean = (num*mean + delta) / (1.0*(num+1));
                                        //means[i] = newMean;
                                        DBG2("\tnewMean: " << newMean);
                                        //bins[bc] = means;
                                        bins[bc][i] = newMean;
                                        binCount[bc][i] += 1;        
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found) {
                                    DBG2(oldBbid[contextID] << " " << contextID << " New bin as delta: " << delta << "\n");
                                    assert(bins[bc].size() == binCount[bc].size());
                                    //means.push_back(delta);
                                    //bins[bc] = means;
                                    bins[bc].push_back(delta);
                                    auto counts = binCount[bc];
                                    counts.push_back(1);
                                    binCount[bc] = counts;
                                }
                            }
                            /*out << delta << " " << oldBbid[contextID] << " " << contextID << 
                                    " " << bbi.numOfOps << " " << bbi.numOfMemOps <<
                                    " " << bbi.critPathLen << "\n";*/
                        } else {
                            cerr << oldBbid[contextID] << " " << 
                                    contextID << "  Potential skew; ignoring delta\n";
                        }
                        DBG(delta << "\ttimestamp: " << oldTime[contextID] << "\tBBID: " << 
                                oldBbid[contextID] << "\tTaskID: " << taskID << "\t# MemOps: " <<
                                bbi.numOfMemOps << "\t# nonMemOps: " << bbi.numOfOps - bbi.numOfMemOps <<
                                "\t# NumOps: " << bbi.numOfOps <<
                                "\tcritPathLen: " << bbi.critPathLen << 
                                "\t" << bbi.functionName << "\t" <<
                                bbi.fileName << ":" << bbi.lineNumber);
                    } else {
                        DBG("####First instance in Context " << contextID);
                        DBG("\ttimestamp: " << timestamp << "\t" <<
                                bba.toString() << "\tTaskID: " << taskID << "\t# MemOps: " <<
                                newBbi.numOfMemOps << "\t# nonMemOps: " << newBbi.numOfOps - newBbi.numOfMemOps <<
                                "\t# NumOps: " << newBbi.numOfOps <<
                                "\tcritPathLen: " << newBbi.critPathLen << 
                                "\t" << newBbi.functionName << "\t" <<
                                newBbi.fileName << ":" << newBbi.lineNumber);
                        DBG("####End First instance in Context " << contextID);
                    }                
                    //oldTime[contextID] = timestamp;
                    //oldBbid[contextID] = bbid; 
                    //hasTime[contextID] = false;
                    oldTime = timestamp;
                    oldBBID = bbid;
                }
                break;  
            }
            default: {
                if (currTask->getType() == task_type_join ||
                    currTask->getType() == task_type_sync ||
                    currTask->getType() == task_type_barrier ||
                    currTask->getType() == task_type_create) {
                    hasTime[contextID] = true;
                }
                uint64_t ti = currTask->getStartTime();
                uint64_t to = currTask->getEndTime();
                if (tick.count(contextID) == 0) {
                    tick[contextID] = ti;
                } else if (to > tock[contextID]) {
                    tock[contextID] = to;
                }
                uint64_t delta = to - ti;
                if (sumDelta.count(contextID) == 0) {
                    sumDelta[contextID] = delta;
                } else {
                    sumDelta[contextID] += delta;
                }
                DBG(delta << "\t" << currTask->getType() << "\t" << taskID << "\ttock: " << to << "\ttick: " << ti);
                oldTime[contextID] = to;
            }
        }
        delete currTask;
    }

    /*for (auto I = tick.begin(), IE = tick.end(); I != IE; ++I) {
        auto contextID = I->first;
        assert(tock.count(contextID) == 1);
        assert(sumDelta.count(contextID) == 1);
        uint64_t delta = tock[contextID] - I->second;
        assert(delta >= 0);
        cerr << contextID << ": " << delta << "\t\tsumDelta: " << sumDelta[contextID] << 
                "\t\t\"Wait\" time: " << sumDelta[contextID] - delta << endl;
    }*/

    for (auto I = bins.begin(), IE = bins.end(); I != IE; ++I) {
        auto bc = I->first;
        assert(bins[bc].size() == binCount[bc].size());
        auto means = I->second;
        auto counts = binCount[bc];
        assert(bc.size() == 1);
        auto J = bc.begin();
        auto bbid = J->first;
        auto contextID = J->second;
        auto bbi = tgi->getBasicBlockInfo(bbid);
        for (uint i = 0, e = means.size(); i != e; i++) {
            uint64_t mean = means[i];
            uint count = counts[i]; 
            outBins << mean << " " << count << " " << bbid << " " << contextID <<  
                       " " << bbi.numOfOps << " " << bbi.numOfMemOps <<
                       " " << bbi.critPathLen << "\n";
        }
    }

    // cleanup
    delete tg;
    //delete tgi;

    // close
    out.close();
    outBins.close();
}
