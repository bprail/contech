#include <inttypes.h>
#include <string>

// Interface class for an x86 instruction front-end
class IFrontEnd
{
public:

    // Represents an x86 instruction, along with memory addresses accessed
    struct Instruction
    {
        uint32_t addr;
        unsigned int length;
        unsigned int bytes[15];
        unsigned int reads;
        unsigned int writes;
        char opcode[16];
        uint32_t readAddr[2];
        uint32_t writeAddr[2];
    };

    // Cpu Id of the frontend
    virtual unsigned int getCpuId() = 0;
    // Gets the next instruction in the trace. Upon reaching the end of the trace, returns false and does not provide a valid instruction.
    virtual bool getNextInstruction(Instruction& inst) = 0;
    // Returns a string representation of the frontend's position in the trace, useful for debugging
    virtual std::string getPositionString() = 0;
    virtual void TaskGraphRegisterCallback(void (*param)(long)) = 0;
};

// Implemented in libTaskGraphFrontend
IFrontEnd* createTaskGraphFrontEnd(std::string basename, unsigned int cpuId);
