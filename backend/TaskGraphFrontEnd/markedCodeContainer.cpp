#include "markedCodeContainer.hpp"

markedBlock* markedCodeContainer::getBlock(unsigned int id) { return blocks[id]; };

markedCodeContainer* markedCodeContainer::fromFile(std::string inFile)
{
    markedCodeContainer* container = new markedCodeContainer();

    // Open the file
    std::ifstream in(inFile);

    // Format:
    // bbId
    // instruction
    // ...
    // instruction
    //
    // bbId
    // instruction
    // ...
    // instruction

    std::string line;
    while (std::getline(in, line))
    {
        std::istringstream stream(line);
        uint bbId;
        stream >> bbId;

        std::getline(in, line);
        markedBlock* b = new markedBlock();
        while (line != "")
        {
            b->instructions.push_back(markedInstruction::fromString(line));
            std::getline(in, line);
        }
        // for (auto i = b->begin(), e = b->end(); i != e; ++i)
        // {
            // std::cout << (*i).toString() << std::endl;
        // }
        container->blocks[bbId] = b;
    }

    in.close();
    return container;
}
