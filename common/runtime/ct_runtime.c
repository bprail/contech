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

typedef struct _ct_serial_buffer_sized
{
    unsigned int pos, length, id;
    struct _ct_serial_buffer* next; // can order buffers 
    char data[SERIAL_BUFFER_SIZE];
} ct_serial_buffer_sized;

//
// initBuffer is a special static buffer, whenever a thread is being created or exiting,
// it stores events into this buffer.  The buffer may be assigned to multiple threads,
// which is fine as the events are outside the bounds of create / join.
//
static ct_serial_buffer_sized initBuffer = {0, SERIAL_BUFFER_SIZE, 0, NULL, {0}};

__thread pct_serial_buffer __ctThreadLocalBuffer = (pct_serial_buffer)&initBuffer;
__thread unsigned int __ctThreadLocalNumber = 0; // no static
__thread pcontech_thread_info __ctThreadInfoList = NULL;
__thread pcontech_id_stack __ctParentIdStack = NULL;
__thread pcontech_id_stack __ctThreadIdStack = NULL;
__thread pcontech_join_stack __ctJoinStack = NULL;

static unsigned long long __ctGlobalOrderNumber = 0;
static unsigned int __ctThreadGlobalNumber = 0;
static unsigned int __ctThreadExitNumber = 0;
static unsigned int __ctMaxBuffers = -1;
static unsigned int __ctCurrentBuffers = 0;
static pct_serial_buffer __ctQueuedBuffers = NULL;
static pct_serial_buffer __ctQueuedBufferTail = NULL;
static pct_serial_buffer __ctFreeBuffers = NULL;
// Setting the size in a variable, so that future code can tune / change this value
const static size_t serialBufferSize = (SERIAL_BUFFER_SIZE);

extern uint8_t _binary_contech_bin_start[];// asm("_binary_contech_bin_start");
extern uint8_t _binary_contech_bin_size[];// asm("_binary_contech_bin_size");
extern uint8_t _binary_contech_bin_end[];//  asm("_binary_contech_bin_end");

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

void* (__ctBackgroundThreadWriter)(void*);
void __ctStoreThreadJoinInternal(bool, unsigned int, ct_tsc_t);
unsigned int __ctStoreThreadJoinInternalPos(bool, unsigned int, unsigned int, ct_tsc_t);

extern int ct_orig_main(int, char**);

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

void __ctCleanupThreadMain(void* v)
{
    char* d = NULL;
    
    // No guarantee that all threads have exited at this time
    //  However, main is exiting, so normal program is "ending"
    {
        struct timeb tp;
        ftime(&tp);
        printf("CT_END: %d.%03d\n", (unsigned int)tp.time, tp.millitm);
    }
    
    // Record that this thread has exited
    __ctStoreThreadJoinInternal(true, __ctThreadLocalNumber, rdtsc());
    // Queue the buffer
    __ctQueueBuffer(false);
    // Increment the exit count
    __sync_fetch_and_add(&__ctThreadExitNumber, 1);
#if DEBUG
    printf("%d =?= %d\n", __ctThreadGlobalNumber, __ctThreadExitNumber);
#endif
    // Wait on background thread
    pthread_join((pthread_t) v, (void**)&d);
}

#ifdef CT_MAIN
int main(int argc, char** argv)
{
    int r;
    pthread_t pt_temp;
    char* d = NULL;

    if (__ctThreadGlobalNumber == 0)
    {
        char* flimit = getenv("CONTECH_FE_LIMIT");
        if (flimit != NULL)
        {
            int ilimit = atoi(flimit);
        
            __ctMaxBuffers = ((unsigned long long)ilimit  * 1024 * 1024) / ((unsigned long long) SERIAL_BUFFER_SIZE);
        }
        else
        {
            struct sysinfo t_info;
            
            if (0 == sysinfo(&t_info))
            {
                unsigned long long mem_size = (unsigned long long)t_info.freeram * (unsigned long long)t_info.mem_unit;
                mem_size = (mem_size * 9) / 10;
                printf("CT_MEM: %llu\n", mem_size);
                __ctMaxBuffers = (mem_size) / ((unsigned long long) SERIAL_BUFFER_SIZE);
            }
        }
        
        pthread_mutex_init(&__ctQueueBufferLock, NULL);
        pthread_cond_init(&__ctQueueSignal, NULL);
        pthread_mutex_init(&__ctFreeBufferLock, NULL);
        pthread_cond_init(&__ctFreeSignal, NULL);
        pthread_mutex_init(&ctAllocLock, NULL);
#ifdef DEBUG        
        pthread_mutex_init(&__ctPrintLock, NULL);
#endif

        // Set aside 0 for main thread
        __ctThreadLocalNumber = __sync_fetch_and_add(&__ctThreadGlobalNumber, 1);
        
        // Now create the background thread writer
        if (0 != pthread_create(&pt_temp, NULL, __ctBackgroundThreadWriter, NULL))
        {
            exit(1);
        }
    }
    
    __ctThreadInfoList = NULL;
    __ctThreadLocalBuffer = NULL;
    
    // Allocate a real CT buffer for the main thread, this replaces initBuffer
    __ctAllocateLocalBuffer();
    // __ctThreadLocalBuffer->pos = 0;
    // __ctThreadLocalBuffer->length = serialBufferSize;

    {
        struct timeb tp;
        ftime(&tp);
        printf("CT_START: %d.%03d\n", (unsigned int)tp.time, tp.millitm);
    }
    
    // Invoke main, protected by pthread_cleanup handlers, so that main can exit cleanly with
    // its background thread
    __ctStoreThreadCreate(0, 0, rdtsc());
    pthread_cleanup_push(__ctCleanupThreadMain, (void*)pt_temp);
    r = ct_orig_main(argc, argv);
    pthread_cleanup_pop(1);
    
    // In rare cases, after we exit, we still have instrumentation being called
    // Record those events in the static buffer and let them languish.
    __ctThreadLocalBuffer = (pct_serial_buffer)&initBuffer;
    
    return r;
}
#endif

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

void __ctCleanupThread(void* v)
{
    // A thread has exited
    // TODO: Verify whether the atomic add should be before the queue buffer
    __sync_fetch_and_add(&__ctThreadExitNumber, 1);
    __ctStoreThreadJoinInternal(true, __ctThreadLocalNumber, rdtsc());
    __ctQueueBuffer(false);
    __ctThreadLocalBuffer = (pct_serial_buffer)&initBuffer;
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
    
    //__ctThreadLocalBuffer->pos = 0;
    //__ctThreadLocalBuffer->length = SERIAL_BUFFER_SIZE;
    __ctThreadInfoList = NULL;

    __ctStoreThreadCreate(p, skew, start);

    free(ptc);
    
    g = __ctCleanupThread;
    pthread_cleanup_push(g, NULL);
    
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
    
#ifdef DEBUG
    pthread_mutex_lock(&__ctPrintLock);
    fprintf(stderr, "q,%p,%d\n", __ctThreadLocalBuffer, __ctThreadLocalNumber);
    fflush(stderr);
    pthread_mutex_unlock(&__ctPrintLock);
#endif
    
    // __ctThreadExitNumber == __ctThreadGlobalNumber &&
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

    if (localBuffer != NULL)
    {
        localBuffer->pos = 0;
        __ctThreadLocalBuffer = localBuffer;
    }
    else if (alloc)
    {
        __ctAllocateLocalBuffer();
        //__ctThreadLocalBuffer->pos = 0;
        //__ctThreadLocalBuffer->length = SERIAL_BUFFER_SIZE;
    }
}

#define EVENT_COMPRESS 0
void* __ctBackgroundThreadWriter(void* d)
{
#if EVENT_COMPRESS
    gzFile serialFileComp;
#else
    FILE* serialFile;
#endif
    char* fname = getenv("CONTECH_FE_FILE");
    unsigned int wpos = 0;
    size_t totalWritten = 0;
    pct_serial_buffer memLimitQueue = NULL;
    pct_serial_buffer memLimitQueueTail = NULL;
    unsigned long long totalLimitTime = 0, startLimitTime, endLimitTime;
    
    if (fname == NULL)
    {
#if EVENT_COMPRESS
        FILE* tempFileHandle = fopen( "/tmp/contech_fe", "wb");
        serialFileComp = gzdopen (fileno(tempFileHandle), "wb");
#else
        serialFile = fopen( "/tmp/contech_fe", "wb");
#endif
    }
    else
    {
#if EVENT_COMPRESS
        FILE* tempFileHandle = fopen( fname, "wb");
        serialFileComp = gzdopen (fileno(tempFileHandle), "wb");
#else
        serialFile = fopen( fname, "wb");
#endif
    }

#if EVENT_COMPRESS
    if (serialFileComp == NULL)
#else
    if (serialFile == NULL)
#endif
    {
        fprintf(stderr, "Failure to open front-end stream for writing.\n");
        if (fname == NULL) { fprintf(stderr, "\tCONTECH_FE_FILE unspecified\n");}
        else {fprintf(stderr, "\tAttempted on %s\n", fname);}
        exit(-1);
    }
    
    {
        unsigned int id = 0;
        ct_event_id ty = ct_event_version;
        unsigned int version = CONTECH_EVENT_VERSION;
        uint8_t* bb_info = _binary_contech_bin_start;
        
#if EVENT_COMPRESS
        gzwrite(serialFileComp, &id, sizeof(unsigned int));
        gzwrite(serialFileComp, &ty, sizeof(unsigned int));
        gzwrite(serialFileComp, &version, sizeof(unsigned int));
        gzwrite(serialFileComp, bb_info, sizeof(unsigned int));
#else
        fwrite(&id, sizeof(unsigned int), 1, serialFile); 
        fwrite(&ty, sizeof(unsigned int), 1, serialFile);
        fwrite(&version, sizeof(unsigned int), 1, serialFile);
        fwrite(bb_info, sizeof(unsigned int), 1, serialFile);
#endif
        totalWritten += 4 * sizeof(unsigned int);
        
        bb_info += 4; // skip the basic block count
        while (bb_info != _binary_contech_bin_end)
        {
            // id,len, memop_0, ... memop_len-1
            unsigned int bb_len = *(unsigned int*) (bb_info + 4);
            char evTy = ct_event_basic_block_info;
            size_t byteToWrite = sizeof(unsigned int) * 2 + sizeof(char) * (2 * bb_len), tl = 0;
            //fprintf(stderr, "Write bb_info %p - %d %d\n", bb_info, *(unsigned int*)bb_info, bb_len);
            //fflush(stderr);
#if EVENT_COMPRESS
            gzwrite(serialFileComp, &evTy, sizeof(char));
            gzwrite(serialFileComp, bb_info, byteToWrite);
#else
            while (1 != fwrite(&evTy, sizeof(char), 1, serialFile));
            do {
                size_t wl = fwrite(bb_info + tl, sizeof(char), byteToWrite - tl, serialFile);
                if (wl > 0)
                    tl += wl;
            } while (tl < byteToWrite);
#endif
            bb_info += byteToWrite;
            totalWritten += byteToWrite + sizeof(char);
        }
    }
    
    pthread_mutex_lock(&__ctQueueBufferLock);
    do {
        while (__ctQueuedBuffers == NULL)
        {
            pthread_cond_wait(&__ctQueueSignal, &__ctQueueBufferLock);
        }
        pthread_mutex_unlock(&__ctQueueBufferLock);
    
        
        while (__ctQueuedBuffers != NULL)
        {
            // Write buffer to file
            size_t tl = 0;
            size_t wl = 0;
            pct_serial_buffer qb = __ctQueuedBuffers;
            
            // First craft the marker event that indicates a new buffer in the event list
            //   This event tells eventLib which contech created the next set of bytes
            {
                unsigned int buf[3];
                buf[0] = ct_event_buffer;
                buf[1] = __ctQueuedBuffers->id;
                buf[2] = __ctQueuedBuffers->pos;
                //fprintf(stderr, "%d, %llx, %d\n", __ctQueuedBuffers->id, totalWritten, __ctQueuedBuffers->pos);
#if EVENT_COMPRESS
                gzwrite (serialFileComp, &buf, sizeof(unsigned int));
                gzwrite (serialFileComp, &__ctQueuedBuffers->id, sizeof(unsigned int));
                gzwrite (serialFileComp, &__ctQueuedBuffers->pos, sizeof(unsigned int));
#else
                do
                {
                    wl = fwrite(&buf + tl, sizeof(unsigned int), 3 - tl, serialFile);
                    //if (wl > 0)
                    // wl is 0 on error, so it is safe to still add
                    tl += wl;
                } while  (tl < 3);
#endif
                totalWritten += 3 * sizeof(unsigned int);
                
            }
            
            // TODO: fully integrate into debug framework
            #if DEBUG
            if (totalWritten < 256)
            {
                int i;
                for (i = 0; i < 256; i ++)
                {
                    fprintf(stderr, "%x ", __ctQueuedBuffers->data[i]);
                }
            }
            #endif
            
            // Now write the bytes out of the buffer, until all have been written
            tl = 0;
            wl = 0;
            while (tl < __ctQueuedBuffers->pos)
            {
                if (qb->pos > SERIAL_BUFFER_SIZE)
                {
                    fprintf(stderr, "Illegal buffer size\n");
                }
#if EVENT_COMPRESS
                wl = gzwrite (serialFileComp, __ctQueuedBuffers->data + tl, (__ctQueuedBuffers->pos) - tl);
#else
                wl = fwrite(__ctQueuedBuffers->data + tl, 
                            sizeof(char), 
                            (__ctQueuedBuffers->pos) - tl, 
                            serialFile);
#endif
                // if (wl < 0)
                // {
                    // continue;
                // }
                tl += wl;
                if (qb != __ctQueuedBuffers)
                {
                    fprintf(stderr, "Tampering with __ctQueuedBuffers!\n");
                }
            }
            if (tl != __ctQueuedBuffers->pos)
            {
                fprintf(stderr, "Write quantity(%lu) is not bytes in buffer(%d)\n", tl, __ctQueuedBuffers->pos);
            }
            totalWritten += tl;
            
            // "Free" buffer
            // First move the queue pointer, as we implicitly held the first element
            // Then switch locks and put this processed buffer onto the free list
            pthread_mutex_lock(&__ctQueueBufferLock);
            {
                pct_serial_buffer t = __ctQueuedBuffers;
                __ctQueuedBuffers = __ctQueuedBuffers->next;
                if (__ctQueuedBuffers == NULL) __ctQueuedBufferTail = NULL;
                pthread_mutex_unlock(&__ctQueueBufferLock);
                
                if (t->length < SERIAL_BUFFER_SIZE)
                {
                    free(t);
                    // After the unlock is end of while loop, 
                    //  so as the buffer was free() rather than put on the list
                    //  we continue
                    continue;
                }
                
                // Switching locks, "t" is now only held locally
                pthread_mutex_lock(&__ctFreeBufferLock);
#ifdef DEBUG
                pthread_mutex_lock(&__ctPrintLock);
                fprintf(stderr, "f,%p,%d\n", t, t->id);
                fflush(stderr);
                pthread_mutex_unlock(&__ctPrintLock);
#endif
                
                
                // If this is the only free buffer, signal any waiting threads
                // TODO: Can we avoid the cond_signal if buffer limits are not in place?
                // TODO: OR not signal until queuedBuffers is NULL ?
                //if (t->next == NULL) {pthread_cond_signal(&__ctFreeSignal);}
                if (__ctCurrentBuffers == __ctMaxBuffers)
                {
                    if (__ctQueuedBuffers == NULL)
                    {
                        // memlimit end
                        struct timeb tp;
                        ftime(&tp);
                        endLimitTime = tp.time*1000 + tp.millitm;
                        totalLimitTime += (endLimitTime - startLimitTime);
                        memLimitQueueTail->next = __ctFreeBuffers;
                        __ctFreeBuffers = memLimitQueue;
                        __ctCurrentBuffers = 0;
                        memLimitQueue = NULL;
                        memLimitQueueTail = NULL;
                        pthread_cond_broadcast(&__ctFreeSignal);
                    }
                    else
                    {
                        if (memLimitQueueTail == NULL)
                        {
                            memLimitQueue = t;
                            memLimitQueueTail = t;
                            t->next = NULL;
                            // memlimit start
                            struct timeb tp;
                            ftime(&tp);
                            startLimitTime = tp.time*1000 + tp.millitm;
                        }
                        else
                        {
                            memLimitQueueTail->next = t;
                            t->next = NULL;
                            memLimitQueueTail = t;
                        }
                    }
                }
                else
                {
                    t->next = __ctFreeBuffers;
                    __ctFreeBuffers = t;
                    __ctCurrentBuffers --;
                }
            }
            pthread_mutex_unlock(&__ctFreeBufferLock);
        }
        
        // Exit condition is # of threads exited = # of threads
        // N.B. Main is part of this count
        pthread_mutex_lock(&__ctQueueBufferLock);
        if (__ctThreadExitNumber == __ctThreadGlobalNumber && 
            __ctQueuedBuffers == NULL) 
        { 
            // destroy mutex, cond variable
            // TODO: free freedBuffers
            {
                struct timeb tp;
                ftime(&tp);
                printf("CT_COMP: %d.%03d\n", (unsigned int)tp.time, tp.millitm);
                printf("CT_LIMIT: %llu.%03llu\n", totalLimitTime / 1000, totalLimitTime % 1000);
            }
            printf("Total Uncomp Written: %ld\n", totalWritten);
            fflush(stdout);
#if EVENT_COMPRESS
            gzflush (serialFileComp, Z_FULL_FLUSH);
            gzclose(serialFileComp);
#else
            fflush(serialFile);
            fclose(serialFile);
#endif
            pthread_mutex_unlock(&__ctQueueBufferLock);
            pthread_exit(NULL);            
        }
    } while (1);
}

unsigned int __ctCheckBufferSizePos(unsigned int pos)
{
    #ifdef POS_USED
    
    __ctThreadLocalBuffer->pos = pos;
    if ((__ctThreadLocalBuffer->length - pos) < 4096)
    {
        __ctThreadLocalBuffer->pos = pos;
        __ctQueueBuffer(true);
        return __ctThreadLocalBuffer->pos;
    }
    #endif
    return pos;
}

void __ctCheckBufferSize(unsigned int p)
{
    #ifdef POS_USED
    // TODO: Set contech pass to match this limit, memops < (X - 64) / 8
    //   4 for basic block, 32 for other event, then 6 for each memop
    if ((SERIAL_BUFFER_SIZE - p) < 1024)
        __ctQueueBuffer(true);
    #endif
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
    
    *((unsigned int*)r) = bbid << 8;
    
    return r;
}

unsigned int __ctStoreBasicBlockPos(unsigned int bbid, unsigned int num_ops, unsigned int pos)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    unsigned int p = pos;
    //__ctCheckBufferSize();
    //__ctCheckBufferSizeDebug(bbid);
    
    //*((unsigned int*)&__ctThreadLocalBuffer->data[p]) = __ctThreadLocalNumber;
    //*((ct_event_id*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = ct_event_basic_block;
    *((unsigned int*)&__ctThreadLocalBuffer->data[p]) = bbid << 8;
    //*((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = num_ops;
    #ifdef POS_USED
    return p + 1 * sizeof(unsigned int);
    #else
    return 0;
    #endif
}

unsigned int __ctStoreBasicBlockComplete(unsigned int c)
{
    #ifdef POS_USED
    unsigned int p = __ctThreadLocalBuffer->pos;
    p += c * 6 * sizeof(char) + sizeof(unsigned int);// sizeof(ct_memory_op);
    __ctThreadLocalBuffer->pos = p;
    return p;
    #endif
}

unsigned int __ctStoreBasicBlockCompletePos(unsigned int c, unsigned int pos)
{
    #ifdef POS_USED
    return pos + c * 6 * sizeof(char);// sizeof(ct_memory_op);
    #else
    return 0;
    #endif
}

void __ctStoreMemOpInternalPos(ct_memory_op cmo, unsigned int c, unsigned int pos)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    *((pct_memory_op)&__ctThreadLocalBuffer->data[pos + c * sizeof(ct_memory_op)]) = cmo;
    //__ctThreadLocalBuffer->pos += sizeof(ct_memory_op);
}

void __ctStoreMemOpInternal(ct_memory_op cmo, unsigned int c)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    *((pct_memory_op)&__ctThreadLocalBuffer->data[__ctThreadLocalBuffer->pos + c * sizeof(ct_memory_op)]) = cmo;
    //__ctThreadLocalBuffer->pos += sizeof(ct_memory_op);
}

void __ctStoreMemOpPos(bool iw, char size, void* addr, unsigned int c, unsigned int pos)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    
    *((unsigned int*)&__ctThreadLocalBuffer->data[__ctThreadLocalBuffer->pos + c * 6* sizeof(char)]) = (uint32_t) (uint64_t)addr;
    *((uint16_t*)&__ctThreadLocalBuffer->data[__ctThreadLocalBuffer->pos + c * 6* sizeof(char) + sizeof(unsigned int)]) = (uint16_t) (((uint64_t)addr) >> 32);
    
    /*ct_memory_op t;
    t.is_write = iw;
    t.pow_size = size;
    t.addr = (unsigned long long) addr;
    __ctStoreMemOpInternalPos(t, c, pos);*/
}

__attribute__((always_inline)) void __ctStoreMemOp(void* addr, unsigned int c, char* r)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    
    // With a little endian machine, we write 8 bytes and then will overwrite the highest two
    //   bytes with the next write.  Thus we have the 6 bytes of interest in the buffer
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
    if (success != 0) return;
    
    *((ct_event_id*)&__ctThreadLocalBuffer->data[p]) = ct_event_sync /*<<24*/;
    *((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = __ctThreadLocalNumber;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int)]) = start_t;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + sizeof(ct_tsc_t)]) = t;
    *((int*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + sizeof(ct_tsc_t) * 2]) = syncType;
    *((ct_addr_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + sizeof(ct_tsc_t) * 2 + sizeof(int)]) = (ct_addr_t) addr;
    *((unsigned long long*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + sizeof(ct_tsc_t) * 2 + sizeof(ct_addr_t) + sizeof(int)]) = ordNum;
    #ifdef POS_USED
    __ctThreadLocalBuffer->pos = p + 2 * sizeof(unsigned int) + sizeof(ct_tsc_t) * 2 + sizeof(ct_addr_t)+ sizeof(int) + sizeof(unsigned long long);
    #endif
}

unsigned int __ctStoreSyncPos(void* addr, int syncType, int success, ct_tsc_t start_t, unsigned int pos)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    
    unsigned int p = pos;
    ct_tsc_t t = rdtsc();
    unsigned long long ordNum = __sync_fetch_and_add(&__ctGlobalOrderNumber, 1);
    
    // Unix 0 is successful
    if (success != 0) return p;
    
    *((ct_event_id*)&__ctThreadLocalBuffer->data[p]) = ct_event_sync /*<<24*/;
    *((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = __ctThreadLocalNumber;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int)]) = start_t;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + sizeof(ct_tsc_t)]) = t;
    *((int*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + sizeof(ct_tsc_t) * 2]) = syncType;
    *((ct_addr_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + sizeof(ct_tsc_t) * 2 + sizeof(int)]) = (ct_addr_t) addr;
    *((unsigned long long*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + sizeof(ct_tsc_t) * 2 + sizeof(ct_addr_t) + sizeof(int)]) = ordNum;
    #ifdef POS_USED
    return p + 2 * sizeof(unsigned int) + sizeof(ct_tsc_t) * 2 + sizeof(ct_addr_t)+ sizeof(int) + sizeof(unsigned long long);
    #else
    return 0;
    #endif
}

void __ctStoreThreadCreate(unsigned int ptc, long long skew, ct_tsc_t start)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    
    unsigned int p = __ctThreadLocalBuffer->pos;
    
    *((ct_event_id*)&__ctThreadLocalBuffer->data[p]) = ct_event_task_create /*<<24*/;
    *((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = __ctThreadLocalNumber;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int)]) = start;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + sizeof(ct_tsc_t)]) = rdtsc();
    *((unsigned int*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + 2*sizeof(ct_tsc_t)]) = ptc;
    *((long long*)&__ctThreadLocalBuffer->data[p + 3 * sizeof(unsigned int) + 2*sizeof(ct_tsc_t)]) = skew;
    #ifdef POS_USED
    __ctThreadLocalBuffer->pos += 3 * sizeof(unsigned int) + 2*sizeof(ct_tsc_t) + sizeof(long long);
    #endif
}

unsigned int __ctStoreThreadCreatePos(unsigned int ptc, long long skew, unsigned int pos, ct_tsc_t start)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    
    unsigned int p = pos;
    
    *((ct_event_id*)&__ctThreadLocalBuffer->data[p]) = ct_event_task_create /*<<24*/;
    *((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = __ctThreadLocalNumber;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int)]) = start;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + sizeof(ct_tsc_t)]) = rdtsc();
    *((unsigned int*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + 2*sizeof(ct_tsc_t)]) = ptc;
    *((long long*)&__ctThreadLocalBuffer->data[p + 3 * sizeof(unsigned int) + 2*sizeof(ct_tsc_t)]) = skew;
    #ifdef POS_USED
    return pos + 3 * sizeof(unsigned int) + sizeof(ct_tsc_t) + sizeof(long long);
    #else
    return 0;
    #endif
}

void __ctStoreMemoryEvent(bool isAlloc, unsigned long long size, void* a)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    
    unsigned int p = __ctThreadLocalBuffer->pos;
    
    *((ct_event_id*)&__ctThreadLocalBuffer->data[p]) = ct_event_memory/*<<24*/;
    *((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = __ctThreadLocalNumber;
    *((char*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int)]) = isAlloc;
    *((unsigned long long*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + sizeof(char)]) = size;
    *((ct_addr_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + sizeof(char) + sizeof(unsigned long long)]) = (ct_addr_t) a;
    #ifdef POS_USED
    __ctThreadLocalBuffer->pos += 2 * sizeof(unsigned int) + sizeof(ct_addr_t) + sizeof(unsigned long long) + sizeof(char);
    #endif
}

unsigned int __ctStoreMemoryEventPos(bool isAlloc, unsigned long long size, void* a, unsigned int pos)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    
    unsigned int p = pos;
    
    *((ct_event_id*)&__ctThreadLocalBuffer->data[p]) = ct_event_memory/*<<24*/;
    *((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = __ctThreadLocalNumber;
    *((char*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int)]) = isAlloc;
    *((unsigned long long*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + sizeof(char)]) = size;
    *((ct_addr_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + sizeof(char) + sizeof(unsigned long long)]) = (ct_addr_t) a;
    #ifdef POS_USED
    return pos + 2 * sizeof(unsigned int) + sizeof(ct_addr_t) + sizeof(unsigned long long) + sizeof(char);
    #else
    return 0;
    #endif
}

void __ctStoreBulkMemoryEvent(bool isWrite, unsigned long long size, void* a)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    
    unsigned int p = __ctThreadLocalBuffer->pos;
    
    *((ct_event_id*)&__ctThreadLocalBuffer->data[p]) = ct_event_bulk_memory_op/*<<24*/;
    *((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = __ctThreadLocalNumber;
    *((char*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int)]) = isWrite;
    *((unsigned long long*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + sizeof(char)]) = size;
    *((ct_addr_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + sizeof(char) + sizeof(unsigned long long)]) = (ct_addr_t) a;
    #ifdef POS_USED
    __ctThreadLocalBuffer->pos += 2 * sizeof(unsigned int) + sizeof(ct_addr_t) + sizeof(unsigned long long) + sizeof(char);
    #endif
}

unsigned int __ctStoreBulkMemoryEventPos(bool isWrite, unsigned long long size, void* a, unsigned int pos)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    
    unsigned int p = pos;
    
    *((ct_event_id*)&__ctThreadLocalBuffer->data[p]) = ct_event_bulk_memory_op/*<<24*/;
    *((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = __ctThreadLocalNumber;
    *((char*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int)]) = isWrite;
    *((unsigned long long*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + sizeof(char)]) = size;
    *((ct_addr_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + sizeof(char) + sizeof(unsigned long long)]) = (ct_addr_t) a;
    #ifdef POS_USED
    return pos + 2 * sizeof(unsigned int) + sizeof(ct_addr_t) + sizeof(unsigned long long) + sizeof(char);
    #else
    return 0;
    #endif
}

void __ctStoreBarrier(bool enter, void* a, ct_tsc_t start)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    
    unsigned int p = __ctThreadLocalBuffer->pos;
    
    *((ct_event_id*)&__ctThreadLocalBuffer->data[p]) = ct_event_barrier/*<<24*/;
    *((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = __ctThreadLocalNumber;
    *((char*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int)]) = enter;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int)+ sizeof(char)]) = start;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int)+ sizeof(char)+ sizeof(ct_tsc_t)]) = rdtsc();
    *((ct_addr_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + 2*sizeof(ct_tsc_t)+ sizeof(char)]) = (ct_addr_t) a;
    #ifdef POS_USED
    __ctThreadLocalBuffer->pos += 2 * sizeof(unsigned int) + 2*sizeof(ct_tsc_t) + sizeof(ct_addr_t) + sizeof(char);
    #endif
}

unsigned int __ctStoreBarrierPos(bool enter, void* a, unsigned int pos, ct_tsc_t start)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    
    unsigned int p = pos;
    
    *((ct_event_id*)&__ctThreadLocalBuffer->data[p]) = ct_event_barrier/*<<24*/;
    *((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = __ctThreadLocalNumber;
    *((char*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int)]) = enter;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int)+ sizeof(char)]) = start;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int)+ sizeof(char)+ sizeof(ct_tsc_t)]) = rdtsc();
    *((ct_addr_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + 2*sizeof(ct_tsc_t)+ sizeof(char)]) = (ct_addr_t) a;
    #ifdef POS_USED
    return pos + 2 * sizeof(unsigned int) + 2*sizeof(ct_tsc_t) + sizeof(ct_addr_t) + sizeof(char);
    #else
    return 0;
    #endif
}

void __ctStoreThreadJoin(pthread_t pt, ct_tsc_t start)
{
    __ctStoreThreadJoinInternal(false, __ctLookupThreadInfo(pt), start);
}

unsigned int __ctStoreThreadJoinPos(pthread_t pt, unsigned int pos, ct_tsc_t start)
{
    return __ctStoreThreadJoinInternalPos(false, __ctLookupThreadInfo(pt), pos, start);
}

void __ctStoreThreadJoinInternal(bool ie, unsigned int id, ct_tsc_t start)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    
    unsigned int p = __ctThreadLocalBuffer->pos;
    
    *((ct_event_id*)&__ctThreadLocalBuffer->data[p]) = ct_event_task_join/*<<24*/;
    *((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = __ctThreadLocalNumber;
    *((char*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int)]) = ie;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int)+ sizeof(char)]) = start;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int)+ sizeof(char)+ sizeof(ct_tsc_t)]) = rdtsc();
    *((unsigned int*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + sizeof(char)+ 2*sizeof(ct_tsc_t)]) = id;
    #ifdef POS_USED
    __ctThreadLocalBuffer->pos += 3 * sizeof(unsigned int) + sizeof(bool)+ 2*sizeof(ct_tsc_t);
    #endif
}

unsigned int __ctStoreThreadJoinInternalPos(bool ie, unsigned int id, unsigned int pos, ct_tsc_t start)
{
    #ifdef __NULL_CHECK
    if (__ctThreadLocalBuffer == NULL) return;
    #endif
    
    unsigned int p = pos;
    
    *((ct_event_id*)&__ctThreadLocalBuffer->data[p]) = ct_event_task_join/*<<24*/;
    *((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = __ctThreadLocalNumber;
    *((char*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int)]) = ie;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int)+ sizeof(char)]) = start;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int)+ sizeof(char)+ sizeof(ct_tsc_t)]) = rdtsc();
    *((unsigned int*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + sizeof(char) + 2*sizeof(ct_tsc_t)]) = id;
    #ifdef POS_USED
    return pos + 3 * sizeof(unsigned int) + sizeof(bool) + 2*sizeof(ct_tsc_t);
    #else
    return 0;
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
    *((unsigned int*)&__ctThreadLocalBuffer->data[p + sizeof(unsigned int)]) = __ctThreadLocalNumber;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int)]) = start_t;
    *((ct_tsc_t*)&__ctThreadLocalBuffer->data[p + 2 * sizeof(unsigned int) + sizeof(ct_tsc_t)]) = t;
    
    __ctThreadLocalBuffer->pos += sizeof(unsigned int) * 2 + sizeof(ct_tsc_t) * 2;
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
    //__ctStoreThreadJoinInternal(false, taskId, rdtsc());
    //__ctQueueBuffer(true);
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

__thread unsigned int ctAllocCount = 4;
__thread char* ctBaseChunk = NULL;

pct_serial_buffer ctInternalAllocateBuffer()
{
    if (ctAllocCount == 4)
    {
        ctBaseChunk = mmap(NULL, 4 * SERIAL_BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0);
        ctAllocCount = 0;
    }
    char* r = ctBaseChunk + ctAllocCount * SERIAL_BUFFER_SIZE;
    ctAllocCount++;
    return (pct_serial_buffer) r;
}

#if 0
{
    char* r = __sync_add_and_fetch(&ctBaseChunk, 1);
    unsigned long int i = ((unsigned long int)r) & 511;
    char* myBase = (char*)(((unsigned long long)r) & (~511));
    
    if (myBase == NULL) goto chunkAlloc;
    if (i < 256)
        return (pct_serial_buffer) (myBase + SERIAL_BUFFER_SIZE * i);
chunkAlloc:
    pthread_mutex_lock(&ctAllocLock);
    if ((char*)(((unsigned long long)ctBaseChunk) & (~511)) == NULL ||
        (((unsigned long long)ctBaseChunk)&511))
    {
        char* t = mmap(NULL, 256 * SERIAL_BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0);
        if (t == NULL) {fprintf(stderr, "Failed mmap of internal buffer\n"); exit(0);}
        ctBaseChunk = t;
        pthread_mutex_unlock(&ctAllocLock);
        return (pct_serial_buffer) t;
    }
    else
    {
        // someone else has already allocated a chunk
        r = __sync_add_and_fetch(&ctBaseChunk, 1);
        i = ((unsigned long int)r) & 511;
        myBase = (char*)(((unsigned long long)r) & (~511));
        
        assert(i < 256);
        pthread_mutex_unlock(&ctAllocLock);
        return (pct_serial_buffer) (myBase + SERIAL_BUFFER_SIZE * i);
    }
    
}
#endif
