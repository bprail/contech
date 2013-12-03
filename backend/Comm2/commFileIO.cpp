#include "commFileIO.hpp"

using namespace contech;

commFileIORecord::commFileIORecord(ct_sharers_t destMask,uint count){
    this->destMask = destMask;
    this->count = count;
}

void commFileIORecord::print(){
    cout << CommTracker::getSharersString(this->destMask) << ";" << this->count <<endl;
}




commFileIO::commFileIO(){
    this->commFileIORecords = createMap();
}
unordered_map<string,vector<commFileIORecord*>> commFileIO::createMap(){
    unordered_map<string,vector<commFileIORecord*>> m;
    return m;
}

void commFileIO::serialize(string filename){
    ofstream outfile;
    outfile.open (filename);
    for (unordered_map<string,vector<commFileIORecord*>>::iterator it=this->commFileIORecords.begin(); it!=this->commFileIORecords.end(); ++it){
        
        for(int i=0;i<it->second.size();i++){
            outfile << it->first << "->" << it->second[i]->destMask << ";" <<  it->second[i]->count << endl;
        }
    }
}

void commFileIO::deserialize(string filename){
    ifstream infile;
    infile.open(filename);
    
    string line;
    size_t pos1=0,pos2=0;
    while (getline(infile, line)){
        //get the bbSig
        pos1 = line.find_first_of("->");
        string bbSig = line.substr(0,pos1);
        
        //then the destMask
        pos2 = line.find_first_of(";");
        ct_sharers_t destMask = atoi(line.substr(pos1+2,pos2-pos1-2).c_str());
        
        //finally the count
        uint count = atoi(line.substr(pos2+1,line.size()).c_str());
        
        //and store in the vector identified by the bbSig
        commFileIORecord* record = new commFileIORecord(destMask,count);
        this->commFileIORecords[bbSig].push_back(record);
    }
    
}

void commFileIO::print(){
    
    //for reach entry in the map
    for (unordered_map<string,vector<commFileIORecord*>>::iterator it=this->commFileIORecords.begin(); it!=this->commFileIORecords.end(); ++it){
        
        //print every entry in the vector
        for(int i=0;i<it->second.size();i++){
            cout << it->first << "->";
            it->second[i]->print();
        }
    }
}

// Returns a set of the contech id's present in a sharers vector
set<ContextId> commFileIO::getSharersFromBitVector(uint64_t sharers)
{
    set<ContextId> s;
    for (uint i = 0; i < sizeof(uint64_t) * 8; i++)
    {
        if (sharers & (1 << i)) {s.insert(ContextId(i));}
    }
    return s;
}

// Returns a single prediction to the most frequently occuring communication target
ct_sharers_t commFileIO::predictMax(ContextId srcThread, string basicBlockSignature){
    stringstream ss;
    ss << srcThread <<  "|" << basicBlockSignature;

    string key = ss.str();
    vector<commFileIORecord*> recordList = this->commFileIORecords[key];
    map<ContextId, uint> bins;

    // Build a histogram of the accesses to each thread
    for (commFileIORecord* r : recordList)
    {
        for (ContextId tid : getSharersFromBitVector(r->destMask))
        {
            bins[tid] += r->count;
        }
    }

    // Predict the contech with the largest bin
    ct_sharers_t mask = 0;
    uint maximum = 0;
    for (pair<ContextId, uint> bin : bins)
    {
        if (bin.second > maximum)
        {
            maximum = bin.second;
            mask = 1 << (uint)bin.first;
        }
    }

    return mask;
}

#define PUSH_MASK             (0x80000000)

// Returns a single prediction to the most frequently occuring communication target
ct_sharers_t commFileIO::predictStableMax(ContextId srcThread, string basicBlockSignature){
    stringstream ss;
    ss << srcThread <<  "|" << basicBlockSignature;

    string key = ss.str();
    vector<commFileIORecord*> recordList = this->commFileIORecords[key];
    map<ContextId, uint> bins;

    // Build a histogram of the accesses to each thread
    for (commFileIORecord* r : recordList)
    {
        for (ContextId tid : getSharersFromBitVector(r->destMask))
        {
            bins[tid] += r->count;
        }
    }


    if (bins.size() == 0) return 0; // If there is no data available, don't make a prediction

    // Find the average and max
    ct_sharers_t mask = 0;
    uint maximum = 0;
    uint average = 0;
    uint sum = 0;
    for (pair<ContextId, uint> bin : bins)
    {
        sum += bin.second;

        if (bin.second > maximum)
        {
            maximum = bin.second;
            mask = 1 << (uint)bin.first;
        }
    }

    average = sum / bins.size();

    // Only predict the max value if it is three times larger than the average
    if (maximum > average * 2) return mask | PUSH_MASK;

    return 0;
}


// Returns predictions to the communication targets that occur more frequently than the average target
ct_sharers_t commFileIO::predictAboveAverage(ContextId srcThread, string basicBlockSignature){
    stringstream ss;
    ss << srcThread <<  "|" << basicBlockSignature;

    string key = ss.str();
    vector<commFileIORecord*> recordList = this->commFileIORecords[key];
    map<ContextId, uint> bins;

    // Build a histogram of the accesses to each thread
    for (commFileIORecord* r : recordList)
    {
        for (ContextId tid : getSharersFromBitVector(r->destMask))
        {
            bins[tid] += r->count;
        }
    }

    // Find the average
    uint average = 0;
    if (bins.size() > 0)
    {
        uint sum = 0;
        for (pair<ContextId, uint> bin : bins)
        {
            sum += bin.second;
        }
        average = sum / bins.size();
    }


    // Predict all that are greater than the average
    ct_sharers_t mask = 0;
    for (pair<ContextId, uint> bin : bins)
    {
        if (bin.second >= average) mask |= (1 << (uint)bin.first);
    }
    return mask;
}

