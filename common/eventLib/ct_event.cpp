#include "ct_event.h"
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../taskLib/ct_file.h"

using namespace contech;

//#define fread_check(x,y,z,a) do {if (z != (t = fread(x,y,z,a))) {fprintf(stderr, "FREAD failure at %d of %d after %llu\n", __LINE__, z, sum);dumpAndTerminate();} sum += (t * y);} while(0)
// Use ct_file
#define fread_check(x,y,z,a) do {if ((y * z) != (t = ct_read(x,(y * z),a))) {fprintf(stderr, "FREAD failure at %d of %d after %llu\n", __LINE__, z, sum);dumpAndTerminate(a);} sum += (t);} while(0)

EventLib::EventLib()
{
    sum = 0;
    bufSum = 0;
    lastBufPos = 0;
    lastID = 0;
    lastBBID = 0;
    lastType = 0;
    
    cedPos = 0;
    debug_file = NULL;
    
    version = 0;
    currentID = ~0;
    bb_count = 0;
    
    bb_info_table = NULL;
}

/* unpack: unpack packed items from buf, return length */
int EventLib::unpack(uint8_t *buf, char *fmt, ...)
{
    va_list args;
    char *p;
    uint8_t *bp, *pc;
    uint16_t *ps;
    uint32_t *pl;

    bp = buf;
    va_start(args, fmt);
    for (p = fmt; *p != '\0'; p++) {

        switch (*p) 
        {
            case 'c': /* char */
            {
                pc = va_arg(args, uint8_t*);

                *pc = *bp++;

                break;
            }
            case 's': /* short */
            {
                ps = va_arg(args, uint16_t*);
                
                *ps = *bp++ << 8;
                *ps |= *bp++;

                break;
            }
            case 'l': /* long */
            {
                pl = va_arg(args, uint32_t*);

                *pl = *bp++ << 24;
                *pl |= *bp++ << 16;
                *pl |= *bp++ << 8;
                *pl |= *bp++;
            }
            default: /* illegal type character */
            {
                va_end(args);

                return -1;
            }
        }
     }
     va_end(args);

     return bp - buf;
}

void EventLib::resetEventLib()
{
    if (bb_info_table != NULL) 
    {
        for (int i = 0; i < bb_count; i++)
        {
            if (bb_info_table[i].mem_op_info != NULL) free(bb_info_table[i].mem_op_info);
        }
        free(bb_info_table);
    }
    bb_info_table = NULL;
    version = 0;
    sum = 0;
    bb_count = 0;
    currentID = 0;
    bufSum = 0;
}

//
// Deserialize a CT_EVENT from a FILE stream
//
pct_event EventLib::createContechEvent(ct_file *fptr)//FILE* fptr)
{
    unsigned int t;
    pct_event npe;
    unsigned long long startSum = sum;

    // feof does no good...
    //if (feof(fptr)) return NULL;
    
    if (debug_file == NULL)
    {
    //    debug_file = fopen("debug.log", "w");
    }
    
    npe = (pct_event) malloc(sizeof(ct_event));
    if (npe == NULL)
    {
        fprintf(stderr, "Failure to allocate new contech event\n");
        return NULL;
    }
    
    //fscanf(fptr, "%ud%ud", &npe->contech_id, &npe->contech_type);
    //if (0 == (t = fread(&npe->contech_id, sizeof(unsigned int), 1, fptr)))
    if (version == 0)
    {
        if (0 == (t = ct_read(&npe->contech_id, sizeof(unsigned int), fptr)))
        {
            free(npe);
            return NULL;
        }
        // ct_read returns bytes read not elements read
        sum += t;
        
        fread_check(&npe->event_type, sizeof(unsigned int), 1, fptr);
    }
    else
    {
        // Problem here is that event_type is of size int, 
        // so we have to initialize the field and not just the ct_read call
        npe->event_type = (ct_event_id)0;
        if (0 == (t = ct_read(&npe->event_type, sizeof(char), fptr)))
        {
            free(npe);
            return NULL;
        }
        sum += t;
                
        npe->contech_id = currentID;
        
        // Currently, runtime treats event_type as int, except for basic blocks
        // Also storing thread_id then gives TYPE + [3], ID[4], so read [7]
        if (npe->event_type != ct_event_basic_block &&
            npe->event_type != ct_event_basic_block_info && 
            npe->event_type != ct_event_buffer &&
            npe->event_type != ct_event_roi)
        {
            char buf[7];
            // As of 8/18/14, thread_id is removed from all events
            fread_check(buf, sizeof(char), 3, fptr);
            if (version < 5)
            {
                fread_check(buf, sizeof(char), 4, fptr);
            }
        }
    }
    
    switch (npe->event_type)
    {
        case (ct_event_basic_block):
        {
            if (version == 0)
            {
                fread_check(&npe->bb.basic_block_id, sizeof(unsigned int), 1, fptr);
                fread_check(&npe->bb.len, sizeof(unsigned int), 1, fptr);
            }
            else
            {
                npe->bb.basic_block_id = 0;
                fread_check(&npe->bb.basic_block_id, sizeof(char), 3, fptr);
                if (npe->bb.basic_block_id >= bb_count)
                {
                    fprintf(stderr, "ERROR: BBid(%d) exceeds maximum in bb_info (%d)\n", npe->bb.basic_block_id, bb_count);
                    dumpAndTerminate(fptr);
                }
                
                npe->bb.len = bb_info_table[npe->bb.basic_block_id].len;
            }
            //fscanf(fptr, "%ud", &npe->bb.len);

            /*
            // IN testing, the following code verified that the bb info's matched
            //  the expected results
            if (version > 0 && 
                (npe->bb.basic_block_id >= bb_count ||
                 bb_info_table[npe->bb.basic_block_id].len != npe->bb.len))
            {
                fprintf(stderr, "Info table does not match value in event list\n");
                fprintf(stderr, "BBID: %d LEN: %d\n", npe->bb.basic_block_id, npe->bb.len);
                fprintf(stderr, "BB_COUNT: %d  LEN: %d\n", bb_count, bb_info_table[npe->bb.basic_block_id].len);
                dumpAndTerminate();
            }*/
            
            if (npe->bb.len > 0)
            {
                npe->bb.mem_op_array = (pct_memory_op) malloc(npe->bb.len * sizeof(ct_memory_op));

                if (npe->bb.mem_op_array == NULL)
                {
                    fprintf(stderr, "Failure to allocate array for memory ops in basic block event\n");
                    free (npe);
                    return NULL;
                }
                
                if (sizeof(ct_memory_op) > sizeof(unsigned long long))
                {
                    fprintf(stderr, "Contect memory op is larger than a long long (8 bytes)\n");
                }
                if (version == 0)
                {
                    fread_check(npe->bb.mem_op_array, sizeof(ct_memory_op), npe->bb.len, fptr);
                }
                else
                {
                    unsigned int id = npe->bb.basic_block_id;
                    for (int i = 0; i < npe->bb.len; i++)
                    {
                        npe->bb.mem_op_array[i].data = 0;
                        
                        fread_check(&npe->bb.mem_op_array[i].data32[0], sizeof(unsigned int), 1, fptr);
                        fread_check(&npe->bb.mem_op_array[i].data32[1], sizeof(unsigned short), 1, fptr);
                        
                        npe->bb.mem_op_array[i].is_write = bb_info_table[id].mem_op_info[i].isWrite;
                        npe->bb.mem_op_array[i].pow_size = bb_info_table[id].mem_op_info[i].size;
                    }
                }
            }
            else 
            {
                npe->bb.mem_op_array = NULL;
            }
        }
        break;
        
        case (ct_event_basic_block_info):
        {
            unsigned int id, len, line;
            char* tStr = NULL;
            fread_check(&id, sizeof(unsigned int), 1, fptr);
            if (id >= bb_count)
            {
                fprintf(stderr, "ERROR: INFO for block %d exceeds number of unique basic blocks (%d)\n", id, bb_count);
                dumpAndTerminate(fptr);
            }
            npe->bbi.basic_block_id = id;
            
            fread_check(&line, sizeof(unsigned int), 1, fptr);
            npe->bbi.flags = line;
            
            fread_check(&line, sizeof(unsigned int), 1, fptr);
            npe->bbi.line_num = line;
            
            fread_check(&line, sizeof(unsigned int), 1, fptr);
            npe->bbi.num_ops = line;
            
            fread_check(&line, sizeof(unsigned int), 1, fptr);
            npe->bbi.crit_path_len = line;
            
            fread_check(&len, sizeof(unsigned int), 1, fptr);
            npe->bbi.fun_name_len = len;
            if (len > 0)
            {
                tStr = (char*) malloc(sizeof(char) * (len + 1));
                if (tStr == NULL)
                {
                    fprintf(stderr, "ERROR: Failed to allocate %u bytes for function name\n", sizeof(char) * (len + 1));
                    free(npe);
                    return NULL;
                }
                tStr[len] = '\0';
                fread_check(tStr, sizeof(char), len, fptr);
                npe->bbi.fun_name = tStr;
            }
            else
            {
                npe->bbi.fun_name = NULL;
            }
            
            fread_check(&len, sizeof(unsigned int), 1, fptr);
            npe->bbi.file_name_len = len;
            if (len > 0)
            {
                tStr = (char*) malloc(sizeof(char) * (len + 1));
                if (tStr == NULL)
                {
                    fprintf(stderr, "ERROR: Failed to allocate %u bytes for function name\n", sizeof(char) * (len + 1));
                    free(npe->bbi.fun_name);
                    free(npe);
                    return NULL;
                }
                tStr[len] = '\0';
                fread_check(tStr, sizeof(char), len, fptr);
                npe->bbi.file_name = tStr;
            }
            else
            {
                npe->bbi.file_name = NULL;
            }
            
            fread_check(&len, sizeof(unsigned int), 1, fptr);
            npe->bbi.callFun_name_len = len;
            if (len > 0)
            {
                tStr = (char*) malloc(sizeof(char) * (len + 1));
                if (tStr == NULL)
                {
                    fprintf(stderr, "ERROR: Failed to allocate %u bytes for function name\n", sizeof(char) * (len + 1));
                    free(npe->bbi.file_name);
                    free(npe->bbi.fun_name);
                    free(npe);
                    return NULL;
                }
                tStr[len] = '\0';
                fread_check(tStr, sizeof(char), len, fptr);
                npe->bbi.callFun_name = tStr;
            }
            else
            {
                npe->bbi.callFun_name = NULL;
            }
            
            fread_check(&len, sizeof(unsigned int), 1, fptr);
            bb_info_table[id].len = len;
            npe->bbi.num_mem_ops = len;
            
            //fprintf(stderr, "Store INFO [%d].len = %d\n", id, len);
            
            if (len > 0)
            {
                bb_info_table[id].mem_op_info = (pinternal_memory_op_info) malloc(sizeof(internal_memory_op_info) * len);

                for (int i = 0; i < len; i++)
                {
                    fread_check(&bb_info_table[id].mem_op_info[i], sizeof(char), 2, fptr);
                }
            }
            else
            {
                bb_info_table[id].mem_op_info = NULL;
            }
        }
        break;
        
        case (ct_event_task_create):
        {
            fread_check(&npe->tc.start_time, sizeof(ct_tsc_t), 1, fptr);
            fread_check(&npe->tc.end_time, sizeof(ct_tsc_t), 1, fptr);
            fread_check(&npe->tc.other_id, sizeof(unsigned int), 1, fptr);
            fread_check(&npe->tc.approx_skew, sizeof(long long), 1, fptr);
        }
        break;
        
        case (ct_event_task_join):
        {
            fread_check(&npe->tj.isExit, sizeof(bool), 1, fptr);
            fread_check(&npe->tj.start_time, sizeof(ct_tsc_t), 1, fptr);
            fread_check(&npe->tj.end_time, sizeof(ct_tsc_t), 1, fptr);
            fread_check(&npe->tj.other_id, sizeof(unsigned int), 1, fptr);
        }
        break;
        
        case (ct_event_sync):
        {
            fread_check(&npe->sy.start_time, sizeof(ct_tsc_t), 1, fptr);
            fread_check(&npe->sy.end_time, sizeof(ct_tsc_t), 1, fptr);
            fread_check(&npe->sy.sync_type, sizeof(int), 1, fptr);
            fread_check(&npe->sy.sync_addr, sizeof(ct_addr_t), 1, fptr);
            fread_check(&npe->sy.ticketNum, sizeof(unsigned long long), 1, fptr);
        }
        break;
        
        case (ct_event_barrier):
        {
            fread_check(&npe->bar.onEnter, sizeof(bool), 1, fptr);
            fread_check(&npe->bar.start_time, sizeof(ct_tsc_t), 1, fptr);
            fread_check(&npe->bar.end_time, sizeof(ct_tsc_t), 1, fptr);
            fread_check(&npe->bar.sync_addr, sizeof(ct_addr_t), 1, fptr);
            fread_check(&npe->bar.barrierNum, sizeof(unsigned long long), 1, fptr);
        }
        break;
        
        case (ct_event_memory):
        {
            fread_check(&npe->mem.isAllocate, sizeof(bool), 1, fptr);
            fread_check(&npe->mem.size, sizeof(unsigned long long), 1, fptr);
            fread_check(&npe->mem.alloc_addr, sizeof(ct_addr_t), 1, fptr);
        }
        break;
        
        case (ct_event_buffer):
        {
            //fprintf(debug_file, "%u\n", lastBBID);
            if (version > 0)
            {
                char buf[3];
                fread_check(buf, sizeof(char), 3, fptr);
                fread_check(&npe->contech_id, sizeof(unsigned int), 1, fptr);
                //fprintf(stderr, "Now in ctid - %d\n", npe->contech_id);
            }
            fread_check(&npe->buf.pos, sizeof(unsigned int), 1, fptr);
            if (bufSum == 0)
            {
                // Everything we've read so far, except this event (12B)
                bufSum = sum - 12;
                
            }
            else if ((sum - 12) != bufSum)
            {
                fprintf(stderr, "Marker at %llu bytes, should be at 12 + %llu\n", sum, bufSum);
                dumpAndTerminate(fptr);
            }
            
            bufSum += npe->buf.pos + 12;  // 12 for the buffer event
            lastBufPos = npe->buf.pos;
            {
                int idx = lastBufPos % 1024;
                if (idx >= 1024 || lastBufPos > 1024) idx = 1024 - 1;
                binInfo[idx] ++;
            }
            if (version > 0)
            {
                currentID = npe->contech_id;
            }
        }
        break;
        
        case (ct_event_bulk_memory_op):
        {
            fread_check(&npe->bm.size, sizeof(unsigned long long), 1, fptr);
            fread_check(&npe->bm.dst_addr, sizeof(ct_addr_t), 1, fptr);
            fread_check(&npe->bm.src_addr, sizeof(ct_addr_t), 1, fptr);
        }
        break;
        
        case (ct_event_delay):
        {
            fread_check(&npe->dly.start_time, sizeof(ct_tsc_t), 1, fptr);
            fread_check(&npe->dly.end_time, sizeof(ct_tsc_t), 1, fptr);
        }
        break;
        
        case (ct_event_rank):
        {
            fread_check(&npe->rank.rank, sizeof(int), 1, fptr);
        }
        break;
        
        case (ct_event_mpi_transfer):
        {
            //mpixf
            fread_check(&npe->mpixf.isSend, sizeof(char), 1, fptr);
            fread_check(&npe->mpixf.isBlocking, sizeof(char), 1, fptr);
            fread_check(&npe->mpixf.comm_rank, sizeof(int), 1, fptr);
            fread_check(&npe->mpixf.tag, sizeof(int), 1, fptr);
            fread_check(&npe->mpixf.buf_ptr, sizeof(ct_addr_t), 1, fptr);
            fread_check(&npe->mpixf.buf_size, sizeof(size_t), 1, fptr);
            fread_check(&npe->mpixf.start_time, sizeof(ct_tsc_t), 1, fptr);
            fread_check(&npe->mpixf.end_time, sizeof(ct_tsc_t), 1, fptr);
            fread_check(&npe->mpixf.req_ptr, sizeof(ct_addr_t), 1, fptr);
        }
        break;
        
        case (ct_event_mpi_wait):
        {
            fread_check(&npe->mpiw.req_ptr, sizeof(ct_addr_t), 1, fptr);
            fread_check(&npe->mpiw.start_time, sizeof(ct_tsc_t), 1, fptr);
            fread_check(&npe->mpiw.end_time, sizeof(ct_tsc_t), 1, fptr);
        }
        break;
        
        case (ct_event_version):
        {
            // There should be only one version event in the list
            assert(version == 0);
            fread_check(&version, sizeof(unsigned int), 1, fptr);
            fread_check(&bb_count, sizeof(unsigned int), 1, fptr);
            if (bb_count > 0)
                bb_info_table = (pinternal_basic_block_info) malloc (sizeof(internal_basic_block_info) * bb_count);
            
            if (version > CONTECH_EVENT_VERSION)
                fprintf(stderr, "WARNING: Version %d exceeds supported versions\n", version);
            else
                fprintf(stderr, "Event Version set: %d\tBasic Block table: %d\n", version, bb_count);
                
            // setDebugScan();
            for (int i = 0; i < 512; i++) binInfo[i] = 0;
        }
        break;
        
        case (ct_event_roi):
        {
            // This event has no additional fields
        }
        break;
        
        default:
        {
            assert(!isCompressed(fptr));
            fprintf(stderr, "ERROR: type %d not supported at %llu\n", npe->event_type, sum);
            fprintf(stderr, "\tPrevious event - %d with ID - %d\n", lastType, lastID);
            dumpAndTerminate(fptr);
        }
        break;
    }
    
    lastID = npe->contech_id;
    lastType = npe->event_type;
    if (npe->event_type == ct_event_basic_block)
    {
        lastBBID = npe->bb.basic_block_id;
    }
    
    cedPos ++;
    if (cedPos > (64 - 1)) cedPos = 0;
    ced[cedPos].sum = startSum;
    ced[cedPos].id = lastID;
    ced[cedPos].type = lastType;
    if (npe->event_type == ct_event_basic_block)
    {
        ced[cedPos].data0 = npe->bb.basic_block_id;
        ced[cedPos].data1 = npe->bb.len;
    }
    else
    {
        ced[cedPos].data0 = npe->mem.isAllocate;
        ced[cedPos].data1 = 0;
    }

    if (sum > bufSum && bufSum > 0) 
    {
        fprintf(stderr, "ERROR: Missing buffer event at %llx.  Should be after %d bytes.\n", sum, lastBufPos);
        dumpAndTerminate(fptr);
    }
    
    return npe;
}

void EventLib::deleteContechEvent(pct_event e)
{
    if (e == NULL) return;
    if (e->event_type == ct_event_basic_block && e->bb.mem_op_array != NULL) free(e->bb.mem_op_array);
    if (e->event_type == ct_event_basic_block_info)
    {
        if (e->bbi.fun_name != NULL) free(e->bbi.fun_name);
        if (e->bbi.file_name != NULL) free(e->bbi.file_name);
    }    
    free(e);
}

void EventLib::dumpAndTerminate(ct_file *fptr)
{
    FILE* fh = getUncompressedHandle(fptr);
    struct stat buf;
    char d = 0;
    fstat(fileno(fh), &buf);
    fprintf(stderr, "%llx - %d - %d - %llx - %d - %llx\n", 
                    fh, ferror(fh), feof(fh), ftell(fh), fread(&d, 1, 1, fh), buf.st_size);
    displayContechEventDebugInfo();
    exit(1);
}

void EventLib::displayContechEventDiagInfo()
{
    for (int i = 0; i < 1024; i++)
    {
        fprintf(stderr, "%d,", binInfo[i]);
    }
    fprintf(stderr, "\n");
}

void EventLib::displayContechEventDebugInfo()
{
    int i;
    fprintf(stderr, "Consumed %llu bytes, in buffer of %d to %llu\n", sum, lastBufPos, bufSum);
    fprintf(stderr, "\tOFF(ty(id) - data0 data 1\n");
    for (i = cedPos; i >= 0; i--)
    {
        fprintf(stderr, "\t0x%llx(%d(%d) - %d %d)\n", ced[i].sum, ced[i].type, ced[i].id, ced[i].data0, ced[i].data1);
    }
    for (i = 64 - 1; i > cedPos; i--)
    {
        fprintf(stderr, "\t0x%llx(%d(%d) - %d %d)\n", ced[i].sum, ced[i].type, ced[i].id, ced[i].data0, ced[i].data1);
    }
    //fprintf(stderr, "Last id - %d, type - %d\n", lastID, lastType);
    fflush(stderr);
}

void EventLib::displayContechEventStats()
{
#ifdef SCAN_TRACE
    fprintf(stderr, "ZERO: %llu\t NEG1: %llu\tBYTES: %llu\n", zeroBytes, negOneBytes, bufSum);
#endif
}
