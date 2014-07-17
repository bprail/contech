#include "../../common/taskLib/ct_file.h"
#include "../../common/taskLib/TaskGraph.hpp"
#include "../CommTracker/CommTracker.hpp"
#include "commFileIO.hpp"
#include <cstring>
#include <vector>
#include <set>
#include <iostream>
#include <iomanip>
#include <sstream>

using namespace std;
using namespace contech;

string blockSendsToThreadAnalysis(CommTracker* tracker);
string blockReceivesFromThreadAnalysis(CommTracker* tracker);
string blockTrafficAnalysis(CommTracker* tracker, uint minThreshold);
string migratoryAnalysis(CommTracker* tracker, float minThreshold);
string threadCommunicationAnalysis(CommTracker* tracker);

void bbCommAnalysisFromSrc(CommTracker* tracker, TaskGraph& taskGraph, string outFile);
void bbCommAnalysisFromDst(CommTracker* tracker, TaskGraph& taskGraph, string outFile);
string getBasicBLockStringFromSignature(string bbSig);
string getBasicBlockSignatureWithThread(Task::basicBlockActionCollection bbList,ContextId contech);


int main(int argc, const char *argv[])
{
    
    //input check
    if(argc != 3){
        cerr << "Usage: " << argv[0] << " taskGraphInputFile output.commprofile" << endl;
        exit(1);
    }

    ct_file* taskGraphFile  = create_ct_file_r(argv[1]);
    if(isClosed(taskGraphFile)){
        cerr << "ERROR: Couldn't open input file" << endl;
        exit(1);
    }

    cerr << "Analyzing communication..." << endl;
    // Parse the task graph with a comm tracker
    CommTracker* tracker = CommTracker::fromFile(taskGraphFile);
    cerr << "Recorded " << tracker->getRecords().size() << " communication instances. " << endl;

    /*
    // Reset the file so it can be used again
    close_ct_file(taskGraphFile);
    taskGraphFile  = create_ct_file_r(argv[1]);

    // Initialize the task graph for random access, if we need it
    cerr << "Initializing task graph for random access..." << endl;
    ct_graph taskGraph(taskGraphFile);
    taskGraph.initializeFileOffsetMap();
    cerr << "Done." << endl;
    */


    cout << "blockSendsToThreadAnalysis:" << endl;
    cout << blockSendsToThreadAnalysis(tracker) << endl;
    cout << "blockReceivesFromThreadAnalysis:" << endl;
    cout << blockReceivesFromThreadAnalysis(tracker) << endl;
    cout << "migratoryAnalysis:" << endl;
    cout << migratoryAnalysis(tracker, 1.0) << endl;
    cout << "threadCommunicationAnalysis" << endl;
    cout << threadCommunicationAnalysis(tracker) << endl;
    cout << "CommTracker" << endl;

    ofstream trackerFile(string(argv[2]) + ".json");
    trackerFile << *tracker << endl;


    //blockTrafficAnalysis(tracker, 0);


    /*
    // Analyze the communication records
    string srcFileName = argv[2];
    srcFileName += ".src";
    string dstFileName = argv[2];
    dstFileName += ".dst";
    cerr << "Start communication analysis of basic blocks. Format: sender -> receivers" << endl;
    bbCommAnalysisFromSrc(tracker, taskGraph, srcFileName);
    cout << endl;
    
    cerr << "Start communication analysis of basic blocks. Format: reciever -> sender" << endl;
    bbCommAnalysisFromDst(tracker, taskGraph, dstFileName);
//    threadCommunicationAnalysis(tracker);
    */

    close_ct_file(taskGraphFile);
    delete tracker;
    return 0;
}

// Basic block sender analysis
string blockSendsToThreadAnalysis(CommTracker* tracker)
{
    map<uint, map<ContextId, uint> > blockSendsToThreadCounter;
    for (CommRecord& r : tracker->getRecords())
    {
        blockSendsToThreadCounter[r.srcBlock][r.receiver.getContextId()]++;
    }

    ostringstream out;
    for (auto& p : blockSendsToThreadCounter)
    {
        uint bbId = p.first;
        out << "Basic block " << bbId << " sends to:" << endl;
        for (auto& t : p.second)
        {
            out << "\tThread " << t.first << ": " << t.second << endl;
        }
    }
    return out.str();
}

// Basic block receiver analysis
string blockReceivesFromThreadAnalysis(CommTracker* tracker)
{
    map<uint, map<ContextId, uint> > blockReceivesFromThreadCounter;
    for (CommRecord& r : tracker->getRecords())
    {
        blockReceivesFromThreadCounter[r.dstBlock][r.sender.getContextId()]++;
    }

    ostringstream out;
    for (auto& p : blockReceivesFromThreadCounter)
    {
        uint bbId = p.first;
        out << "Basic block " << bbId << " receives from:" << endl;
        for (auto& t : p.second)
        {
            out << "\tThread " << t.first << ": " << t.second << endl;
        }
    }
    return out.str();
}

// Breaks down traffic by sender block, thread, and receiver block
string blockTrafficAnalysis(CommTracker* tracker, uint minThreshold)
{
    ostringstream out;
    out << endl << "Communication Traffic (in bytes) by thread and basic block: " << endl;
    map<uint, map<ContextId, map<ContextId, map<uint, uint> > > > theDataStructureWhichShallNotBeNamed;
    for (CommRecord& r : tracker->getRecords())
    {
        theDataStructureWhichShallNotBeNamed[r.srcBlock][r.sender.getContextId()][r.receiver.getContextId()][r.dstBlock]++;
    }

    ostringstream buffer;

    for (auto& a : theDataStructureWhichShallNotBeNamed)
    {
        uint srcBlock = a.first;
        buffer << "Basic Block " << srcBlock << ":" << endl;
        for (auto& b : a.second)
        {
            ContextId sender = b.first;
            buffer << "\tFrom thread " << sender << ":" << endl;
            for (auto& c : b.second)
            {
                ContextId receiver = c.first;
                buffer << "\t\tTo thread " << receiver << ":" << endl;
                for (auto& d : c.second)
                {
                    uint dstBlock = d.first;
                    uint count = d.second;
                    if (count > minThreshold)
                    {
                        out << buffer.str();
                        out << "\t\t\tTo block " << dstBlock << ": " << count << endl;
                    }
                    buffer.str("");
                }
            }
        }
    }
    return out.str();
}


// Breaks down traffic by sender block, thread, and receiver block
string migratoryAnalysis(CommTracker* tracker, float minThreshold)
{
    ostringstream out;
    out.setf(ios::fixed, ios::floatfield);
    out.setf(ios::showpoint);
    out << setprecision(2);
    out << endl << " Migratory patterns per basic block: " << endl;
    // Count total number of runs for all basic blocks
    uint total = 0;
    for (auto& p : tracker->getBbStats()) { total += p.second.runCount; }

    for (auto& p : tracker->getBbStats())
    {
        uint bbId = p.first;
        uint runCount = p.second.runCount;
        float runPercent = 100 * (float) runCount / total;
        if (runPercent < minThreshold) continue;
        out << "BB #" << bbId << ": ran " << runCount << " times (" << runPercent << "%)" << endl;
        map<uint,memOpStats>& stats = p.second.memOps;
        for (map<uint,memOpStats>::iterator f = stats.begin(), e = stats.end(); f != e; ++f)
        {
            memOpStats& m = f->second;
            if (m.numMigratory < 1) continue;
            float percentHit = 100 * (float)m.numHits / runCount;
            float percentMigratory = 100 * (float)m.numMigratory / runCount;
            out << "\t" << f->first << ": ";
            out << m.numHits      << " (" << percentHit       << "%) hit, ";
            out << m.numMigratory << " (" << percentMigratory << "%) migratory" << endl;
        }
    }
    return out.str();
}

/*
 Unfinished
void blockWriteSetAnalysis(CommTracker* tracker)
{
    map<uint, uint64> maxAddrTouchedByBlock;
    map<uint, map<ContextId, uint> > blockReceivesFromThreadCounter;
    for (CommRecord& r : tracker->getRecords())
    {
        blockReceivesFromThreadCounter[r.dstBlock][r.sender.getContextId()]++;
    }
    for (auto& p : blockReceivesFromThreadCounter)
    {
        uint bbId = p.first;
        cout << "Basic block " << bbId << " receives from:" << endl;
        for (auto& t : p.second)
        {
            cout << "\tThread " << t.first << ": " << t.second << endl;
        }
    }
}
*/
void bbCommAnalysisFromSrc(CommTracker* tracker, TaskGraph& taskGraph, string outFile)
{
    // 'first' is the sender. 'second' is a bit vector of receivers
    map<uint64_t, ct_sharers_t> destinations;

    // Maps number of times a multicast communication has happened.
    map< pair<string, ct_sharers_t>, uint > count;

    //This map allows skipping signature construction
    map<TaskId,string> sigMap;
    
    // Last writer per address
    map<uint64_t, string> owner;
    int i=0;
    clock_t t;
    unsigned long bbCount = 0;
    for (CommRecord r : tracker->getRecords())
    {
        /*
        if(tracker->getRecords().size() > 2000){
        
            if(i%1000 == 1){
                t = clock();
            }
            if(i%1000 == 0){
                t = clock() - t;
                cout << "CommPass1: Processing iteration " << i << " of " << tracker->getRecords().size() <<" Time per 1000: " << ((float)t)/CLOCKS_PER_SEC << " Average Basic Blocks per iteration:" << ((float)bbCount)/1000 <<endl;
                bbCount = 0;
            }
        } else {
            cout << "CommPass1: Processing iteration " << i << " of " << tracker->getRecords().size() << endl;
        }
        */
        // Obtain a signature of the source based on the basic blocks it executed
        Task* srcTask = taskGraph.getTaskById(r.sender);
        bbCount += srcTask->getBasicBlockActions().size();
        
        
        //check if this signature has alread been computed, and fetch it if it has.
        string srcBasicBlockSig;
        if( sigMap.count(srcTask->getTaskId()) == 0 ){
            srcBasicBlockSig = getBasicBlockSignatureWithThread(srcTask->getBasicBlockActions(),srcTask->getContextId());
            sigMap[srcTask->getTaskId()] = srcBasicBlockSig;
        } else {
            srcBasicBlockSig = sigMap[srcTask->getTaskId()];
        }
        
        
        // Get contech ID of destination
        ContextId dstContech = r.receiver.getContextId();

        ct_sharers_t dstMask = 1 << (uint)dstContech;
        ct_sharers_t& currentMask = destinations[r.address];

        // If we see another read from one of the sharers, there must have been a write
        if (currentMask & dstMask || owner[r.address] != srcBasicBlockSig)
        {
            count[make_pair(owner[r.address], currentMask)]++;
            currentMask = 0;
            owner[r.address] = srcBasicBlockSig;
        }

        // Add the receiver to the sender's list of destinations
        currentMask |= dstMask;
        i++;
        
    }


    // Record residual data
    for (map<uint64_t, string>::iterator f = owner.begin(), e = owner.end(); f != e; f++)
    {
        uint64_t address = f->first;
        string owner = f->second;
        ct_sharers_t& finalMask = destinations[address];
        count[make_pair(owner, finalMask)]++;
    }

    
    commFileIO* serializer = new commFileIO();
    // Print results and fill out commFileIORecords
    for (map< pair<string, ct_sharers_t>, uint>::iterator f = count.begin(), e = count.end(); f != e; f++)
    {
        cout << getBasicBLockStringFromSignature(f->first.first) << "->" << CommTracker::getSharersString(f->first.second) << ";" << f->second << endl;
        
        //create records
        commFileIORecord* record = new commFileIORecord((f->first.second),f->second);
        
        serializer->commFileIORecords[f->first.first].push_back(record);
    }
    
    serializer->serialize(outFile);
    delete serializer;
}

void bbCommAnalysisFromDst(CommTracker* tracker, TaskGraph& taskGraph, string outFile)
{
    // 'first' is the destination. 'second' is a bit vector of senders
    map<uint64_t, ct_sharers_t> sources;

    // Maps number of times a multicast communication has happened.
    map< pair<string, ct_sharers_t>, uint > count;

    
    //This map allows skipping signature construction
    map<TaskId,string> sigMap;
    
    // Last reader per address
    map<uint64_t, string> owner;
    int i=0;
    clock_t t;
    unsigned long bbCount = 0;
    for (CommRecord r : tracker->getRecords())
    {

        /*
        if(tracker->getRecords().size() > 2000){
        
            if(i%1000 == 1){
                t = clock();
            }
            if(i%1000 == 0){
                t = clock() - t;
                cout << "CommPass2: Processing iteration " << i << " of " << tracker->getRecords().size() <<" Time per 1000: " << ((float)t)/CLOCKS_PER_SEC << " Average Basic Blocks per iteration:" << ((float)bbCount)/1000 << endl;
                bbCount=0;
            }
        } else {
            cout << "CommPass2: Processing iteration " << i << " of " << tracker->getRecords().size() << endl;
        }
        */

        // Obtain a signature of the destination based on the basic blocks it executed
        //cout << "Receiver: " << r.receiver << endl;
        Task* dstTask = taskGraph.getTaskById(r.receiver);
        bbCount += dstTask->getBasicBlockActions().size();
        
        
        //check if this signature has alread been computed, and fetch it if it has.
        string dstBasicBlockSig;
        if( sigMap.count(dstTask->getTaskId()) == 0 ){
            dstBasicBlockSig = getBasicBlockSignatureWithThread(dstTask->getBasicBlockActions(),dstTask->getContextId());
            sigMap[dstTask->getTaskId()] = dstBasicBlockSig;
        } else {
            dstBasicBlockSig = sigMap[dstTask->getTaskId()];
        }
        
        // Get contech ID of source
        ContextId srcContech = r.sender.getContextId();

        ct_sharers_t srcMask = 1 << (uint)srcContech;
        ct_sharers_t& currentMask = sources[r.address];

        // If we see another write from one of the sharers, there must have been a read
        if (currentMask & srcMask || owner[r.address] != dstBasicBlockSig)
        {
            count[make_pair(owner[r.address], currentMask)]++;
            currentMask = 0;
            owner[r.address] = dstBasicBlockSig;
        }

        // Add the sender to the receiver's list of source
        currentMask |= srcMask;
        
        i++;
        
    }


    // Record residual data
    for (map<uint64_t, string>::iterator f = owner.begin(), e = owner.end(); f != e; f++)
    {
        uint64_t address = f->first;
        string owner = f->second;
        ct_sharers_t& finalMask = sources[address];
        count[make_pair(owner, finalMask)]++;
    }

    
    commFileIO* serializer = new commFileIO();
    // Print results and fill out commFileIORecords
    for (map< pair<string, ct_sharers_t>, uint>::iterator f = count.begin(), e = count.end(); f != e; f++)
    {
        cout << getBasicBLockStringFromSignature(f->first.first) << "->" << CommTracker::getSharersString(f->first.second) << ";" << f->second << endl;
        
        //create records
        commFileIORecord* record = new commFileIORecord((f->first.second),f->second);
        
        serializer->commFileIORecords[f->first.first].push_back(record);
    }
    
    serializer->serialize(outFile);
    delete serializer;
}


string threadCommunicationAnalysis(CommTracker* tracker)
{
    ostringstream out;

    // 'first' is the sender. 'second' is a bit vector of receivers
    map<uint64_t, ct_sharers_t> destinations;

    // Maps number of times a multicast communication has happened.
    map< pair<ContextId, ct_sharers_t>, uint > count;

    // Last writer per address
    map<uint64_t, ContextId> owner;

    for (CommRecord r : tracker->getRecords())
    {
        ContextId srcContech = r.sender.getContextId();
        ContextId dstContech = r.receiver.getContextId();
        //out << srcContech << " " << r.address << " " << dstContech << endl;

        ct_sharers_t dstMask = 1 << (uint)dstContech;
        ct_sharers_t& currentMask = destinations[r.address];
        //out << "Current mask for " << srcContech << ", addr (" << r.address << "): "<< CommTracker::getSharersString(currentMask) << endl;

        // If we see another read from one of the sharers, there must have been a write
        if (currentMask & dstMask || owner[r.address] != srcContech)
        {
            //out << "New writer!" << endl << "Counting : " << owner[r.address] << "->" << CommTracker::getSharersString(currentMask) << " for (" << r.address << ")" << endl;
            count[make_pair(owner[r.address], currentMask)]++;
            currentMask = 0;
            owner[r.address] = srcContech;
        }

        // Add the receiver to the sender's list of destinations
        //out << "Adding sharer: " << CommTracker::getSharersString(dstMask) << " [destMask=" << dstMask << "]" << endl;

        currentMask |= dstMask;
    }

    // Record residual data
    for (map<uint64_t, ContextId>::iterator f = owner.begin(), e = owner.end(); f != e; f++)
    {
        uint64_t address = f->first;
        ContextId owner = f->second;
        ct_sharers_t& finalMask = destinations[address];
        if (finalMask == 0) { continue; }
        count[make_pair(owner, finalMask)]++;
    }

    // Print results
    for (map< pair<ContextId, ct_sharers_t>, uint>::iterator f = count.begin(), e = count.end(); f != e; f++)
    {
        out << f->first.first << "->" << CommTracker::getSharersString(f->first.second) << "," << f->second << endl;
    }
    return out.str();
}

string getBasicBlockSignatureWithThread(Task::basicBlockActionCollection bbList,ContextId contech)
{
    string str = contech.toString() + "|";
    int bbSigArraySize = 1024;
    char bbSigArray[bbSigArraySize];
    for(int i=0;i<bbSigArraySize;i++){
        bbSigArray[i] = '0';
    }
    
    for(BasicBlockAction a : bbList){
        bbSigArray[a.basic_block_id%bbSigArraySize] = '1';
    }
    bbSigArray[bbSigArraySize-1] = '\0';
    str += bbSigArray;

    return str;
}

string getBasicBLockStringFromSignature(string bbSig){
    size_t pos1=0,pos2=0;
    //get the bbSig
    string bbSigDec = ""; 
    pos1 = bbSig.find_first_of("|");
    bbSigDec += bbSig.substr(0,pos1+1);
    pos2 = bbSig.find_first_of("->");
    string bbSigBin = bbSig.substr(pos1+1,(pos2-pos1)+1);
    
    for (int i=0; i<bbSigBin.length(); ++i){
        if(bbSigBin[i] == '1'){
            bbSigDec += to_string(i) + ",";
        }
    }
    //cout << bbSigDec << endl;
    
    return bbSigDec;
}


