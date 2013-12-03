#include "IFrontEnd.hpp"
#include <iostream>
#include <map>

using namespace std;

// Tests
void test();

int main(int argc, char* argv[])
{
    test();

    return 0;
}


void test()
{
    std::map<std::string,int> programMap;
    
    for (unsigned int coreId = 0; coreId < 1; coreId++)
    {
        std::cout << "Constructing frontend object for core " << coreId << std::endl;
        IFrontEnd* frontend = createTaskGraphFrontEnd("/net/tinker/dcook31/contech/middle/output/helloWorld",coreId);
        IFrontEnd::Instruction inst;
        
        
        
        while (frontend->getNextInstruction(inst))
        {
            programMap[string(inst.opcode)] = programMap[string(inst.opcode)] + 1;
            printf("Instruction: %s Count: %d\n",inst.opcode,programMap[string(inst.opcode)]);
            
            /*
            cout << frontend.getPositionString() << endl;

            cout << inst.toString();
            if (inst.reads == 1)
            {
                cout << "R [" << hex << inst.readAddr[0] << "]";
            }
            else if (inst.writes == 1)
            {
                cout << "W [" << hex << inst.writeAddr[0] << "]";
            }
            cout << endl;
            */
        }
    }
}
