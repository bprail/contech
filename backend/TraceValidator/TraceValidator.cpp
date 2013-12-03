#include "TraceValidator.hpp"
#include <cstdlib>

void checkContextId(unsigned int id)
{
    // Make sure we aren't overflowing statically allocated arrays
    if (id > 1024)
    {
        printf("Exceeded static maximum number of contechs\n");
        printf("Largest contech ID seen: %u\n", id);
        displayContechEventDebugInfo();
        exit(1);
    }
}

void TraceValidator::validate(ct_file* in)
{
    while (ct_event* event = createContechEvent(in))
    {
        checkContextId(event->contech_id);
        deleteContechEvent(event);
    }
}

