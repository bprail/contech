#include "graphviz/gvc.h"
#include "graphviz/cgraph.h"
#include "../../common/taskLib/Task.hpp"
#include "../../common/taskLib/TaskGraph.hpp"
#include "../CommTracker/CommTracker.hpp"
#include <iostream>
#include <map>
#include <set>
#include <vector>
#include <sstream>
#include <string>
#include <algorithm>

using namespace contech;

#define MAX_GRAPH_NODE_SIZE 100

class CompressTask{
public:
    TaskId taskId;
    ct_timestamp duration;
    // list of successor tasks
    vector<TaskId> s;
    // list of predecessor tasks
    vector<TaskId> p;

    // The type of event that this task represents
    task_type type;
    sync_type syncType;

    // more details information about this task
    uint64 startLine;
    uint64 endLine;
    uint64 numOfMemOps;
    uint64 numOfOps;
    uint64 critPathLen;
    uint64 memBytes;
    string functionName;
    string fileName;
    string callsFunction;

    CompressTask(Task* currentTask, TaskGraphInfo* tgi){
        taskId = currentTask->getTaskId();
        duration = currentTask->getEndTime() - currentTask->getStartTime();
        type = currentTask->getType();
        syncType = currentTask->getSyncType();

        startLine = 0;
        endLine = 0;
        numOfMemOps = 0;
        numOfOps = 0;
        critPathLen = 0;
        memBytes = 0;
        functionName = "";
        fileName = "";
        callsFunction = "";

        vector<TaskId> ts(currentTask->getSuccessorTasks());
        vector<TaskId> tp(currentTask->getPredecessorTasks());
        s = ts;
        p = tp;

        auto bba = currentTask->getBasicBlockActions();
        auto f = bba.begin(), e = bba.end();
        while (f != e) {
            BasicBlockAction bb = *f;
            auto bbi = tgi->getBasicBlockInfo((uint)bb.basic_block_id);
            if (f == bba.begin()) {
                startLine = bbi.lineNumber;
                functionName = bbi.functionName;
                callsFunction = bbi.callsFunction;
                fileName = bbi.fileName;
            }
            numOfMemOps += bbi.numOfMemOps;
            numOfOps += bbi.numOfOps;
            critPathLen += bbi.critPathLen;
            // Note that memory actions include malloc, etc
            for (MemoryAction mem : f.getMemoryActions())
            {
                if (mem.type == action_type_mem_read || mem.type == action_type_mem_write)
                    memBytes += (0x1 << mem.pow_size);
            }
            if ((++f) == e){
                endLine = bbi.lineNumber;
            }
        }
        if (startLine > endLine) {
            swap(startLine, endLine);
        }
    }

};

void depict_compressed_graph(TaskGraph* tg, graph_t *g){
    Task* currentTask = NULL;
    int createIfItDoesntExist = 1;

    set< pair<TaskId, TaskId> > edgeSet;
    map<TaskId, Agnode_t*> nodes;

    TaskGraphInfo* tgi = tg->getTaskGraphInfo();

    map<TaskId, CompressTask*> ctTable;
    map<TaskId, CompressTask*> ctUniTable; // store unique CompressTask struct
    set<TaskId> ctVisit;
    CompressTask *ct;

     // load all Task into memory
    while((currentTask = tg->readContechTask()) != NULL) {
        cout<<"load Task "<<currentTask->getTaskId().toString()<<endl;

        ct = new CompressTask(currentTask, tgi);
        ctTable[ct->taskId] = ct;
        ctUniTable[ct->taskId] = ct;
        delete currentTask;
    }

    if (ctTable.empty()) {
        cout<<"no task inside the graph file"<<endl;
        return;
    }

    // breadth first search
    vector<TaskId> queue;
    queue.push_back((ctTable.begin())->second->taskId);
    while (!queue.empty()){
        ct = ctTable[queue[0]];
        cout<<"compress task "<<ct->taskId.toString()<<endl;
        queue.erase(queue.begin());
        if (ct->type == task_type_basic_blocks) { // basic block type
            if (ct->s.size() == 1) { // only have one successor
                CompressTask *chld = ctTable[ct->s[0]];
                if (chld->type == task_type_basic_blocks) { 
                    // merge consecutive basic block
                    cout<<"merge consecutive basic block "<<ct->taskId.toString()<<" and "<<chld->taskId.toString()<<endl;
                    ctTable[chld->taskId] = ct;
                    ct->duration += chld->duration;
                    ct->s = chld->s;
                    ct->startLine = min(ct->startLine, chld->startLine);
                    ct->endLine = max(ct->endLine, chld->endLine);
                    ct->numOfMemOps += chld->numOfMemOps;
                    ct->numOfOps += chld->numOfOps;
                    ct->critPathLen += chld->critPathLen;
                    ct->memBytes += chld->memBytes;
                    // delete chld
                    ctUniTable.erase(chld->taskId);
                    delete chld;
                    queue.push_back(ct->taskId);
                } else {
                    queue.push_back(chld->taskId);
                }
            } else { // has more successor
                for (TaskId childTaskId : ct->s) {
                    if (ctVisit.find(childTaskId) == ctVisit.end()) {
                        queue.push_back(childTaskId);
                        ctVisit.insert(childTaskId);
                    }
                }
            }
        } else if (ct->type == task_type_create) { // create node
            bool allCompressed = true;
            for (uint i=0; i<ct->s.size(); i++) {
                // judge whether the grandchild is also a create node
                CompressTask *chld = NULL, *gchld = NULL;
                chld = ctTable[ct->s[i]];
                if (chld->type == task_type_basic_blocks && chld->s.size() == 1) {
                    gchld = ctTable[chld->s[0]];
                    if (gchld->type == task_type_create) {
                        // merge
                        cout<<"merge consecutive basic block "<<ct->taskId.toString()<<" and "<<chld->taskId.toString()<<endl;
                        ctTable[chld->taskId] = ct;
                        ct->duration += chld->duration;
                        ct->startLine = min(ct->startLine, chld->startLine);
                        ct->endLine = max(ct->endLine, chld->endLine);
                        ct->numOfMemOps += chld->numOfMemOps;
                        ct->numOfOps += chld->numOfOps;
                        ct->critPathLen += chld->critPathLen;
                        ct->memBytes += chld->memBytes;
                        // delete chld
                        ctUniTable.erase(chld->taskId);
                        delete chld;

                        // merge
                        chld = gchld;
                        cout<<"merge consecutive basic block "<<ct->taskId.toString()<<" and "<<chld->taskId.toString()<<endl;
                        ctTable[chld->taskId] = ct;
                        ct->duration += chld->duration;
                        ct->startLine = min(ct->startLine, chld->startLine);
                        ct->endLine = max(ct->endLine, chld->endLine);
                        ct->numOfMemOps += chld->numOfMemOps;
                        ct->numOfOps += chld->numOfOps;
                        ct->critPathLen += chld->critPathLen;
                        ct->memBytes += chld->memBytes;
                        // delete chld
                        ctUniTable.erase(chld->taskId);

                        // update successor list
                        ct->s.erase(ct->s.begin()+i);
                        for (uint j=0; j<chld->s.size(); j++) {
                            ct->s.push_back(chld->s[j]);
                        }
                        delete chld;

                        allCompressed = false;
                        break;
                    }
                }
            }
            if (allCompressed) {
                for (TaskId childTaskId : ct->s) {
                    if (ctVisit.find(childTaskId) == ctVisit.end()) {
                        queue.push_back(childTaskId);
                        ctVisit.insert(childTaskId);
                    }
                }
            } else {
                queue.push_back(ct->taskId);
            }
        } else if (ct->type == task_type_join) { // join node
            bool allCompressed = true;
            if (ct->s.size() == 1) {
                // judge whether the grandchild is also a join node
                CompressTask *chld = NULL, *gchld = NULL;
                chld = ctTable[ct->s[0]];
                if (chld->type == task_type_basic_blocks && chld->s.size() == 1) {
                    gchld = ctTable[chld->s[0]];
                    if (gchld->type == task_type_join) {
                        // merge
                        cout<<"merge consecutive basic block "<<ct->taskId.toString()<<" and "<<chld->taskId.toString()<<endl;
                        ctTable[chld->taskId] = ct;
                        ct->duration += chld->duration;
                        ct->startLine = min(ct->startLine, chld->startLine);
                        ct->endLine = max(ct->endLine, chld->endLine);
                        ct->numOfMemOps += chld->numOfMemOps;
                        ct->numOfOps += chld->numOfOps;
                        ct->critPathLen += chld->critPathLen;
                        ct->memBytes += chld->memBytes;
                        // delete chld
                        ctUniTable.erase(chld->taskId);
                        delete chld;

                        // merge
                        chld = gchld;
                        cout<<"merge consecutive basic block "<<ct->taskId.toString()<<" and "<<chld->taskId.toString()<<endl;
                        ctTable[chld->taskId] = ct;
                        ct->duration += chld->duration;
                        ct->startLine = min(ct->startLine, chld->startLine);
                        ct->endLine = max(ct->endLine, chld->endLine);
                        ct->numOfMemOps += chld->numOfMemOps;
                        ct->numOfOps += chld->numOfOps;
                        ct->critPathLen += chld->critPathLen;
                        ct->memBytes += chld->memBytes;
                        // delete chld
                        ctUniTable.erase(chld->taskId);

                        // update successor list
                        ct->s = chld->s;
                        // update predecessor list
                        for (uint j=0; j<chld->p.size(); j++) {
                            if (ctTable[chld->p[j]] != ct) {
                                ct->p.push_back(chld->p[j]);
                                CompressTask *parent = ctTable[chld->p[j]];
                                for (uint k=0; k<parent->s.size(); k++) {
                                    if (parent->s[k] == chld->taskId) {
                                        parent->s[k] = ct->taskId;
                                        break;
                                    }
                                }
                            }
                        }
                        delete chld;

                        allCompressed = false;
                    }
                }
            } else {
                cout<<"join node "<<ct->taskId.toString()<<" have "<<ct->s.size()<<" successors, strange"<<endl;
            }
            if (allCompressed) {
                for (TaskId childTaskId : ct->s) {
                    if (ctVisit.find(childTaskId) == ctVisit.end()) {
                        queue.push_back(childTaskId);
                        ctVisit.insert(childTaskId);
                    }
                }
            } else {
                queue.push_back(ct->taskId);
            }
        } else {
            for (TaskId childTaskId : ct->s) {
                if (ctVisit.find(childTaskId) == ctVisit.end()) {
                    queue.push_back(childTaskId);
                    ctVisit.insert(childTaskId);
                }
            }
        }
        
    }

    for (auto f = ctUniTable.begin(), e = ctUniTable.end(); f != e; ++f)
    {
        ct = f->second;
        // give a node its name
        string nodeName = ct->taskId.toString() + "\n" + 
        "Duration = "+to_string(ct->duration/1000000)+"ms \n"+
        "Memory:"+to_string(ct->memBytes)+" bytes"+"\n"+
        "Lines:"+to_string(ct->startLine)+"-"+to_string(ct->endLine)+"\n"+
        "Func:"+ct->functionName+"\n"+
        "File:"+ct->fileName;
        
        cout<<"Graph add node "<<ct->taskId.toString()<<endl;
        Agnode_t *currentNode = agnode(g, (char*) ct->taskId.toString().c_str(), createIfItDoesntExist);

        agset (currentNode, (char*)"label",(char*)nodeName.c_str());

        for (TaskId childTaskId : ct->s)
        {
            edgeSet.insert(make_pair(ct->taskId, childTaskId));
            cout<<"add edge "<<ct->taskId.toString()<<" -- "<<childTaskId.toString()<<endl;
        }

        string label;

        // Colorize nodes based on event type
        switch(ct->type){
            case task_type_sync:
                agset (currentNode, (char*)"color",(char*)"darkorange");
                label = "Sync\nduration="+to_string(ct->duration/1000000)+"ms";
                agset (currentNode, (char*)"label",(char*)label.c_str());
                goto allsyncs;
            case task_type_barrier:
                agset (currentNode, (char*)"color",(char*)"darkorange");
                label = "Barrier\nduration="+to_string(ct->duration/1000000)+"ms";
                agset (currentNode, (char*)"label",(char*)label.c_str());
                goto allsyncs;
            case task_type_create:
                agset (currentNode, (char*)"color",(char*)"forestgreen");
                label = "Create\nduration="+to_string(ct->duration/1000000)+"ms";
                agset (currentNode, (char*)"label",(char*)label.c_str());
                goto allsyncs;
            case task_type_join:
                agset (currentNode, (char*)"color",(char*)"firebrick1");
                label = "Join\nduration="+to_string(ct->duration/1000000)+"ms";
                agset (currentNode, (char*)"label",(char*)label.c_str());
                goto allsyncs;
            allsyncs:
                agset (currentNode, (char*)"shape", (char*)"box");
                agset (currentNode, (char*)"style", (char*)"filled");
                agset (currentNode, (char*)"width",(char*)".2");
                agset (currentNode, (char*)"height",(char*)".2");
                agset (currentNode, (char*)"fontcolor",(char*)"white");
                agset (currentNode, (char*)"fontsize",(char*)"12");
                break;

            default:
                break;
        }

        // Store the created node
        nodes[ct->taskId] = currentNode;

        delete ct;
    }

    // Create edges in the graph
    for (pair<TaskId, TaskId> edge : edgeSet)
    {
        try
        {
            Agnode_t* a = nodes.at(edge.first);
            Agnode_t* b = nodes.at(edge.second);
            
            string edgeName = edge.first.toString() + edge.second.toString();
            
            Agedge_t* graph_edge = agedge(g, a, b,(char*) edgeName.c_str(), createIfItDoesntExist);
            agset (graph_edge, (char*)"color", (char*)"black");
        }
        catch(...) { continue;}
    }
}


void depict_partial_graph(TaskGraph* tg, graph_t *g) {
    int createIfItDoesntExist = 1;
    Task* currentTask = NULL;

    set< pair<TaskId, TaskId> > edgeSet;
    map<TaskId, Agnode_t*> nodes;

    TaskGraphInfo* tgi = tg->getTaskGraphInfo();
    int loopCount = 0;
    
    while((loopCount < MAX_GRAPH_NODE_SIZE) && (currentTask = tg->readContechTask()))
    {
        // give a node its name
        auto bba = currentTask->getBasicBlockActions();
        uint64 startLine = 0;
        uint64 endLine = 0;
        string functionName = "";
        string fileName = "";
        uint64 memBytes = 0;
        auto f = bba.begin(), e = bba.end();
        while (f != e) {
            BasicBlockAction bb = *f;
            auto bbi = tgi->getBasicBlockInfo((uint)bb.basic_block_id);
            if (f == bba.begin()) {
                startLine = bbi.lineNumber;
                functionName = bbi.functionName;
                fileName = bbi.fileName;
            }
            for (MemoryAction mem : f.getMemoryActions())
            {
                if (mem.type == action_type_mem_read || mem.type == action_type_mem_write)
                    memBytes += (0x1 << mem.pow_size);
            }
            if ((++f) == e){
                endLine = bbi.lineNumber;
            }
        }
        if (startLine > endLine){
            swap(startLine, endLine);
        }

        string node_name = currentTask->getTaskId().toString() + "\n"+
        "Duration = "+ to_string((currentTask->getEndTime()-currentTask->getStartTime())/1000000) + " ms\n";

        if (bba.begin() != bba.end()) {
            node_name += "Memory:"+to_string(memBytes)+" bytes"+"\n"+
            "Lines:"+to_string(startLine)+"-"+to_string(endLine)+"\n"+
            "Func:"+functionName+"\n"+
            "File:"+fileName;
        }

        Agnode_t *currentNode = agnode(g, (char*) currentTask->getTaskId().toString().c_str(), createIfItDoesntExist);

        agset (currentNode, (char*)"label",(char*)node_name.c_str());

        for (TaskId childTaskId : currentTask->getSuccessorTasks())
        {
            edgeSet.insert(make_pair(currentTask->getTaskId(), childTaskId));
        }

        string label;

        // Colorize nodes based on event type
        switch(currentTask->getType()){
            case task_type_sync:
                agset (currentNode, (char*)"color",(char*)"darkorange");
                label = "Sync\n"+node_name;
                agset (currentNode, (char*)"label",(char*)label.c_str());
                goto allsyncs;
            case task_type_barrier:
                agset (currentNode, (char*)"color",(char*)"darkorange");
                label = "Barrier\n"+node_name;
                agset (currentNode, (char*)"label",(char*)label.c_str());
                goto allsyncs;
            case task_type_create:
                agset (currentNode, (char*)"color",(char*)"forestgreen");
                label = "Create\n"+node_name;
                agset (currentNode, (char*)"label",(char*)label.c_str());
                goto allsyncs;
            case task_type_join:
                agset (currentNode, (char*)"color",(char*)"firebrick1");
                label = "Join\n"+node_name;
                agset (currentNode, (char*)"label",(char*)label.c_str());
                goto allsyncs;
            allsyncs:
                agset (currentNode, (char*)"shape", (char*)"box");
                agset (currentNode, (char*)"style", (char*)"filled");
                agset (currentNode, (char*)"width",(char*)".2");
                agset (currentNode, (char*)"height",(char*)".2");
                agset (currentNode, (char*)"fontcolor",(char*)"white");
                agset (currentNode, (char*)"fontsize",(char*)"12");
                break;

            default:
                break;
        }

        // Store the created node
        nodes[currentTask->getTaskId()] = currentNode;

        delete currentTask;
        loopCount++;
    }

    // Create edges in the graph
    for (pair<TaskId, TaskId> edge : edgeSet)
    {
        try
        {
            Agnode_t* a = nodes.at(edge.first);
            Agnode_t* b = nodes.at(edge.second);
            
            string edgeName = edge.first.toString() + edge.second.toString();
            
            Agedge_t* graph_edge = agedge(g, a, b,(char*) edgeName.c_str(), createIfItDoesntExist);
            agset (graph_edge, (char*)"color", (char*)"black");
            agset (graph_edge, (char*)"label",(char*)edgeName.c_str());
        }
        catch(...) { continue;}
    }
}

/**
 * This program takes in a taskgraph file as defined by Task.h, reads it, and converts it 
 * to a graphical representation (PNG)
 */
int main(int argc, char const *argv[])
{
    //input check
    if(argc < 2 || argc > 3){
        cerr << "Usage: " << argv[0] << " taskGraphInputFile [--compress]" << endl;
        exit(1);
    }

    // Draw data arrows?
    bool compress = false;
    if(argc == 3)
    {
        if (!strcmp(argv[2], "--compress"))
        {
            cout << "Enabling compress..." << endl;
            compress = true;
        }
        else cerr << "Unrecognized parameter: " << argv[2] << endl;
    }

    //Create file handles for input and output
    FILE* taskGraphOut;
    if (compress) {
        taskGraphOut = fopen("EasyCompressedGraph.png","wb");
    } else {
        taskGraphOut = fopen("EasyGraph.png","wb");
    }
    FILE* taskGraphIn  = fopen(argv[1], "rb");
    if (taskGraphIn == NULL){
        cerr << "ERROR: Couldn't open input file" << endl;
        exit(1);
    }
    
    //Get graph context
    GVC_t *gvc = gvContext();
    //Create directed graph
    graph_t *g = agopen((char*)"g", Agdirected, NULL);
    //Create a default edge color
    agattr(g,AGEDGE,(char*)"color",(char*)"black");
    agattr(g,AGEDGE,(char*)"style",(char*)"solid");
    agattr(g,AGEDGE,(char*)"weight",(char*)"1");
    // Default node attributes
    agattr(g,AGNODE,(char*)"shape",(char*)"oval");
    agattr(g,AGNODE,(char*)"color",(char*)"black");
    agattr(g,AGNODE,(char*)"style",(char*)"solid");
    agattr(g,AGNODE,(char*)"fontcolor",(char*)"black");
    agattr(g,AGNODE,(char*)"width",(char*)".5");
    agattr(g,AGNODE,(char*)"height",(char*)".5");
    agattr(g,AGNODE,(char*)"fontsize",(char*)"16");


    //read Tasks and create graph
    TaskGraph* tg = TaskGraph::initFromFile(taskGraphIn);
    if (tg == NULL) {
        fprintf(stderr, "Failure to open task graph\n");
        return -1;
    }

    if (compress) {
        depict_compressed_graph(tg, g);
    } else {
        depict_partial_graph(tg, g);
    }


    printf("Graph Built. Nodes: %d\n",agnnodes(g));
    printf("Building graph image \n");
    //Generte graph using the dot layout and write to a png file
    gvLayout(gvc, g, "dot");
    gvRender(gvc, g, "png", taskGraphOut);


    //cleanup
    gvFreeLayout(gvc, g);
    agclose(g);
    gvFreeContext(gvc);
    fclose(taskGraphOut);
    fclose(taskGraphIn);
}
