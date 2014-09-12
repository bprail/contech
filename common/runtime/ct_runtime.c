#define _GNU_SOURCE
#ifndef __USE_GNU
#define __USE_GNU
#endif
#include "ct_runtime.h"
#include "rdtsc.h"
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/timeb.h>
#include <sys/sysinfo.h>
#include <sys/mman.h>
#include <assert.h>

#include <sched.h>


// Check for NULL on every instrumentation routine
//#define __NULL_CHECK

// Should position in buffer be changed - Yes
// Disable to test instrumentation overhead
#define POS_USED

// Include debugging checks / prints
//#define DEBUG

// Should the create events measure clock skew
//#define CT_CLOCK_SKEW

// Record overhead from instrumentation
//#define CT_OVERHEAD_TRACK

//
// initBuffer is a special static buffer, whenever a thread is being created or exiting,
// it stores events into this buffer.  The buffer may be assigned to multiple threads,
// which is fine as the events are outside the bounds of create / join.
//
ct_serial_buffer_sized initBuffer = {0, SERIAL_BUFFER_SIZE, 0, NULL, {0}};

__thread pct_serial_buffer __ctThreadLocalBuffer = (pct_serial_buffer)&initBuffer;
__thread unsigned int __ctThreadLocalNumber = 0; // no static
__thread pcontech_thread_info __ctThreadInfoList = NULL;
__thread pcontech_id_stack __ctParentIdStack = NULL;
__thread pcontech_id_stack __ctThreadIdStack = NULL;
__thread pcontech_join_stack __ctJoinStack = NULL;

#ifdef CT_OVERHEAD_TRACK
__thread ct_tsc_t __ctTotalThreadOverhead = 0;
__thread unsigned int __ctTotalThreadBuffersQueued = 0;
__thread ct_tsc_t __ctLastQueueBuffer = 0;
__thread ct_tsc_t __ctTotalTimeBetweenQueueBuffers = 0;
//__thread uint64_t __ctCurrentOverheadStart = 0;
#endif

unsigned long long __ctGlobalOrderNumber = 0;
unsigned int __ctThreadGlobalNumber = 0;
unsigned int __ctThreadExitNumber = 0;
unsigned int __ctMaxBuffers = -1;
unsigned int __ctCurrentBuffers = 0;
pct_serial_buffer __ctQueuedBuffers = NULL;
pct_serial_buffer __ctQueuedBufferTail = NULL;
pct_serial_buffer __ctFreeBuffers = NULL;
// Setting the size in a variable, so that future code can tune / change this value
const size_t serialBufferSize = (SERIAL_BUFFER_SIZE);

#ifdef DEBUG
pthread_mutex_t __ctPrintLock;
#endif

//
// Buffers are queued to a background thread that processes them
//   and then puts them onto the free list.
// When the queuedBuffers goes from NULL to non-NULL, then signal bufferSignal
//
pthread_mutex_t __ctQueueBufferLock;
pthread_cond_t __ctQueueSignal;
pthread_mutex_t __ctFreeBufferLock;
pthread_cond_t __ctFreeSignal;

pthread_mutex_t ctAllocLock;

void __ctStoreThreadJoinInternal(bool, unsigned int, ct_tsc_t);
unsigned int __ctStoreThreadJoinInternalPos(bool, unsigned int, unsigned int, ct_tsc_t);


// The wrapper functions exist so that the LLVM compiler
// pass can easily access ct_runtime's data without being tied to the internals.
unsigned int __ctGetLocalNumber()
{
    //printf("Requested %d\n", __ctThreadLocalNumber);
    return __ctThreadLocalNumber;
}

ct_tsc_t __ctGetCurrentTick()
{
    return rdtsc();
}

unsigned int __ctAllocateCTid()
{
    // Return old number and increment
    unsigned int r = __sync_fetch_and_add(&__ctThreadGlobalNumber, 1);
    return r;
}

void __ctAllocateLocalBuffer()
{
    pthread_mutex_lock(&__ctFreeBufferLock);
    if (__ctFreeBuffers != NULL)
    {
        __ctThreadLocalBuffer = __ctFreeBuffers;
        __ctFreeBuffers = __ctFreeBuffers->next;
        __ctCurrentBuffers++;
        pthread_mutex_unlock(&__ctFreeBufferLock);
        __ctThreadLocalBuffer->next = NULL;
        // Buffer from list, just set position
        __ctThreadLocalBuffer->pos = 0;
    }
    else
    {
        if (__ctCurrentBuffers == __ctMaxBuffers)
        {
            ct_tsc_t start = rdtsc();
            // Does this condition variable need an additional check?
            //   Or are we asserting that the delay is finished and
            //   __ctFreeBuffers is not NULL
            pthread_cond_wait(&__ctFreeSignal, &__ctFreeBufferLock);
            __ctThreadLocalBuffer = __ctFreeBuffers;
            __ctFreeBuffers = __ctFreeBuffers->next;
            __ctCurrentBuffers++;
            pthread_mutex_unlock(&__ctFreeBufferLock);
            __ctStoreDelay(start);
            __ctThreadLocalBuffer->next = NULL;
            // Buffer from list, just set position
            __ctThreadLocalBuffer->pos = 0;
        }
        else {
            __ctCurrentBuffers++;
            pthread_mutex_unlock(&__ctFreeBufferLock);
            __ctThreadLocalBuffer = (pct_serial_buffer) malloc(sizeof(ct_serial_buffer) + serialBufferSize);
            //__ctThreadLocalBuffer = ctInternalAllocateBuffer();
            if (__ctThreadLocalBuffer == NULL)
            {
                // This may be a bad thing, but we're already failing memory allocations
                pthread_exit(NULL);
            }
            //fprintf(stdout, "%p\n", __ctThreadLocalBuffer);
            
            // Buffer was malloc, so set the length
            __ctThreadLocalBuffer->pos = 0;
            __ctThreadLocalBuffer->length = serialBufferSize;
        }
    }

    __ctThreadLocalBuffer->next = NULL;
    __ctThreadLocalBuffer->id = __ctThreadLocalNumber;
    #ifdef DEBUG
    pthread_mutex_lock(&__ctPrintLock);
    fprintf(stderr, "a,%p,%d\n", __ctThreadLocalBuffer, __ctThreadLocalNumber);
    fflush(stderr);
    pthread_mutex_unlock(&__ctPrintLock);
    #endif
}

void __parsec_bench_begin(int t)
{
    //__ctQueueBuffer(false);
    //__ctThreadLocalBuffer = (pct_serial_buffer)&initBuffer;
    // Set threadlocal to initbuffer
    //   flag that create / join need to be recorded
}

 void __parsec_roi_begin()
 {
    //__ctAllocateLocalBuffer();
 }
 
 void __parsec_roi_end()
 {
    // set threadlocal to initbuffer
    //   flag that create / join need to be recorded
 }
 
 void __parsec_bench_end()
 {
    // all events now discarded
 }

void __ctCleanupThread(void* v)
{
    // A thread has exited
    // TODO: Verify whether the atomic add should be before the queue buffer
    #ifdef CT_OVERHEAD_TRACK
    ct_tsc_t start = rdtsc();
    #endif
    __sync_fetch_and_add(&__ctThreadExitNumber, 1);
    __ctStoreThreadJoinInternal(true, __ctThreadLocalNumber, rdtsc());
    __ctQueueBuffer(false);
    __ctThreadLocalBuffer = (pct_serial_buffer)&initBuffer;
    #ifdef CT_OVERHEAD_TRACK
    ct_tsc_t end = rdtsc();
    //__ctTotalThreadOverhead += (end - start);
    printf("T(%d), %lld, %lld, %d\n", __ctThreadLocalNumber, 
                                __ctTotalThreadOverhead, 
                                __ctTotalTimeBetweenQueueBuffers,
                                __ctTotalThreadBuffersQueued);
    #endif
}

int __ctThreadCreateActual(pthread_t * thread, const pthread_attr_t * attr,
        void * (*start_routine)(void *), void * arg)
{
    int ret;
    unsigned int child_ctid = 0;
    ct_tsc_t temp, start;
    pcontech_thread_create ptc;
    
    start = rdtsc();
    ptc = (pcontech_thread_create)malloc(sizeof(contech_thread_create));
    if (ptc == NULL) return EAGAIN;
    
    // Build the contech packet for __ctInitThread
    // __ctInitThread will invoke start_routine(arg)
    // Also pass the allocated ctid and the parent ctid
    ptc->func = start_routine;
    ptc->arg = arg;
    ptc->parent_ctid = __ctThreadLocalNumber;
    child_ctid = __ctAllocateCTid();
    ptc->child_ctid = child_ctid;
    ptc->child_skew = 0;
    ptc->parent_skew = 0;
    
    // Parent, store the create event before creating
    __ctStoreThreadCreate(child_ctid, 0, start);
    __ctQueueBuffer(true);
    
    ret = pthread_create(thread, attr, __ctInitThread, ptc);
    
    if (ret != 0) 
    {
        __sync_fetch_and_add(&__ctThreadExitNumber, 1);
        free(ptc);
        goto create_exit;
    }
 
    __ctAddThreadInfo(thread, child_ctid);
    
    //
    // Now compute the skew
    //
#ifdef CT_CLOCK_SKEW
    while (ptc->child_skew == 0) ;
    temp = ptc->child_skew - rdtsc();
    ptc->parent_skew = (temp)?temp:1;
#endif
    
create_exit: 
    return ret;
}

void* __ctInitThread(void* v)//pcontech_thread_create ptc
{
    void* (*f)(void*);
    void (*g)(void*);
    void* a;
    unsigned int p;
    long long skew;
    ct_tsc_t start;
    pcontech_thread_create ptc = (pcontech_thread_create) v;
    f = ptc->func;
    a = ptc->arg;
    p = ptc->parent_ctid;
    start = rdtsc();
    
    // HACK -- REMOVE!!!
    #if 0
    printf("Hello!\n"); fflush(stdout);
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(24 - (ptc->child_ctid % 16), &cpuset);
    int r = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    printf("%d - %d - %llx\n", ptc->child_ctid, r, cpuset);
    #endif
    
    //
    // Now compute the skew
    //
#ifdef CT_CLOCK_SKEW
    skew = 0;
    while (ptc->parent_skew == 0)
    {
        ptc->child_skew = rdtsc();
    }
    skew = ptc->parent_skew;
#else
    skew = 1;
#endif
    
    __ctThreadLocalNumber = ptc->child_ctid;//__sync_fetch_and_add(&__ctThreadGlobalNumber, 1);
    __ctAllocateLocalBuffer();
    
    __ctThreadInfoList = NULL;

    free(ptc);
    
    __ctStoreThreadCreate(p, skew, start);
 
    g = __ctCleanupThread;
    pthread_cleanup_push(g, NULL);
    
    #ifdef CT_OVERHEAD_TRACK
    {
        ct_tsc_t end = rdtsc();
        //__ctTotalThreadOverhead += (end - start);
    }
    #endif
    
    a = f(a);
    pthread_cleanup_pop(1);
    return a;
}

//
//  Put the current local buffer into the queue and allocate a new buffer
//
void __ctQueueBuffer(bool alloc)
{
    pct_serial_buffer localBuffer = NULL;
#ifdef CT_OVERHEAD_TRACK
    ct_tsc_t start, end;
    start = rdtsc();
#endif
    
#ifdef DEBUG
    pthread_mutex_lock(&__ctPrintLock);
    fprintf(stderr, "q,%p,%d\n", __ctThreadLocalBuffer, __ctThreadLocalNumber);
    fflush(stderr);
    pthread_mutex_unlock(&__ctPrintLock);
#endif

    assert(__ctThreadLocalBuffer->pos < SERIAL_BUFFER_SIZE);
    
    // If this thread is still using the init buffer, then discard the events
    if (__ctThreadLocalBuffer == (pct_serial_buffer)&initBuffer)
    {
        __ctThreadLocalBuffer->pos = 0;
        return;
    }
    
    // If we need to allocate a new buffer, and the current one is rather empty,
    //   then allocate one for the current, copy data and reuse the existing buffer
    if (alloc && 
        (__ctThreadLocalBuffer->pos < (__ctThreadLocalBuffer->length / 2)))
    {
        unsigned int allocSize = (__ctThreadLocalBuffer->pos + 0) & (~0);
        localBuffer = __ctThreadLocalBuffer;
        
        __ctThreadLocalBuffer = (pct_serial_buffer) malloc(sizeof(ct_serial_buffer) + allocSize);
        
        if(__ctThreadLocalBuffer != NULL)
        {
            __ctThreadLocalBuffer->pos = localBuffer->pos;
            __ctThreadLocalBuffer->length = allocSize;
            __ctThreadLocalBuffer->next = NULL;
            __ctThreadLocalBuffer->id = __ctThreadLocalNumber;
            
            memcpy(__ctThreadLocalBuffer->data, localBuffer->data, allocSize);
        }
        else
        {
            __ctThreadLocalBuffer = localBuffer;
            localBuffer = NULL;
        }
    }
    
    if (__ctThreadLocalBuffer->id != __ctThreadLocalNumber)
    {
        fprintf(stderr, "WARNING: Local Buffer has migrated from %d to %d since allocation\n",
                __ctThreadLocalBuffer->id, __ctThreadLocalNumber);
    }
    
#if 0
    // This check verifies that the first 16 bytes of a buffer are not 0s
    //   Such a failure would likely indicate the lack of a basic block (or other) event
    //   to start a buffer.  The check is disabled for runtime reasons.
    int s = 0;
    for (int i = 0; i < 16; i++)
        if (__ctThreadLocalBuffer->data[i] == 0) s++;
    assert(s < 16);
#endif
    
    //
    // Queue the thread local buffer to the back of the queue, tail pointer available
    //   Signal the background thread if the queue is empty
    //
    pthread_mutex_lock(&__ctQueueBufferLock);
    __builtin_prefetch(&__ctQueuedBufferTail->next, 1, 0);
    if (__ctQueuedBuffers == NULL)
    {
        __ctQueuedBuffers = __ctThreadLocalBuffer;
        __ctQueuedBufferTail = __ctThreadLocalBuffer;
        __ctThreadLocalBuffer = NULL;
        pthread_cond_signal(&__ctQueueSignal);
    }
    else
    {
        pct_serial_buffer t = __ctQueuedBuffers;
        __ctQueuedBufferTail->next = __ctThreadLocalBuffer;
        __ctQueuedBufferTail = __ctThreadLocalBuffer;
        __ctThreadLocalBuffer = NULL;
    }
    pthread_mutex_unlock(&__ctQueueBufferLock);

    //
    // Used a temporary to hold the thread local buffer, restore
    //
    if (localBuffer != NULL)
    {
        localBuffer->pos = 0;
        __ctThreadLocalBuffer = localBuffer;
    }
    //
    // If we need to allocate a new buffer do so now
    //
    else if (alloc)
    {
        __ctAllocateLocalBuffer();
    }
    
#ifdef CT_OVERHEAD_TRACK
    end = rdtsc();
    __ctTotalThreadOverhead += (end - start);
    __ctTotalThreadBuffersQueued ++;
    
    if (__ctLastQueueBuffer != 0)
    {
        __ctTotalTimeBetweenQueueBuffers += (start - __ctLastQueueBuffer);
    }
    __ctLastQueueBuffer = end;
#endif 
}


//
// __ctDebugLocalBuffer
//   This function is only invoked by the debugger to look at the contents
//     of the local buffer and investigate whether there is an issue
//
void __ctDebugLocalBuffer()
{
    int i;
    printf("Local buffer: %p\n", __ctThreadLocalBuffer);
    printf("Capacity: %d\tSize: %d\n", __ctThreadLocalBuffer->length, __ctThreadLocalBuffer->pos);
    printf("First Bytes:\n");
    for (i = 0; i < 32; i++)
    {
        printf("%x", __ctThreadLocalBuffer->data[i]);
    }
    printf("\nLast Bytes:\n");
    for (i = 0; i < 32; i++)
    {
        printf("%x", __ctThreadLocalBuffer->data[__ctThreadLocalBuffer->pos + i - 32]);
    }
    printf("\n");
}

void __ctCheckBufferBySize(unsigned int numOps)
{
    #ifdef POS_USED
    if ((SERIAL_BUFFER_SIZE - (numOps + 1)*6) < __ctThreadLocalBuffer->pos)
        __ctQueueBuffer(true);
    #endif
}

__attribute__((always_inline)) void __ctCheckBufferSize(unsigned int p)
{
    //#ifdef POS_USED
    // Contech LLVM pass knows this limit
    //   It will call check by size if the basic block needs more than 1K to store its data
    if ((SERIAL_BUFFER_SIZE - 1024) < p)
        __ctQueueBuffer(true);
    /* Adding a prefetch reduces the L1 D$ miss rate by 1 - 3%, but also increases overhead by 5 - 10%
    else // TODO: test with , 1 to indicate write prefetch
        __builtin_prefetch(((char*)__ctThreadLocalBuffer) + p + 1024);*/
   // #endif
}

void __ctCheckBufferSizeDebug(unsigned int bbid)
{
#ifdef DEBUG
    if ((__ctThreadLocalBuffer->length - __ctThreadLocalBuffer->pos) < 64)
    {
        pthread_mutex_lock(&__ctPrintLock);
        fprintf(stderr, "%u - %u\n", bbid, __ctThreadLocalBuffer->pos);
        fflush(stderr);
        pthread_mutex_unlock(&__ctPrintLock);
    }
#endif
}

void __ctStoreBasicBlockMark(unsigned int bbid)
{

}

void __ctStoreMemReadMark()
{

}

void __ctStoreMemWriteMark()
{

}

unsigned int __ctGetBufferPos()
{
    return __ctThreadLocalBuffer->pos;
}

void __ctSetBufferPos(unsigned int pos)
{
    __ctThreadLocalBuffer->pos = pos;
}

// (contech_id, basic block id, num of ops)
__attribute__((always_inline)) char* __ctStoreBasicBlock(unsigned int bbid)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    unsigned int p = __ctThreadLocalBuffer->pos;
    char* r = &__ctThreadLocalBuffer->data[p];
    
    __ctCheckBufferSizeDebug(bbid);
    
    // Shift 1 byte of 0s, which is the basic block event
    *((unsigned int*)r) = bbid << 8;
    
    return r;
}

__attribute__((always_inline)) unsigned int __ctStoreBasicBlockComplete(unsigned int numMemOps)
{
    #ifdef POS_USED
    // 6 bytes per memory op, unsigned int for id + event
    (__ctThreadLocalBuffer->pos += numMemOps * 6 * sizeof(char) + sizeof(unsigned int));
    #endif
    return __ctThreadLocalBuffer->pos;
}

__attribute__((always_inline)) void __ctStoreMemOp(void* addr, unsigned int c, char* r)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    
    // With a little endian machine, we write 8 bytes and then will overwrite the highest two
    //   bytes with the next write.  Thus we have the 6 bytes of interest in the buffer
    // void __builtin_ia32_movntq (di *, di)
    *((uint64_t*)(r + c * 6 * sizeof(char) + sizeof(unsigned int))) = (uint64_t)addr;
    //*((unsigned int*)(r + c * 6* sizeof(char)+sizeof(unsigned int))) = (uint32_t) (uint64_t)addr;
    // *((uint16_t*)(r + c * 6* sizeof(char) + 2*sizeof(unsigned int))) = (uint16_t) (((uint64_t)addr) >> 32);
}

void __ctStoreSync(void* addr, int syncType, int success, ct_tsc_t start_t)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    
    unsigned int p = __ctThreadLocalBuffer->pos;
    ct_tsc_t t = rdtsc();
    unsigned long long ordNum = __sync_fetch_and_add(&__ctGlobalOrderNumber, 1);
    
    // Unix 0 is successful
    //   So non zeros indicate the sync event did not happen
    if (success != 0) return;
    
    *((ct_event_id*)&__ctThreadLocalBuffer->data[p]) = ct_event_sync;
    //*((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = __ctThreadLocalNumber;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = start_t;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int) + sizeof(ct_tsc_t)]) = t;
    *((int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int) + sizeof(ct_tsc_t) * 2]) = syncType;
    *((ct_addr_t*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int) + sizeof(ct_tsc_t) * 2 + sizeof(int)]) = (ct_addr_t) addr;
    *((unsigned long long*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int) + sizeof(ct_tsc_t) * 2 + sizeof(ct_addr_t) + sizeof(int)]) = ordNum;
    #ifdef POS_USED
    __ctThreadLocalBuffer->pos = p + sizeof(unsigned int) + sizeof(ct_tsc_t) * 2 + sizeof(ct_addr_t)+ sizeof(int) + sizeof(unsigned long long);
    #endif
}

void __ctStoreThreadCreate(unsigned int ptc, long long skew, ct_tsc_t start)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    
    unsigned int p = __ctThreadLocalBuffer->pos;
    
    *((ct_event_id*)&__ctThreadLocalBuffer->data[p]) = ct_event_task_create /*<<24*/;
    //*((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = __ctThreadLocalNumber;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = start;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int) + sizeof(ct_tsc_t)]) = rdtsc();
    *((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int) + 2*sizeof(ct_tsc_t)]) = ptc;
    *((long long*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + 2*sizeof(ct_tsc_t)]) = skew;
    #ifdef POS_USED
    __ctThreadLocalBuffer->pos += 2 * sizeof(unsigned int) + 2*sizeof(ct_tsc_t) + sizeof(long long);
    #endif
}

void __ctStoreMemoryEvent(bool isAlloc, size_t size, void* a)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    
    unsigned int p = __ctThreadLocalBuffer->pos;
    uint64_t s = size;
    
    *((ct_event_id*)&__ctThreadLocalBuffer->data[p]) = ct_event_memory/*<<24*/;
    //*((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = __ctThreadLocalNumber;
    *((char*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = isAlloc;
    *((unsigned long long*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int) + sizeof(char)]) = s;
    *((ct_addr_t*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int) + sizeof(char) + sizeof(unsigned long long)]) = (ct_addr_t) a;
    #ifdef POS_USED
    __ctThreadLocalBuffer->pos += sizeof(unsigned int) + sizeof(ct_addr_t) + sizeof(unsigned long long) + sizeof(char);
    #endif
}

void __ctStoreBulkMemoryEvent(bool isWrite, size_t s, void* a)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    
    unsigned int p = __ctThreadLocalBuffer->pos;
    unsigned long long size = s;
    
    *((ct_event_id*)&__ctThreadLocalBuffer->data[p]) = ct_event_bulk_memory_op/*<<24*/;
    //*((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = __ctThreadLocalNumber;
    *((char*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = isWrite;
    *((unsigned long long*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int) + sizeof(char)]) = size;
    *((ct_addr_t*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int) + sizeof(char) + sizeof(unsigned long long)]) = (ct_addr_t) a;
    #ifdef POS_USED
    __ctThreadLocalBuffer->pos += sizeof(unsigned int) + sizeof(ct_addr_t) + sizeof(unsigned long long) + sizeof(char);
    #endif
}

void __ctStoreBarrier(bool enter, void* a, ct_tsc_t start)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    
    unsigned int p = __ctThreadLocalBuffer->pos;
    
    *((ct_event_id*)&__ctThreadLocalBuffer->data[p]) = ct_event_barrier/*<<24*/;
    //*((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = __ctThreadLocalNumber;
    *((char*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = enter;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)+ sizeof(char)]) = start;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)+ sizeof(char)+ sizeof(ct_tsc_t)]) = rdtsc();
    *((ct_addr_t*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int) + 2*sizeof(ct_tsc_t)+ sizeof(char)]) = (ct_addr_t) a;
    #ifdef POS_USED
    __ctThreadLocalBuffer->pos += sizeof(unsigned int) + 2*sizeof(ct_tsc_t) + sizeof(ct_addr_t) + sizeof(char);
    #endif
}

void __ctStoreThreadJoin(pthread_t pt, ct_tsc_t start)
{
    __ctStoreThreadJoinInternal(false, __ctLookupThreadInfo(pt), start);
}

void __ctStoreThreadJoinInternal(bool ie, unsigned int id, ct_tsc_t start)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    
    unsigned int p = __ctThreadLocalBuffer->pos;
    
    *((ct_event_id*)&__ctThreadLocalBuffer->data[p]) = ct_event_task_join/*<<24*/;
    //*((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = __ctThreadLocalNumber;
    *((char*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = ie;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)+ sizeof(char)]) = start;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)+ sizeof(char)+ sizeof(ct_tsc_t)]) = rdtsc();
    *((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int) + sizeof(char)+ 2*sizeof(ct_tsc_t)]) = id;
    #ifdef POS_USED
    __ctThreadLocalBuffer->pos += 2 * sizeof(unsigned int) + sizeof(bool)+ 2*sizeof(ct_tsc_t);
    #endif
}

void __ctStoreDelay(ct_tsc_t start_t)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    
    unsigned int p = __ctThreadLocalBuffer->pos;
    ct_tsc_t t = rdtsc();

    *((ct_event_id*)&__ctThreadLocalBuffer->data[p]) = ct_event_delay;
    //*((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = __ctThreadLocalNumber;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = start_t;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int) + sizeof(ct_tsc_t)]) = t;
    
    __ctThreadLocalBuffer->pos += sizeof(unsigned int) + sizeof(ct_tsc_t) * 2;
}

// Each thread maintains a map of pthread_t to ctid
// Insert this pair into the map
//   Currently the map is a linked list, as # of threads created by 1 thread stays low (<64)
void __ctAddThreadInfo(pthread_t *pt, unsigned int id)
{
    pcontech_thread_info t = (pcontech_thread_info) malloc(sizeof(contech_thread_info));
    if (t == NULL) return;
    
    t->pt_info = *pt;
    t->ctid = id;
    t->next = __ctThreadInfoList;
    __ctThreadInfoList = t;
}

// Lookup the pthread_t -> ctid entry and free it if found
unsigned int __ctLookupThreadInfo(pthread_t pt)
{
    pcontech_thread_info t, l;
    
    l = __ctThreadInfoList;
    
    if (l == NULL) return 0;
    if (pthread_equal(pt, (l->pt_info)))
    {
        unsigned int r = l->ctid;
        __ctThreadInfoList = l->next;
        free(l);
        return r;
    }
    
    t = l->next;
    while (t != NULL)
    {
        if (pthread_equal(pt, (t->pt_info)))
        {
            unsigned int r = t->ctid;
            l->next = t->next;
            free(t);
            return r;
        }
        t = t->next;
        l = l->next;
    }
    
    return 0;
}

// Create event for thread and parent
//   Puts event for parent ctid into a buffer
//   Allocates a new ctid for thread and assigns it
//   And thread ctid to thread ctid stack
void __ctOMPThreadCreate(unsigned int parent)
{
    unsigned int threadId = __ctAllocateCTid();

    // First close out any current buffer, if one exists
    if (__ctThreadLocalBuffer != NULL &&
        __ctThreadLocalBuffer != (pct_serial_buffer)&initBuffer)
    {
        __ctQueueBuffer(true);
    }
    else
    {
        __ctAllocateLocalBuffer();
    }
    
    // Pretend we are the parent id and queue a create event
    //   because there is the small copy path, this create
    //   will be copied out with the current id
    __ctThreadLocalNumber = parent;
    __ctThreadLocalBuffer->id = parent;
    __ctStoreThreadCreate(threadId, 0, rdtsc());
    __ctQueueBuffer(true);
    __ctThreadLocalNumber = threadId;
    __ctThreadLocalBuffer->id = threadId;
    
    __ctStoreThreadCreate(parent, 1, rdtsc());
    __ctPushIdStack(&__ctThreadIdStack, threadId);
}

// create event for thread and task
//   if int == 0, restore thread ctid from stack
//   else create events with task and thread ids
//   set local bool to return value (used with join)
void __ctOMPTaskCreate(int ret)
{
    unsigned int taskId;
    if (ret == 0)
    {
        __ctThreadLocalNumber = __ctPeekIdStack(&__ctThreadIdStack);
        pcontech_join_stack elem = __ctJoinStack;
        while (elem != NULL)
        {
            pcontech_join_stack t = elem;
            __ctStoreThreadJoinInternal(false, elem->id, elem->start);
            elem = elem->next;
            free(elem);
            __ctCheckBufferSize(__ctThreadLocalBuffer->pos);
        }
        __ctJoinStack = NULL;
        return;
    }
    taskId = __ctAllocateCTid();
    
    unsigned int threadId = __ctPeekIdStack(&__ctThreadIdStack);
    __ctThreadLocalNumber = threadId;
    __ctStoreThreadCreate(taskId, 0, rdtsc());
    __ctQueueBuffer(true);
    __ctThreadLocalBuffer->id = taskId;
    __ctThreadLocalNumber = taskId;
    
    __ctStoreThreadCreate(threadId, 1, rdtsc());
    
    return;
}

// join event for thread and task
//   if bool == true, then we are in task context
//   else ignore
void __ctOMPTaskJoin()
{
    // If the top of the stack is the current ID, then no tasks have been created
    if (__ctPeekIdStack(&__ctThreadIdStack) == __ctThreadLocalNumber) return;
    
    // We do this in reverse, so that threadId is local leaving this call
    unsigned int threadId = __ctPeekIdStack(&__ctThreadIdStack);
    __ctStoreThreadJoinInternal(true, threadId, rdtsc());
    __ctQueueBuffer(true);
    unsigned int taskId = __ctThreadLocalNumber;
    
    __sync_fetch_and_add(&__ctThreadExitNumber, 1);
    
    __ctThreadLocalNumber = threadId;
    __ctThreadLocalBuffer->id = threadId;
    
    // Joins are pushed onto a stack, so that
    //   All of the creates occur for the tasks before any joins of the tasks
    pcontech_join_stack elem = malloc(sizeof(contech_join_stack));
    if (elem == NULL)
    {
        fprintf(stderr, "Internal Contech allocation failure at %d\n", __LINE__);
        pthread_exit(NULL);
    }
    
    elem->id = taskId;
    elem->start = rdtsc();
    elem->next = __ctJoinStack;
    __ctJoinStack = elem;
}

// join event for parent and thread
//   queue and do not allocate new buffer for thread
void __ctOMPThreadJoin(unsigned int parent)
{
    // We do this in reverse, so that threadId is local leaving this call
    unsigned int threadId = __ctPopIdStack(&__ctThreadIdStack);
    
    if (threadId != __ctThreadLocalNumber)
    {
        __ctOMPTaskJoin();
    }
    
    pcontech_join_stack elem = __ctJoinStack;
    while (elem != NULL)
    {
        pcontech_join_stack t = elem;
        __ctStoreThreadJoinInternal(false, elem->id, elem->start);
        elem = elem->next;
        free(elem);
        __ctCheckBufferSize(__ctThreadLocalBuffer->pos);
    }
    __ctJoinStack = NULL;
    
    __ctStoreThreadJoinInternal(true, parent, rdtsc());
    __ctQueueBuffer(true);
    
    __sync_fetch_and_add(&__ctThreadExitNumber, 1);
    
    __ctThreadLocalNumber = parent;
    __ctThreadLocalBuffer->id = parent;
    __ctStoreThreadJoinInternal(false, threadId, rdtsc());
    __ctQueueBuffer(true);  // Yes, this time we will be wasting space
}

// Push current ctid onto parent stack
void __ctOMPPushParent()
{
    __ctPushIdStack(&__ctParentIdStack, __ctThreadLocalNumber);
}

// Pop current ctid off of parent stack
//   N.B. This assumes that the returning context is the same as the caller
void __ctOMPPopParent()
{
    __ctThreadLocalNumber = __ctPopIdStack(&__ctParentIdStack);
}

void __ctPushIdStack(pcontech_id_stack *head, unsigned int id)
{
    pcontech_id_stack elem = malloc(sizeof(contech_id_stack));
    if (elem == NULL)
    {
        fprintf(stderr, "Internal Contech allocation failure at %d\n", __LINE__);
        pthread_exit(NULL);
    }
    
    if (head == NULL) return;
    
    elem->id = id;
    elem->next = *head;
    *head = elem;
}

unsigned int __ctPopIdStack(pcontech_id_stack *head)
{
    if (head == NULL || *head == NULL) return 0;
    pcontech_id_stack elem = *head;
    unsigned int id = elem->id;
    *head = elem->next;
    free(elem);
    return id;
}

unsigned int __ctPeekIdStack(pcontech_id_stack *head)
{
    if (head == NULL || *head == NULL) return 0;
    pcontech_id_stack elem = *head;
    unsigned int id = elem->id;
    return id;
}
