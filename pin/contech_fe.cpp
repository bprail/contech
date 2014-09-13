/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2013 Intel Corporation. All rights reserved.
 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
/*
 * Sample buffering tool
 * 
 * This tool collects an address trace of instructions that access memory
 * by filling a buffer.  When the buffer overflows,the callback writes all
 * of the collected records to a file.
 *
 */

#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <stddef.h>

#include <map>
#include <vector>

#include <sys/timeb.h>
#include <sys/sysinfo.h>

#include "pin.H"
#include "portability.H"
using namespace std;

/*
 * Name of the output file
 */
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "buffer.out", "output file");

/*
 * The ID of the buffer
 */
BUFFER_ID bufId;


REG scratch_reg0;

/*
 * Number of OS pages for the buffer
 */
#define NUM_BUF_PAGES 1024

struct BASICBLOCK
{
    UINT32      id;
};

struct MEMREF
{
    ADDRINT     ea;
    UINT32      size;
    BOOL        read;
};

struct MEMMGMT
{
    UINT32      type;
    UINT32      size;
};

struct TASK
{
    UINT32      type;
    UINT32      data;
};

enum event_type { ct_event_basic_block = 0, ct_event_memory_op, ct_event_memory, ct_event_task };

struct EVENT
{
    //UINT32      type;
    union
    {
        BASICBLOCK  bb;
        ADDRINT     mem;
        MEMMGMT     mgmt;
        TASK        task;
    };
};

// Compressed form for serialization
// Borrowed from contech
struct memOp {
  union {
    struct {
        uint64_t is_write : 1;
        uint64_t pow_size : 3; // the size of the op is 2^pow_size
        uint64_t addr : 58;
    };
    uint64_t data;
    char data8[8];
  };
};

UINT32 nextBBId = 0;
map<UINT32, vector<MEMREF> > blockMap;

struct ThreadBufferWrapper {
    VOID* buf;
    THREADID tid;
    UINT64 elemCount;
    ThreadBufferWrapper* next;
};

PIN_MUTEX QueueBufferLock;
PIN_MUTEX ThreadIDLock;
PIN_SEMAPHORE BufferQueueSignal;
ThreadBufferWrapper* BufferHead = NULL; 
ThreadBufferWrapper* BufferTail = NULL;
UINT32 runningThreadCount = 0;
UINT32 exitThreadCount = 0;
UINT32 ticketNumber = 0;

struct timeb starttp;
struct timeb endtp;

static void InternalThreadMain(void* v);

#if defined(TARGET_MAC)
#define MALLOC "_malloc"
#define FREE "_free"
#else
#define MALLOC "malloc"
#define FREE "free"
#define SYNC "pthread_mutex_lock"
#define CREATE "pthread_create"
#define JOIN "pthread_join"
#endif

/**************************************************************************
 *
 *  Instrumentation routines
 *
 **************************************************************************/

/*
 * Insert code to write data to a thread-specific buffer for instructions
 * that access memory.
 */
VOID Trace(TRACE trace, VOID *v)
{
    for(BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl=BBL_Next(bbl))
    {
        INS iPoint = BBL_InsHead(bbl);
        vector<MEMREF> bbMemOps;
        UINT32 bbId = ~0x0;
        for(INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins=INS_Next(ins))
        {
            string fnName = RTN_FindNameByAddress(BBL_Address(bbl));
            if (fnName == MALLOC) {continue;}
            if (fnName == FREE) {continue;}
            
            // Insert BBL buffer fill before first instruction
            if (ins == BBL_InsHead(bbl))
            {
                iPoint = ins;
                // BBL_Address is not unique
                //bbId = (UINT32) BBL_Address(bbl);
                bbId = nextBBId++;
                
                map<UINT32, vector<MEMREF> >::iterator it = blockMap.find(bbId);
                if (it != blockMap.end())
                {
                    // bbId is already a basic block
                    do {
                        bbId += 0x0100000000;
                        cerr << "BBID now " << bbId << endl;
                        return;
                    } while ((it = blockMap.find(bbId)) != blockMap.end());
                }
                
                INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                     //IARG_UINT32, ct_event_basic_block, offsetof(EVENT, type),
                                     IARG_UINT32, bbId, offsetof(EVENT, bb.id),
                                     IARG_END);

            
            }
            UINT32 memoryOperands = INS_MemoryOperandCount(ins);

            for (UINT32 memOp = 0; memOp < memoryOperands; memOp++)
            {
                UINT32 refSize = INS_MemoryOperandSize(ins, memOp);
                MEMREF currMemOp;
                //
                // Add record for bbId that it has memoryOperands, each of the following properties
                //
                
                // Note that if the operand is both read and written we log it once
                // for each.
                if (INS_MemoryOperandIsRead(ins, memOp))
                {
                    INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                         //IARG_UINT32, ct_event_memory_op, offsetof(EVENT, type),
                                         IARG_MEMORYOP_EA, memOp, offsetof(EVENT, mem),
                                         //IARG_UINT32, refSize, offsetof(EVENT, mem.size),
                                         //IARG_BOOL, TRUE, offsetof(EVENT, mem.read),
                                         IARG_END);
                    currMemOp.size = refSize;
                    currMemOp.read = TRUE;
                }

                if (INS_MemoryOperandIsWritten(ins, memOp))
                {
                    INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                         //IARG_UINT32, ct_event_memory_op, offsetof(EVENT, type),
                                         IARG_MEMORYOP_EA, memOp, offsetof(EVENT, mem), //mem.ea
                                         //IARG_UINT32, refSize, offsetof(EVENT, mem.size),
                                         //IARG_BOOL, FALSE, offsetof(EVENT, mem.read),
                                         IARG_END);
                    currMemOp.size = refSize;
                    currMemOp.read = FALSE;
                }
                
                bbMemOps.push_back(currMemOp);
            }
        }
        
        if (bbId != (~ (UINT32)0x0))
            blockMap[bbId] = bbMemOps;
    }
}

VOID MallocBefore(CHAR * name, ADDRINT size)
{
    
}

VOID MallocAfter(ADDRINT ret)
{
    
}

UINT32 GetTicketNumber()
{
    return __sync_fetch_and_add( &ticketNumber, 1);
}

VOID Image(IMG img, VOID *v)
{
    // Instrument the malloc() and free() functions.  Print the input argument
    // of each malloc() or free(), and the return value of malloc().
    //
    //  Find the malloc() function.
    RTN mallocRtn = RTN_FindByName(img, MALLOC);
    if (RTN_Valid(mallocRtn))
    {
        RTN_Open(mallocRtn);
        
        INS ins = RTN_InsHeadOnly(mallocRtn);
        INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                     IARG_UINT32, ct_event_memory, offsetof(EVENT, mgmt.type),
                                     IARG_FUNCARG_ENTRYPOINT_VALUE, 0, offsetof(EVENT, mgmt.size),
                                     IARG_END);

        // Instrument malloc() to print the input argument value and the return value.
        RTN_InsertCall(mallocRtn, IPOINT_BEFORE, (AFUNPTR)MallocBefore,
                       IARG_ADDRINT, MALLOC,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                       IARG_END);
        RTN_InsertCall(mallocRtn, IPOINT_AFTER, (AFUNPTR)MallocAfter,
                       IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);

        RTN_Close(mallocRtn);
    }

    // Find the free() function.
    RTN freeRtn = RTN_FindByName(img, FREE);
    if (RTN_Valid(freeRtn))
    {
        RTN_Open(freeRtn);
        // Instrument free() to print the input argument value.
        
        INS ins = RTN_InsHeadOnly(freeRtn);
        INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                     IARG_UINT32, ct_event_memory, offsetof(EVENT, mgmt.type),
                                     IARG_FUNCARG_ENTRYPOINT_VALUE, 0, offsetof(EVENT, mgmt.size), // hmm
                                     IARG_END);
        
        RTN_InsertCall(freeRtn, IPOINT_BEFORE, (AFUNPTR)MallocBefore,
                       IARG_ADDRINT, FREE,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                       IARG_END);
        RTN_InsertCall(freeRtn, IPOINT_AFTER, (AFUNPTR)MallocAfter,
                       IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);
        RTN_Close(freeRtn);
    }
    
    RTN syncRtn = RTN_FindByName(img, SYNC);
    if (RTN_Valid(syncRtn))
    {
        RTN_Open(syncRtn);
        
        INS ins = RTN_InsHeadOnly(syncRtn);
        INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                     IARG_UINT32, ct_event_task, offsetof(EVENT, task.type),
                                     IARG_THREAD_ID, offsetof(EVENT, task.data), 
                                     IARG_END);
                                     
        //scratch_reg0
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)GetTicketNumber, 
                                    IARG_RETURN_REGS, scratch_reg0,
                                    IARG_END);
        
        // Overload an event with additional data on the sync
        INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                     IARG_REG_VALUE, scratch_reg0, offsetof(EVENT, task.type),
                                     IARG_TSC, offsetof(EVENT, mgmt.size), 
                                     IARG_END);
        
        RTN_Close(syncRtn);
    }
    
    RTN createRtn = RTN_FindByName(img, CREATE);
    if (RTN_Valid(createRtn))
    {
        RTN_Open(createRtn);
        
        INS ins = RTN_InsHeadOnly(createRtn);
        INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                     IARG_UINT32, ct_event_task, offsetof(EVENT, task.type),
                                     IARG_THREAD_ID, offsetof(EVENT, task.data), 
                                     IARG_END);
        
        INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                     IARG_UINT32, ct_event_task, offsetof(EVENT, task.type),
                                     IARG_TSC, offsetof(EVENT, mgmt.size), 
                                     IARG_END);
        
        RTN_Close(createRtn);
    }
    
    RTN joinRtn = RTN_FindByName(img, JOIN);
    if (RTN_Valid(joinRtn))
    {
        RTN_Open(joinRtn);
        
        INS ins = RTN_InsHeadOnly(joinRtn);
        INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                     IARG_UINT32, ct_event_task, offsetof(EVENT, task.type),
                                     IARG_THREAD_ID, offsetof(EVENT, task.data), 
                                     IARG_END);
        
        INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                     IARG_UINT32, ct_event_task, offsetof(EVENT, task.type),
                                     IARG_TSC, offsetof(EVENT, mgmt.size), 
                                     IARG_END);
        
        RTN_Close(joinRtn);
    }
}

/**************************************************************************
 *
 *  Callback Routines
 *
 **************************************************************************/

/*!
 * Called when a buffer fills up, or the thread exits, so we can process it or pass it off
 * as we see fit.
 * @param[in] id		buffer handle
 * @param[in] tid		id of owning thread
 * @param[in] ctxt		application context
 * @param[in] buf		actual pointer to buffer
 * @param[in] numElements	number of records
 * @param[in] v			callback value
 * @return  A pointer to the buffer to resume filling.
 */
VOID * BufferFull(BUFFER_ID id, THREADID tid, const CONTEXT *ctxt, VOID *buf, UINT64 numElements, VOID *v)
{
    VOID* r = NULL;
    ThreadBufferWrapper* tbw = (ThreadBufferWrapper*) malloc(sizeof(ThreadBufferWrapper));
    tbw->tid = tid;
    tbw->elemCount = numElements;
    tbw->buf = buf;
    tbw->next = NULL;
    
//    cerr << "Thread " << tid << ": Dumping " << numElements << " memops" << endl;
    PIN_MutexLock(&QueueBufferLock);
    //cerr << buf << ", " << numElements << endl;
    if (BufferHead == NULL)
    {
        BufferHead = tbw;
        BufferTail = tbw;
        PIN_SemaphoreSet(&BufferQueueSignal);
    }
    else
    {
        BufferTail->next = tbw;
        BufferTail = tbw;
        //cerr << "Head next - " << BufferHead->next->buf << endl;
    }
    PIN_MutexUnlock(&QueueBufferLock);
    do {
        r = PIN_AllocateBuffer(bufId);
    } while (r == NULL);
    return r;
}


VOID ThreadStart(THREADID tid, CONTEXT *ctxt, INT32 flags, VOID *v)
{
    PIN_MutexLock(&ThreadIDLock);
    runningThreadCount++;
    PIN_MutexUnlock(&ThreadIDLock);
}


VOID ThreadFini(THREADID tid, const CONTEXT *ctxt, INT32 code, VOID *v)
{
    struct timeb tp;
    
    {
        ftime(&tp);
        //cerr << tp.time << "." << tp.millitm << "\n";
        LOG("PIN_END: " + decstr((unsigned int)tp.time) + "." + decstr(tp.millitm) + "\n");
    }

    PIN_MutexLock(&ThreadIDLock);
    exitThreadCount++;
    endtp = tp; // memcpy, implicitly
    PIN_MutexUnlock(&ThreadIDLock);
}


/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    cerr << "Experimental Pin Frontend for the Contech framework." << endl;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

void InternalThreadMain(void* v) {
    char* fname = getenv("CONTECH_FE_FILE");
    FILE* ofile;
    UINT64 totalBytes = 0;
    UINT32 rdBytes = 1024;
    
    if (fname == NULL)
    {
        ofile = fopen("/tmp/contech.trace", "w");
    }
    else
    {
        ofile = fopen(fname, "w");
    }
    
    if ( ! ofile )
    {
        cerr << "Error: could not open output file." << endl;
        exit(1);
    }

    // ThreadIDLock is carried over in the while condition
    PIN_MutexLock(&ThreadIDLock);
    do {
        PIN_MutexUnlock(&ThreadIDLock);
        
        // While there are no queued buffers, wait
        PIN_MutexLock(&QueueBufferLock);
        while (BufferHead == NULL)
        {
            PIN_MutexUnlock(&QueueBufferLock);
            PIN_SemaphoreWait(&BufferQueueSignal);
            PIN_MutexLock(&QueueBufferLock);
        }
        
        // tbw is the wrapper for the current head of the queue
        ThreadBufferWrapper* tbw = BufferHead;
        
        PIN_MutexUnlock(&QueueBufferLock);
        while (tbw != NULL)
        {
            UINT32 chksum = 0;
            UINT32* b = (UINT32*) tbw->buf;
            UINT32 numBytes = sizeof(EVENT) * tbw->elemCount ;
            totalBytes += (UINT64) numBytes;
            
            //cerr << ", " << tbw->buf << ", " << tbw->elemCount << "\n";
            
            // WRITE DATA TO FILE HERE
            //   Even computing a checksum results in a buffer backlog and termination
            if (numBytes > rdBytes) numBytes = rdBytes;
            for (UINT32 i = 0; i < numBytes; i += 4)
            {
                chksum = chksum ^ *b;
                b++;
            }
            rdBytes = rdBytes >> 1;
            
            fwrite(&chksum, sizeof(chksum), 1, ofile);
            
            // We could use the more complex Contech buffer management, which
            //   maintains a list of pre-allocated buffers;
            //   However, the savings should be minimal
            //   Contech uses ~5% time to manage buffers, since buffer creation is
            //     much higher than deletion, Deallocate probably models this more
            //     accurately when there is no delay writing data to disk
            PIN_DeallocateBuffer(bufId, tbw->buf);
            
            PIN_MutexLock(&QueueBufferLock);
            ThreadBufferWrapper* tNext = tbw->next;
            BufferHead = tNext;
            if (BufferHead == NULL) {BufferTail = NULL;}
            PIN_MutexUnlock(&QueueBufferLock);
            free(tbw);
            tbw = tNext;
        }
        
        PIN_MutexLock(&ThreadIDLock);
        
        // Contech switches exit() to pthread_exit()
        //   This may result in a Contech instrumented program executing longer than pin
    } while (runningThreadCount != exitThreadCount); // Test for threads exiting
    
    {
        
        cerr << "CT_END: " << endtp.time << "." << endtp.millitm << "\n";
        
        if (endtp.millitm < starttp.millitm)
        {
            endtp.millitm += 1000;
            endtp.time -= 1;
        }
        cerr << "CT_DELTA: " << (endtp.time - starttp.time) << "." << (endtp.millitm - starttp.millitm) << "\n";
        cerr << "CT_DATA: " << totalBytes << "\n";
    }
    
    {
        struct timeb tp;
        ftime(&tp);
        cerr << "CT_FLUSH: " << tp.time << "." << tp.millitm << "\n";
    }
    fclose(ofile);
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */
/*!
 * The main procedure of the tool.
 * This function is called when the application image is loaded but not yet started.
 * @param[in]   argc            total number of elements in the argv array
 * @param[in]   argv            array of command line arguments, 
 *                              including pin -t <toolname> -- ...
 */
int main(int argc, char *argv[])
{
    // Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid
    if( PIN_Init(argc,argv) )
    {
        return Usage();
    }
    
    // Initialize the memory reference buffer;
    // set up the callback to process the buffer.
    //
    bufId = PIN_DefineTraceBuffer(sizeof(EVENT), NUM_BUF_PAGES, BufferFull, 0);

    if(bufId == BUFFER_ID_INVALID)
    {
        cerr << "Error: could not allocate initial buffer" << endl;
        return 1;
    }
   
    // add an instrumentation function
    TRACE_AddInstrumentFunction(Trace, 0);
    //IMG_AddInstrumentFunction(Image, 0);

    // add callbacks
    PIN_AddThreadStartFunction(ThreadStart, 0);
    PIN_AddThreadFiniFunction(ThreadFini, 0);

    PIN_MutexInit(&QueueBufferLock);
    PIN_MutexInit(&ThreadIDLock);
    PIN_SemaphoreInit(&BufferQueueSignal);
    if (PIN_SpawnInternalThread(InternalThreadMain, NULL, 0, NULL) == INVALID_THREADID) {
        fprintf(stderr, "TOOL: <%d> Unable to spawn internal thread. Killing the test!\n", PIN_GetTid());
        fflush(stderr);
        PIN_ExitProcess(101);
    }
    
    {
        struct timeb tp;
        
        ftime(&tp);
        starttp = tp;
        cerr << "CT_START: " << tp.time << "." << tp.millitm << "\n";
    }
    
    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}


