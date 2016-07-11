#include "gvc.h"
#include "cgraph.h"
#include "../../common/taskLib/Task.hpp"
#include "../CommTracker/CommTracker.hpp"
#include <iostream>
#include <map>
#include <set>
#include <sstream>

using namespace contech;

#define MAX_GRAPH_NODE_SIZE 1000

/**
 * This program takes in a taskgraph file as defined by Task.h, reads it, and converts it 
 * to a graphical representation (PNG)
 */
int main(int argc, char const *argv[])
{
    //input check
    if(argc < 2 || argc > 3){
        cerr << "Usage: " << argv[0] << " taskGraphInputFile [--enableDataArrows]" << endl;
        exit(1);
    }

    // Draw data arrows?
    bool dataArrowsEnabled = false;
    if(argc == 3)
    {
        if (!strcmp(argv[2], "--enableDataArrows"))
        {
            cerr << "Enabling data arrows..." << endl;
             dataArrowsEnabled = true;
        }
        else cerr << "Unrecognized parameter: " << argv[2] << endl;
    }

    //Create file handles for input and output
    FILE* taskGraphOut = fopen("taskGraph.png","wb");
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

    //Desigates to node and edge constructors to create new objects if no existing ones found
    //If this is 0 (false), the constructors strictly searches for existing objects with the same name
    int createIfItDoesntExist = 1;
    
    if (!dataArrowsEnabled)
    {

        // Tracks nodes in the graph by uniqueid
        map<TaskId, Agnode_t*> nodes;

        // Tracks edges that should be added to the graph
        set< pair<TaskId, TaskId> > edgeSet;

        //read Tasks and create graph
        TaskGraph* tg = TaskGraph::initFromFile(taskGraphIn);
        Task* currentTask = NULL;
        if (tg == NULL) {}

        int loopCount = 0;
        while((loopCount < MAX_GRAPH_NODE_SIZE) && (currentTask = tg->readContechTask()))
        {
            


            // Debug
            /*
            cout << "UID: " << currentTask->getTaskId(.toString()) <<
                        " ChildUID: " << currentTask->getChildTaskId(.toString()) <<
                        " ContinuationUID: " << currentTask->getContinuationTaskId(.toString()) <<
                        " EventType: " << Task::taskTypeToString(currentTask->getType()) << endl;
            */
            // Create edges

            Agnode_t *currentNode = agnode(g, (char*) currentTask->getTaskId().toString().c_str(), createIfItDoesntExist);
            for (TaskId childTaskId : currentTask->getSuccessorTasks())
            {
                edgeSet.insert(make_pair(currentTask->getTaskId(), childTaskId));
            }


            // Colorize nodes based on event type
            switch(currentTask->getType()){
                case task_type_sync:
                    agset (currentNode, (char*)"color",(char*)"darkorange");
                    agset (currentNode, (char*)"label",(char*)"S");
                    goto allsyncs;
                case task_type_barrier:
                    agset (currentNode, (char*)"color",(char*)"darkorange");
                    agset (currentNode, (char*)"label",(char*)"B");
                    goto allsyncs;
                case task_type_create:
                    agset (currentNode, (char*)"color",(char*)"forestgreen");
                    agset (currentNode, (char*)"label",(char*)"C");
                    goto allsyncs;
                case task_type_join:
                    agset (currentNode, (char*)"color",(char*)"firebrick1");
                    agset (currentNode, (char*)"label",(char*)"J");
                    goto allsyncs;
                allsyncs:
                    agset (currentNode, (char*)"shape", (char*)"box");
                    agset (currentNode, (char*)"style", (char*)"filled");
                    agset (currentNode, (char*)"width",(char*)".1");
                    agset (currentNode, (char*)"height",(char*)".1");
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
            }
            catch(...) { continue;}
        }
    }

    else if (dataArrowsEnabled)
    {
        // Create additional edges for communication
        close_ct_file(taskGraphIn);
        taskGraphIn = create_ct_file_r(argv[1]);
        CommTracker* tracker = CommTracker::fromFile(taskGraphIn);
        set< pair<TaskId, TaskId> > commEdgeSet;
        for (CommRecord r : tracker->getRecords())
        {
            commEdgeSet.insert(make_pair(r.sender, r.receiver));
        }
        for (pair<TaskId, TaskId> edge : commEdgeSet)
        {
            Agnode_t* a = agnode(g, (char*) edge.first.toString().c_str(),createIfItDoesntExist);
            Agnode_t* b = agnode(g, (char*) edge.second.toString().c_str(),createIfItDoesntExist);
            
            string edgeName = edge.first.toString() + edge.second.toString();
            
            Agedge_t* graph_edge = agedge(g, a, b, (char*)edgeName.c_str(), createIfItDoesntExist);
            agset (graph_edge, (char*)"style", (char*)"dashed");
            agset (graph_edge, (char*)"color", (char*)"blue");
            agset (graph_edge, (char*)"weight", (char*)"0");
        }
        delete tracker;
    }

    printf("Done reading taskgraph file \n");
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
