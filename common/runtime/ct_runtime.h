#ifndef CT_RUNTIME_H
#define CT_RUNTIME_H

#include <ct_event_st.h>
//#include "../taskLib/ct_file.h"
#include <pthread.h>
#include <zlib.h>

#define SERIAL_BUFFER_SIZE (1024 * 1024)

// Used to store serial data
typedef struct _ct_serial_buffer
{
    unsigned int pos, length, id;
    struct _ct_serial_buffer* next; // can order buffers 
    char data[0];
} ct_serial_buffer, *pct_serial_buffer;

// possibly unneeded with __thread variables
typedef struct _ct_serial_header
{

} ct_serial_header, *pct_serial_header;

typedef struct _contech_thread_create {
    void* (*func)(void*);
    void* arg;
    unsigned int parent_ctid;
    unsigned int child_ctid;
    ct_tsc_t volatile child_skew;
    char pad[64];
    ct_tsc_t volatile parent_skew;
} contech_thread_create, *pcontech_thread_create;

typedef struct _contech_thread_info {
    pthread_t pt_info;
    unsigned int ctid;
    struct _contech_thread_info* next;
} contech_thread_info, *pcontech_thread_info;

void __ctCleanupThread(void* v);
void __ctAllocateLocalBuffer();
unsigned int __ctAllocateCTid();

int __ctThreadCreateActual(pthread_t*, const pthread_attr_t*, void * (*start_routine)(void *), void*);

void __ctQueueBuffer(bool);
// (contech_id, basic block id, num of ops)
void __ctStoreBasicBlock( unsigned int, unsigned int);
// (basic block id, size of string, string)
void __ctStoreBasicBlockInfo (unsigned int, unsigned int, char*);
void __ctStoreMemOp(bool, char, void*, unsigned int);
void __ctStoreBasicBlockComplete(unsigned int);
void __ctStoreThreadCreate(unsigned int, long long, ct_tsc_t);
void __ctStoreThreadJoin(pthread_t, ct_tsc_t);
void __ctStoreSync(void*, int, int, ct_tsc_t);
void __ctStoreBarrier(bool, void*, ct_tsc_t);
void __ctStoreMemoryEvent(bool, unsigned long long, void*);
void* __ctInitThread(void*);//pcontech_thread_create ptc
void __ctCheckBufferSize();

void __ctAddThreadInfo(pthread_t *pt, unsigned int);
unsigned int __ctLookupThreadInfo(pthread_t pt);

#endif
