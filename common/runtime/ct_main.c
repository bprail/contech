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

void* (__ctBackgroundThreadWriter)(void*);
void* (__ctBackgroundThreadDiscard)(void*);

bool __ctIsROIEnabled = false;
bool __ctIsROIActive = false;

extern int ct_orig_main(int, char**);

//#define CT_OVERHEAD_TRACK
void printQueueStats()
{
    #ifdef CT_OVERHEAD_TRACK
    printf("T(0), %lld, %lld, %d\n",  
                                __ctTotalThreadOverhead, 
                                __ctTotalTimeBetweenQueueBuffers,
                                __ctTotalThreadBuffersQueued);
    #endif
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
        
        if (getenv("CONTECH_ROI_ENABLE"))
        {
            __ctIsROIEnabled = true;
        }
    }
    
    __ctThreadInfoList = NULL;
    __ctThreadLocalBuffer = NULL;
    
    // Allocate a real CT buffer for the main thread, this replaces initBuffer
    //   Use ROI code to reset if there is a ROI present
    __ctAllocateLocalBuffer();

    {
        struct timeb tp;
        ftime(&tp);
        printf("CT_START: %d.%03d\n", (unsigned int)tp.time, tp.millitm);
    }
    
    // Invoke main, protected by pthread_cleanup handlers, so that main can exit cleanly with
    // its background thread
    __ctStoreThreadCreate(0, 0, rdtsc());
    
    if (__ctIsROIEnabled == true)
    {
        __ctQueueBuffer(false);
    }
    
    pthread_cleanup_push(__ctCleanupThreadMain, (void*)pt_temp);
    r = ct_orig_main(argc, argv);
    pthread_cleanup_pop(1);
    
    // In rare cases, after we exit, we still have instrumentation being called
    // Record those events in the static buffer and let them languish.
    __ctThreadLocalBuffer = (pct_serial_buffer)&initBuffer;
    
    return r;
}
#endif


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
    int mpiRank = __ctGetMPIRank();
    int mpiPresent = __ctIsMPIPresent();
    // TODO: Create MPI event
    // TODO: Modify filename with MPI rank
    // TODO: Only do the above when MPI is present
    
    if (fname == NULL)
    {
        fname = "/tmp/contech_fe      ";
        
#if EVENT_COMPRESS
        FILE* tempFileHandle = fopen(fname, "wb");
        serialFileComp = gzdopen (fileno(tempFileHandle), "wb");
#else
        if (mpiPresent != 0)
        {
            char* fnameMPI = strdup(fname);
            fnameMPI[15] = '.';
            snprintf(fnameMPI + 16, 5, "%d", mpiRank);
            serialFile = fopen(fnameMPI, "wb");
            free(fnameMPI);
        }
        else
        {
            serialFile = fopen(fname, "wb");
        }
#endif
    }
    else
    {
#if EVENT_COMPRESS
        FILE* tempFileHandle = fopen(fname, "wb");
        serialFileComp = gzdopen (fileno(tempFileHandle), "wb");
#else
        serialFile = fopen(fname, "wb");
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
        
        {
            size_t tl, wl;
            unsigned int buf[2];
            buf[0] = ct_event_rank;
            buf[1] = mpiRank;
            
            tl = 0;
            do
            {
                wl = fwrite(&buf + tl, sizeof(unsigned int), 2 - tl, serialFile);
                //if (wl > 0)
                // wl is 0 on error, so it is safe to still add
                tl += wl;
            } while  (tl < 2);
            
            totalWritten += 2 * sizeof(unsigned int);
        }
        
        bb_info += 4; // skip the basic block count
        while (bb_info != _binary_contech_bin_end)
        {
            // id, len, memop_0, ... memop_len-1
            // Contech pass lays out the events in appropriate format
            size_t tl = fwrite(bb_info, sizeof(char), _binary_contech_bin_end - bb_info, serialFile);
            bb_info += tl;
            totalWritten += tl;
        }
    }
    
    // Main loop
    //   Write queued buffer to disk until program terminates
    pthread_mutex_lock(&__ctQueueBufferLock);
    do {
        // Check for queued buffer, i.e. is the program generating events
        while (__ctQueuedBuffers == NULL)
        {
            pthread_cond_wait(&__ctQueueSignal, &__ctQueueBufferLock);
        }
        pthread_mutex_unlock(&__ctQueueBufferLock);
    
        // The thread writer will likely sit in this loop except when the memory limit is triggered
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
                    fprintf(stderr, "Illegal buffer size - %d\n", qb->pos);
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
                //     continue;
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
#if DEBUG
                    if (__ctCurrentBuffers < 2)
                    {
                        printf("%p\n", &t->data);
                    }
#endif
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
            printQueueStats();
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

//
//  __ctBackgroundThreadDiscard()
//    This routine is like the background thread writer, except it discards the buffers instead.
//
void* __ctBackgroundThreadDiscard(void* d)
{
    size_t totalWritten = 0;
    pct_serial_buffer memLimitQueue = NULL;
    pct_serial_buffer memLimitQueueTail = NULL;
    unsigned long long totalLimitTime = 0, startLimitTime, endLimitTime;
    
    // Main loop
    //   Write queued buffer to disk until program terminates
    pthread_mutex_lock(&__ctQueueBufferLock);
    do {
        // Check for queued buffer, i.e. is the program generating events
        while (__ctQueuedBuffers == NULL)
        {
            pthread_cond_wait(&__ctQueueSignal, &__ctQueueBufferLock);
        }
        pthread_mutex_unlock(&__ctQueueBufferLock);
    
        // The thread writer will likely sit in this loop except when the memory limit is triggered
        while (__ctQueuedBuffers != NULL)
        {
            // Write buffer to file
            size_t tl = 0;
            size_t wl = 0;
            pct_serial_buffer qb = __ctQueuedBuffers;
            
            // **** DISCARD BUFFER in lieu of writing
            
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

            pthread_mutex_unlock(&__ctQueueBufferLock);
            pthread_exit(NULL);            
        }
    } while (1);
}
