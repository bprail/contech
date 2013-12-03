#include "TraceValidator.hpp"

int main(int argc, char* argv[])
{
    // Open input file
    // Use command line argument or stdin
    ct_file* in;
    if (argc > 1)
    {
        in = create_ct_file_r(argv[1]);
    }
    else
    {
        in = create_ct_file_from_handle(stdin);
    }

    // TraceValidator will crash with output 1 if the trace is invalid
    TraceValidator::validate(in);
    return 0;
}
