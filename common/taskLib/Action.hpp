#ifndef CT_ACTION_H
#define CT_ACTION_H

#include <inttypes.h>
#include <sstream>
#include <iostream>
namespace contech {

// The action class encodes all the different "actions" that a task can take.
// It is a 64 bit value. The first three bits determine what type of action it is.
struct Action; // forward reference

// Enum that decodes the first three bits of an action into an action type
enum action_type
{
    action_type_null = 0,
    action_type_mem_read = 1,
    action_type_mem_write = 2,
    action_type_free = 3,
    action_type_malloc = 4,
    action_type_size = 5,
    action_type_basicBlock = 6
    // If you add more action types, recheck the methods of Action to ensure no assumptions are broken
};

// Next, we define the layout for each type of action. It is critical that each one reserves three bits for the action type.

// Load/store or allocation
struct MemoryAction
{
    union {
        struct {
            uint64_t type : 3;
            uint64_t pow_size : 3; // the size of the read or write is 2^pow_size
            uint64_t addr : 58;
        };
        uint64_t data;
    };
    MemoryAction();
    MemoryAction(Action a);
};
bool operator< (const MemoryAction& lhs, const MemoryAction& rhs);


// Basic block
struct BasicBlockAction
{
    union {
        struct {
            uint64_t type : 3;
            uint64_t basic_block_id : 61;
        };
        uint64_t data;
    };
    BasicBlockAction();
    BasicBlockAction(Action a);
};
bool operator< (const BasicBlockAction& lhs, const BasicBlockAction& rhs);

// Finally, overlay all the different action structs into a single 64 bit layout
struct Action
{
    union {
        struct {
            uint64_t type : 3;
            uint64_t junk : 61;
        };
        // Raw bits
        uint64_t data;
    };

    Action();
    Action(BasicBlockAction a);
    Action(MemoryAction a);
    action_type getType() const;
    bool isMemOp() const;
    bool isMemoryAction() const;
    bool isBasicBlockAction() const;
    bool operator==(const Action& rhs) const;
    bool operator!=(const Action& rhs) const;
    std::string toString() const;
};

bool operator< (const Action& lhs, const Action& rhs);
inline static std::ostream& operator<<(std::ostream& os, const Action& i) { os << i.toString(); return os; }

} // end namespace contech

#endif
