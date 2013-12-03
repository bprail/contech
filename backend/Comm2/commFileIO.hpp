#include "../../common/taskLib/Task.hpp"
#include "../CommTracker/CommTracker.hpp"
#include <string>
#include <unordered_map>
#include <set>
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>


using namespace std;
class commFileIO;
class commFileIORecord {
public:
    ct_sharers_t destMask;
    unsigned int count;
    
    //constructor
    commFileIORecord(ct_sharers_t destMask,unsigned int count);
    void print();
    
};


class commFileIO {
private:
    static set<contech::ContextId> getSharersFromBitVector(uint64_t sharers);
public:
    commFileIO();
    
    //mapping of a basic block signature (0|1,2,3,4,) to a commFileIORecord
    //which is identified by a destMask and count of communication
    unordered_map<string,vector<commFileIORecord*>> commFileIORecords;
    unordered_map<string,vector<commFileIORecord*>> createMap();
    
    //functions to get data into and out of files
    void serialize(string filename);
    void deserialize(string filename);
    
    void print();
    ct_sharers_t predictStableMax(contech::ContextId srcThread, string basicBlockSignature);
    ct_sharers_t predictMax(contech::ContextId srcThread, string basicBlockSignature);
    ct_sharers_t predictAboveAverage(contech::ContextId srcThread, string basicBlockSignature);

    
    // TODO Migratory data predictions
    
};
