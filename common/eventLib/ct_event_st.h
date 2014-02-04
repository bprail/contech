#ifndef CT_EVENT_ST_H
#define CT_EVENT_ST_H

#include <stdio.h>
#include <stdbool.h>

//#if 0
//#ifndef __cplusplus
//typedef char bool;
//#define false 0
//#define true 1
//#endif
//#endif

#define CONTECH_EVENT_VERSION 3

typedef unsigned long long ct_tsc_t;
typedef unsigned long long ct_addr_t;

typedef struct _ct_memory_op {
  union {
    struct {
        unsigned long long is_write : 1;
        unsigned long long pow_size : 3; // the size of the op is 2^pow_size
        unsigned long long addr : 58;
    };
    unsigned long long data;
    unsigned int data32[2];
  };
} ct_memory_op, *pct_memory_op;

// Event IDs used primarily in serialize / deserialize
//   NB.  If the high bit (ie 128) is set, then the field
//   represents the memory op information (is_write and pow_size)
enum _ct_event_id { ct_event_basic_block = 0, 
    ct_event_basic_block_info, 
    ct_event_memory, 
    ct_event_sync, 
    ct_event_barrier, 
    ct_event_task_create, 
    ct_event_task_join, 
    ct_event_buffer,  // INTERNAL USE
    ct_event_bulk_memory_op,
    ct_event_version, // INTERNAL USE
    ct_event_memory_op = 128,
    ct_event_unknown};
typedef enum _ct_event_id ct_event_id;

enum _ct_sync_type {
    ct_sync_release = 0,
    ct_sync_acquire = 1,
    ct_cond_wait,
    ct_cond_sig,
    ct_sync_unknown};
typedef enum _ct_sync_type ct_sync_type;

//
// Maps basic_block_ids to string identifiers
//
typedef struct _ct_basic_block_info
{
    unsigned int basic_block_id, name_len;
    char name[0];
} ct_basic_block_info, *pct_basic_block_info;

typedef struct _ct_basic_block
{
    unsigned int basic_block_id, len;
    pct_memory_op mem_op_array;
} ct_basic_block, *pct_basic_block;

typedef struct _ct_memory
{
    bool isAllocate;
    unsigned long long size;  // Only has meaning if isAllocate = true
    ct_addr_t alloc_addr;
} ct_memory, *pct_memory;

typedef struct _ct_sync
{
    int sync_type;
    ct_tsc_t start_time;
    ct_tsc_t end_time;
    ct_addr_t sync_addr;
    unsigned long long ticketNum;
} ct_sync, *pct_sync;

typedef struct _ct_barrier
{
    bool onEnter;
    ct_tsc_t start_time;
    ct_tsc_t end_time;
    ct_addr_t sync_addr;
} ct_barrier, *pct_barrier;

typedef struct _ct_task_create
{
    unsigned int other_id; // child if id is creator, parent if id is created
    ct_tsc_t start_time;
    ct_tsc_t end_time;
    long long approx_skew; // 0 if parent, value if child, middle layer uses this as a flag
} ct_task_create, *pct_task_create;

typedef struct _ct_task_join
{
    bool isExit;
    ct_tsc_t start_time;
    ct_tsc_t end_time;
    unsigned int other_id; // self if calling exit, child if calling join
} ct_task_join, *pct_task_join;

typedef struct _ct_buffer_info
{
    unsigned int pos;
} ct_buffer_info, *pct_buffer_info;

typedef struct _ct_bulk_memory
{
    bool isWrite;
    unsigned long long size;
    ct_addr_t alloc_addr;
} ct_bulk_memory, *pct_bulk_memory;

//
// There are two ways to combine objects with common fields.
//   1) Common fields in a single type that is the first field
//       in the other structs
//   2) Union block of the other structs
//
//  Should a basic block contain an event header or should events be basic blocks?
//
typedef struct _ct_event {
    unsigned int contech_id;
    ct_event_id event_type;
    
    union {
        ct_basic_block      bb;
        ct_basic_block_info bbi;
        ct_barrier          bar;
        ct_sync             sy;
        ct_task_create      tc;
        ct_task_join        tj;
        ct_memory           mem;
        ct_buffer_info      buf;
        ct_bulk_memory      bm;
    };
} ct_event, *pct_event;

#endif
