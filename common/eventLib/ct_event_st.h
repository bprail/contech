#ifndef CT_EVENT_ST_H
#define CT_EVENT_ST_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#define CONTECH_EVENT_VERSION 9

typedef uint64_t ct_tsc_t;
typedef uint64_t ct_addr_t;

typedef struct _ct_memory_op {
  union {
    struct {
        // NB. Addr must come first so that the storing to data32[0] is completely stored into
        //   the address field.
        uint64_t addr : 50;
        uint64_t rank : 8;
        uint64_t is_write : 1;
        uint64_t pow_size : 3; // the size of the op is 2^pow_size
    };
    uint64_t data;
    uint32_t data32[2];
  };
} ct_memory_op, *pct_memory_op;

// Event IDs used primarily in serialize / deserialize
enum _ct_event_id { ct_event_basic_block = 0, 
    ct_event_basic_block_info = 128, 
    ct_event_memory, 
    ct_event_sync, 
    ct_event_barrier, 
    ct_event_task_create, 
    ct_event_task_join, 
    ct_event_buffer,  // INTERNAL USE
    ct_event_bulk_memory_op,
    ct_event_version, // INTERNAL USE
    ct_event_delay,
    ct_event_rank,
    ct_event_mpi_transfer,
    ct_event_mpi_wait,
    ct_event_roi,
    ct_event_gv_info,
    ct_event_unknown};
typedef enum _ct_event_id ct_event_id;

enum _ct_sync_type {
    ct_sync_release = 0,
    ct_sync_acquire = 1,
    ct_cond_wait,
    ct_cond_sig,
    ct_sync_atomic,
    ct_task_depend,
    ct_sync_unknown};
typedef enum _ct_sync_type ct_sync_type;

#define BBI_FLAG_CONTAIN_CALL 0x1
#define BBI_FLAG_MEM_DUP 0x2
#define BBI_FLAG_MEM_GV 0x4

#endif
