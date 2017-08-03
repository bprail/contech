#include "graphviz/gvc.h"
#include "graphviz/cgraph.h"
#include "../../common/taskLib/TaskGraph.hpp"
#include "../../common/taskLib/ct_file.h"
#include <algorithm>
#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <iostream>
#include <fstream>
#include <sstream>

#include "BasicBlock.hpp"
#include "../CommTracker/CommTracker.hpp"

#define MAX_CONTECHS 1024

using namespace std;
using namespace contech;

char* getLabel( uint32_t id , uint64_t runCount);

int main(int argc, char const *argv[])
{
    //input check
    if(argc != 3){
        cout << "Usage: " << argv[0] << " taskGraphInputFile outFile" << endl;
        exit(1);
    }

    map<uint64_t, task_type> basicBlockType;

    // Count the number of times each basic block ran
    FILE* taskGraphIn  = fopen(argv[1], "rb");
    if(taskGraphIn == NULL){
        cerr << "Error: Couldn't open input file" << endl;
        exit(1);
    }
    map<uint64_t, uint32_t> basicBlockRunCount;
    uint64_t maxRunCount = 0;

    // Tracks edges that should be added to the graph
    set< pair<uint32_t, uint32_t> > edgeSet;

    // Tracks basic blocks that have been created
    map<uint32_t, BasicBlock*> basicBlocks;

    BasicBlock* lastBlock[MAX_CONTECHS];
    for (uint32_t i = 0; i < MAX_CONTECHS; i++) {lastBlock[i] = NULL;}

    // Process the task graph
    TaskGraph* tg = TaskGraph::initFromFile(taskGraphIn);
    if (tg == NULL) {}

    while(Task* currentTask = tg->readContechTask()){
        BasicBlock* bb;
        for (BasicBlockAction a : currentTask->getBasicBlockActions())
        {
            // Look up / create the basic block
            bb = basicBlocks[a.basic_block_id];
            if (bb == NULL)
            {
                bb = new BasicBlock(a.basic_block_id);
                basicBlocks[a.basic_block_id] = bb;
            }

            // Increment the run count for this block
            bb->incrementRunCount();

            // Keep track of the largest run count for any block
            maxRunCount = max(bb->getRunCount(), maxRunCount);

            // Make an edge from the last block to run in this contech to this one
            if (lastBlock[(uint32_t)currentTask->getContextId()] != NULL)
            {
                edgeSet.insert(make_pair(lastBlock[(uint32_t)currentTask->getContextId()]->getID(), (uint32_t)a.basic_block_id));
            }

            lastBlock[(uint32_t)currentTask->getContextId()] = bb;
        }

        //cout << "Processed " << currentTask->getBasicBlocks().size() << " basic blocks from task " << currentTask->getContextId() << ":" << currentTask->getSeqId() << endl;

        // Connect task create blocks to their children
        if (currentTask->getType() == task_type_create)
        {
            for (TaskId id : currentTask->getSuccessorTasks())
            {
                lastBlock[(uint32_t)id.getContextId()] = bb;
            }
        }

        // The event type of the task belongs to the last basic block to run in this contech
        if (currentTask->getType() != task_type_basic_blocks)
        {
            BasicBlock* lastBasicBlock = lastBlock[(uint32_t)currentTask->getContextId()];

            // If this block's type is unassigned, it will have type basic_block
            // If it has already been assigned, this assignment is redundant (it should match this task's type)
            // Else, the type is about to get clobbered and we should throw an error
            if (lastBasicBlock->getType() != task_type_basic_blocks && lastBasicBlock->getType() != currentTask->getType())
            {
                cout << endl << "ERROR:" << endl;
                cout << "Task " << currentTask->getTaskId() << " has type " << currentTask->getType() << endl;
                cout << "Tried to assign to basic block # " << lastBasicBlock->getID() << ", which already has type " << lastBasicBlock->getType() << endl;
            }
            else
            {
                //cout << "Task " << currentTask->getContextId() << ":" << currentTask->getSeqId() << " has type " << Task::taskTypeToString(currentTask->getType()) << "; Assigning to basic block # " << lastBasicBlock->getID() << endl;
                lastBasicBlock->setType(currentTask->getType());
            }
        }

        delete currentTask;
    }

    // Create a directed graph
    GVC_t *gvc = gvContext();
    graph_t *g = agopen((char*)"g", Agdirected, NULL);
    // Set default edge attributes
    agattr(g,AGEDGE,(char*)"color",(char*)"black");
    agattr(g,AGEDGE,(char*)"style",(char*)"solid");
    // Set default node attributes
    agattr(g,AGNODE,(char*)"shape",(char*)"box");
    agattr(g,AGNODE,(char*)"color",(char*)"white");
    agattr(g,AGNODE,(char*)"style",(char*)"filled");
    agattr(g,AGNODE,(char*)"fontcolor",(char*)"yellow");
    
    //Desigates to node and edge constructors to create new objects if no existing ones found
    //If this is 0 (false), the constructors strictly searches for existing objects with the same name
    int createIfItDoesntExist = 1;

    for (pair<uint32_t, BasicBlock*> it : basicBlocks)
    {
        BasicBlock* bb = it.second;

        // Create a node for the block
        bb->setNode(agnode(g, getLabel(bb->getID(), bb->getRunCount()), createIfItDoesntExist));

        // Colorize blocks by run count
        signed short shade = 255 * (1-(bb->getRunCount() / (double)maxRunCount));
        shade = shade > 50 ? shade - 50 : shade;
        char shadeString[8];
        sprintf(shadeString, "#%2x%2x%2x", shade, shade, shade);
        agset( bb->getNode(), (char*)"color", shadeString);

        // Colorize blocks if they have a special event type
        switch(bb->getType()){
            case task_type_sync:
            case task_type_barrier:
                agset (bb->getNode(),(char*)"color",(char*)"darkorange"); //orange
                agset (bb->getNode(),(char*)"style",(char*)"filled");
                agset (bb->getNode(),(char*)"fontcolor",(char*)"white");
                break;
            case task_type_create:
                agset (bb->getNode(),(char*)"color",(char*)"forestgreen"); //green
                agset (bb->getNode(),(char*)"style",(char*)"filled");
                agset (bb->getNode(),(char*)"fontcolor",(char*)"white");
                break;
            case task_type_join:
                agset (bb->getNode(),(char*)"color",(char*)"firebrick1"); //red
                agset (bb->getNode(),(char*)"style",(char*)"filled");
                agset (bb->getNode(),(char*)"fontcolor",(char*)"white");
            default:
                break;
        }
    }

    // Create edges
    for (pair<uint32_t, uint32_t> edge : edgeSet)
    {
        Agnode_t* a = basicBlocks[edge.first]->getNode();
        Agnode_t* b = basicBlocks[edge.second]->getNode();
        std::ostringstream oss; oss << edge.first << "->" << edge.second;
        string edgeName = oss.str();
        Agedge_t* graph_edge = agedge(g, a, b, (char*) edgeName.c_str(), createIfItDoesntExist);
        agset (graph_edge, (char*)"color", (char*)"black");
    }

    // Create communication edges
    //ct_rewind(taskGraphIn);
    
    CommTracker* tracker = CommTracker::fromGraph(tg);//CommTracker::fromFile(taskGraphIn);
    cout << "Recorded " << tracker->getRecords().size() << " instances of communication. " << endl;

    set< pair<uint32_t, uint32_t> > commEdgeSet;
    for (CommRecord r : tracker->getRecords())
    {
        commEdgeSet.insert(make_pair(r.srcBlock,r.dstBlock));
    }
    for (pair<uint32_t, uint32_t> edge : commEdgeSet)
    {
        // For some reason, blocks in the comm set are occasionally not found in the block list
        if (!basicBlocks.count(edge.first) || !basicBlocks.count(edge.second)) continue;

        Agnode_t* a = basicBlocks[edge.first]->getNode();
        Agnode_t* b = basicBlocks[edge.second]->getNode();
        std::ostringstream oss; oss << edge.first << "->" << edge.second;
        string edgeName = oss.str();
        Agedge_t* graph_edge = agedge(g, a, b, (char*) edgeName.c_str(), createIfItDoesntExist);
        agset (graph_edge, (char*)"color", (char*)"blue");
        agset (graph_edge, (char*)"style", (char*)"dashed");
    }
    delete tracker;
    delete tg;
    fclose(taskGraphIn);

    // Write out the graph
    FILE* cfgOut = fopen(argv[2],"wb");
    if (cfgOut == NULL) { cerr << "Error: Could not open output file." << endl; exit(1); }
    gvLayout(gvc, g, "dot");
    gvRender(gvc, g, "png", cfgOut);
    fclose(cfgOut);

    //cleanup
    gvFreeLayout(gvc, g);
    agclose(g);
    gvFreeContext(gvc);

    return 0;
}

/**
 * convert a TaskId to its string representation. Used for displaying node
 * names in the graph
 */
char* getLabel( uint32_t id, uint64_t runCount ) {
    std::ostringstream os;
    os << id << "\n" << runCount;
    return (char*)os.str().c_str();
}
