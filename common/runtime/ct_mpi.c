#include "ct_runtime.h"
#include <mpi.h>
#include <sched.h>

int __ctIsMPIPresent()
{
    return 1;
}

int __ctGetMPIRank()
{
    int rank = 0;
    int flag = 0;
    MPI_Initialized(&flag);
    while (flag == 0)
    {
        sched_yield();
        MPI_Initialized(&flag);
    }
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    return rank;
}

int __ctGetSizeofMPIDatatype(int datatype)
{
    int size = 0;
    MPI_Type_size(datatype, &size);
    return size;
}
