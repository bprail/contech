#include "markedCodeContainer.hpp"

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
    markedCodeContainer* container = markedCodeContainer::fromFile("/net/tinker/ehein6/contech/middle/output/barr.mo");

    uint bbId = 0;
    for (uint bbId = 0; bbId < 87; bbId++)
    {
        cout << endl << bbId << endl;
        markedBlock* bb = container->getBlock(bbId);
        if (bb == NULL) continue;

        for (markedInstruction inst : *bb)
        {
            cout << inst.toString() << endl;
        }

        // TODO Capture output and diff against input file

    }

}
