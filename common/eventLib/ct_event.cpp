#include "ct_event.h"
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace contech;

void EventLib::fread_check(void* x, size_t y, size_t z, FILE* a)
{
    uint32_t t = 0;
    if ((y * z) != (t = ct_read(x,(y * z),a))) 
    {
        fprintf(stderr, "FREAD failure at %d of %lu after %lu\n", __LINE__, z, sum);
        dumpAndTerminate(a);
    } 
    sum += (t);
}
    
EventLib::EventLib()
{
    sum = 0;
    bufSum = 0;
    lastBufPos = 0;
    lastID = 0;
    lastBBID = 0;
    lastType = 0;
    next_basic_block_id = -1;
    
    skipSet.clear();
    skipList.clear();
    
    cedPos = 0;
    debug_file = NULL;
    
    version = 0;
    currentID = ~0;
    bb_count = 0;
    
    bb_info_table = NULL;
    currentPath = NULL;
    constGVAddr = NULL;
    maxConstGVId = 0;
}

EventLib::~EventLib()
{
    if (bb_info_table != NULL) 
    {
        uint64_t thresh = sum / 100;
        for (int i = 0; i < bb_count; i++)
        {
            uint64_t prod = bb_info_table[i].count * bb_info_table[i].totalBytes;
           
            if (prod > thresh)
            {
                printf("BBID:%u\t%d\t%u\t%lu\n", i, bb_info_table[i].count, bb_info_table[i].totalBytes, prod);
            }
        }
    }
    
    if (currentPath != NULL ) delete (currentPath);
    
    resetEventLib();
}

/* unpack: unpack packed items from buf, return length */
// This code is derived from a description in Practice of Programming
int EventLib::unpack(uint8_t *buf, char const fmt[], ...)
{
    va_list args;
    const char *p;
    uint8_t *bp, *pc;
    uint16_t *ps;
    uint32_t *pl;
    uint64_t *pll;

    bp = buf;
    va_start(args, fmt);
    for (p = fmt; *p != '\0'; p++) 
    {
        char ty = *p;
        if (ty == 's') /* size_t */
        {
            switch (sizeof(size_t))
            {
                case (sizeof(uint16_t)): ty = 's'; break;
                case (sizeof(uint32_t)): ty = 'l'; break;
                case (sizeof(uint64_t)): ty = 't'; break;
                default: ty = 'l'; break;
            }
        }
        
        switch (ty) 
        {
            case 'b': /* bool */
            case 'c': /* char */
            {
                pc = va_arg(args, uint8_t*);

                *pc = *bp++;

                break;
            }
            case 's': /* short */
            {
                ps = va_arg(args, uint16_t*);
                
                *ps = *bp++;
                *ps |= *bp++ << 8;

                break;
            }
            case 'l': /* long */
            {
                pl = va_arg(args, uint32_t*);

                *pl = *bp++;
                *pl |= *bp++ << 8;
                *pl |= *bp++ << 16;
                *pl |= *bp++ << 24;
                break;
            }
            case 't': /* ct_tsc_t */
            case 'p': /* pointer or long long */
            {
                pll = va_arg(args, uint64_t*);
                
                *pll = *bp++;
                *pll += *bp++ << 8;
                *pll += *bp++ << 16;
                *pll += ((uint64_t)(*bp++)) << 24; // If adding 0x80, compiler would set high bits to ff..
                *pll += ((uint64_t)(*bp++)) << 32;
                *pll += ((uint64_t)(*bp++)) << 40;
                *pll += ((uint64_t)(*bp++)) << 48;
                *pll += ((uint64_t)(*bp++)) << 56;
                
                break;
            }
            default: /* illegal type character */
            {
                va_end(args);
                assert("Illegal type character" && 0);
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
    constGVAddr = NULL;
    maxConstGVId = 0;
    
    path_info_table.clear();
    if (currentPath != NULL) {delete currentPath; currentPath = NULL;}
    
    skipSet.clear();
    skipList.clear();
}

void EventLib::readMemOp(pct_memory_op pmo, FILE* fptr)
{
    pmo->data = 0;
    fread_check(&pmo->data32[0], sizeof(unsigned int), 1, fptr);
    fread_check(&pmo->data32[1], sizeof(unsigned short), 1, fptr);
}

//
// Deserialize a CT_EVENT from a FILE stream
//
pct_event EventLib::createContechEvent(FILE* fptr)
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
    else if (this->next_basic_block_id != -1)
    {
        //fprintf(stderr, "Implicit ID: %d\n", this->next_basic_block_id);
        npe->contech_id = currentID;
        npe->event_type = ct_event_basic_block;
        npe->bb.basic_block_id = this->next_basic_block_id;
    }
    else if (currentPath != NULL)
    {
        npe->contech_id = currentID;
        npe->event_type = ct_event_basic_block;
        
        int idx = -1;
        for (int i = 0; i < currentPath->pathInfo->pathDepth; i++)
        {
            if (lastBBID == currentPath->pathInfo->pathMap[i])
            {
                idx = i;
                break;
            }
        }
        
        if (idx == -1)
        {
            delete currentPath;
            currentPath = NULL;
            free(npe);
            return createContechEvent(fptr);
        }
        
        uint32_t branchID = currentPath->pathInfo->pathMap[idx];
        assert(branchID < bb_count);
        pinternal_basic_block_info bbi = &bb_info_table[branchID];
        
        npe->bb.basic_block_id = bbi->next_basic_block_id[(currentPath->pathBits >> idx) & 0x1];
        this->next_basic_block_id = npe->bb.basic_block_id;
        
        /*uint32_t branchID = currentPath->pathInfo->pathMap[currentPath->currentPathIndex];
        assert(branchID < bb_count);
        pinternal_basic_block_info bbi = &bb_info_table[branchID];
        
        npe->bb.basic_block_id = bbi->next_basic_block_id[(currentPath->pathBits) & 0x1];
        this->next_basic_block_id = npe->bb.basic_block_id;
        currentPath->pathBits >>= 1;
        currentPath->currentPathIndex++;
        if (currentPath->currentPathIndex == currentPath->pathInfo->pathDepth)
        {
            //printf("L%d, N: %d -> %d\n", lastBBID, npe->bb.basic_block_id, 
            //                             bb_info_table[npe->bb.basic_block_id].next_basic_block_id[0]);
            delete currentPath;
            currentPath = NULL;
        }
        else
        {
            //printf("CD: %d D: %d\n", currentPath->currentPathIndex, currentPath->pathInfo->pathDepth);
            //printf("PB: %d\n", currentPath->pathBits);
        }*/
    }
    else
    {
        // Problem here is that event_type is of size int, 
        // so we have to initialize the field and not just the ct_read call
        npe->event_type = (ct_event_id)0;
        if (0 == (t = ct_read(&npe->event_type, sizeof(char), fptr)))
        {
            free(npe);
            
            // Go back and check for unblocked buffers
            long earliestPos = LONG_MAX;
            for (auto it = skipList.begin(), et = skipList.end(); it != et; ++it)
            {
                auto ss = skipSet.find(it->first);
                if (ss != skipSet.end() && ss->second == true) continue;
                if (it->second.size() == 0)
                {
                    //it = skipList.erase(it);
                    //--it;
                    continue;
                }
                long newPos = it->second.front();
                if (newPos < earliestPos)
                {
                    earliestPos = newPos;
                }
            }
            
            if (earliestPos < LONG_MAX)
            {
                fseek(fptr, earliestPos, SEEK_SET);
                //fprintf(stderr, "Skipping to: %ld\n", earliestPos);
                
                return createContechEvent(fptr);
            }
            
            return NULL;
        }
        
        if (npe->event_type < ct_event_basic_block_info) 
        {
            npe->bb.basic_block_id = npe->event_type;
            npe->event_type = ct_event_basic_block;
        }
        
        sum += t;
                
        npe->contech_id = currentID;
        
        // Currently, runtime treats event_type as int, except for basic blocks
        // Also storing thread_id then gives TYPE + [3], ID[4], so read [7]
        if (npe->event_type != ct_event_basic_block &&
            npe->event_type != ct_event_basic_block_info && 
            npe->event_type != ct_event_loop_enter && 
            npe->event_type != ct_event_loop_short && 
            npe->event_type != ct_event_loop_exit && 
            npe->event_type != ct_event_buffer &&
            npe->event_type != ct_event_path_info &&
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
            unsigned int id;
            if (version == 0)
            {
                fread_check(&npe->bb.basic_block_id, sizeof(unsigned int), 1, fptr);
                fread_check(&npe->bb.len, sizeof(unsigned int), 1, fptr);
            }
            else if (this->next_basic_block_id == -1)
            {
                unsigned short bbid_high = 0;
                fread_check(&bbid_high, sizeof(unsigned short), 1, fptr);
                npe->bb.basic_block_id |= (((unsigned int)bbid_high) << 7);
                if (npe->bb.basic_block_id >= bb_count)
                {
                    // Search for a matching path.
                    //   Paths are inserted based on their start IDs and then
                    //   constitute a range of IDs following that start.
                    auto pathEntry = path_info_table.lower_bound(npe->bb.basic_block_id);
                    if (pathEntry != path_info_table.end())
                    {
                        uint32_t pathID = npe->bb.basic_block_id;
                        if (pathEntry->first > pathID)
                        {
                            // Lower bound returns the entry after.
                            --pathEntry;
                        }
                        npe->bb.basic_block_id = pathEntry->second.startID;
                        
                        assert(pathEntry->second.startID != -1);
                        
                        currentPath = new internal_path_track;
                        
                        currentPath->pathBits = pathID - pathEntry->first;
                        currentPath->currentPathIndex = 0;
                        currentPath->pathInfo = &pathEntry->second;
                    }
                    else
                    {
                        fprintf(stderr, "ERROR: BBid(%d) exceeds maximum in bb_info (%d)\n", npe->bb.basic_block_id, bb_count);
                        dumpAndTerminate(fptr);
                    }
                }
                
                npe->bb.len = bb_info_table[npe->bb.basic_block_id].len;
            }
            else
            {
                this->next_basic_block_id = -1;
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
            id = npe->bb.basic_block_id;
            this->next_basic_block_id = bb_info_table[id].next_basic_block_id[0];
            if (this->next_basic_block_id != -1)
            {
                if (bb_info_table[id].next_basic_block_id[1] != -1)
                {
                    // With direction bits removed, two directions must be part of a path
                    next_basic_block_id = -1;
                    assert(currentPath != NULL);
                    /*char dir = 0;
                    fread_check(&dir, sizeof(char), 1, fptr);
                    //fprintf(stderr, "DIR - %x - (%u %u)\n", dir, bb_info_table[id].next_basic_block_id[0], bb_info_table[id].next_basic_block_id[1]);
                    if (dir & 1)
                    {
                        this->next_basic_block_id = bb_info_table[id].next_basic_block_id[1];
                    }*/
                }
            }
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
                    fprintf(stderr, "Contech memory op is larger than a long long (8 bytes)\n");
                }
                if (version == 0)
                {
                    fread_check(npe->bb.mem_op_array, sizeof(ct_memory_op), npe->bb.len, fptr);
                }
                else
                {
                    for (int i = 0; i < npe->bb.len; i++)
                    {
                        npe->bb.mem_op_array[i].data = 0;
                        
                        if ((bb_info_table[id].mem_op_info[i].memFlags & BBI_FLAG_MEM_DUP) == BBI_FLAG_MEM_DUP)
                        {
                            unsigned short dupOp = bb_info_table[id].mem_op_info[i].baseOp;
                            int64_t offset = bb_info_table[id].mem_op_info[i].baseOffset;
                            npe->bb.mem_op_array[i].addr = (npe->bb.mem_op_array[dupOp].addr) + offset;
                            
                            npe->bb.mem_op_array[i].is_write = bb_info_table[id].mem_op_info[i].memFlags & 0x1;
                            npe->bb.mem_op_array[i].pow_size = bb_info_table[id].mem_op_info[i].size;
                            
                            /*
                             * The following code verified that the duplicate memory addresses were being
                             * computed correctly.
                             *
                             * This also requires changing the driver to not omit the operations and
                             * also including the duplicate operations in the count.*/
                            /*ct_memory_op tmo;
                            tmo.data = 0;
                            fread_check(&tmo.data32[0], sizeof(unsigned int), 1, fptr);
                            fread_check(&tmo.data32[1], sizeof(unsigned short), 1, fptr);
                            
                            if (tmo.addr != npe->bb.mem_op_array[i].addr)
                            {
                                fprintf(stderr, "%d.%d\n", id, i);
                                fprintf(stderr, "%p != %p\n", tmo.addr, npe->bb.mem_op_array[i].addr);
                                fprintf(stderr, "[%d] + %d -> %p\n", dupOp, bb_info_table[id].mem_op_info[i].baseOffset, npe->bb.mem_op_array[dupOp].addr);
                                assert(0);
                            }*/
                        }
                        else if ((bb_info_table[id].mem_op_info[i].memFlags & BBI_FLAG_MEM_GV) == BBI_FLAG_MEM_GV)
                        {
                            int offset = bb_info_table[id].mem_op_info[i].baseOffset;
                            uint32_t gvid = bb_info_table[id].mem_op_info[i].constGVAddrId;
                            if (gvid >= maxConstGVId)
                            {
                                fprintf(stderr, "ERROR: Request for GV ID %d greater than max %d\n", gvid, maxConstGVId);
                                dumpAndTerminate(fptr);
                            }
                            
                            npe->bb.mem_op_array[i].addr = (constGVAddr[gvid]) + offset;
                            
                            /*
                             * The following code verified the global value elide addresses are computed
                             *   correctly.  Along with a change in the LLVM Pass to not omit these operations.
                             */
                             /*ct_memory_op tmo;
                             tmo.data = 0;
                             fread_check(&tmo.data32[0], sizeof(unsigned int), 1, fptr);
                            fread_check(&tmo.data32[1], sizeof(unsigned short), 1, fptr);
                            
                            
                            
                            if (tmo.addr != npe->bb.mem_op_array[i].addr)
                            {
                                fprintf(stderr, "%d.%d\n", id, i);
                                fprintf(stderr, "%p != %p\n", tmo.addr, npe->bb.mem_op_array[i].addr);
                                fprintf(stderr, "[%d] + %d -> %p\n", gvid, bb_info_table[id].mem_op_info[i].baseOffset, constGVAddr[gvid]);
                                assert(0);
                            }*/
                            
                            npe->bb.mem_op_array[i].is_write = bb_info_table[id].mem_op_info[i].memFlags & 0x1;
                            npe->bb.mem_op_array[i].pow_size = bb_info_table[id].mem_op_info[i].size;
                        }
                        else if ((bb_info_table[id].mem_op_info[i].memFlags & BBI_FLAG_MEM_LOOP) == BBI_FLAG_MEM_LOOP)
                        {
                            int64_t offset = bb_info_table[id].mem_op_info[i].baseOffset;
                            uint16_t loopMemOpId = bb_info_table[id].mem_op_info[i].loopMemOpId;
                            uint32_t loopId = bb_info_table[id].mem_op_info[i].headerLoopId;
                            uint8_t size = bb_info_table[id].mem_op_info[i].size;
                            
                            auto lv = loopTrack[npe->contech_id];
                            internal_loop_track* clt = NULL;
                            for (auto it = lv.rbegin(), et = lv.rend(); it != et; ++it)
                            {
                                if ((*it)->preLoopId == loopId)
                                {
                                    clt = *it;
                                    break;
                                }
                            }
                            assert(clt != NULL);
                            
                            npe->bb.mem_op_array[i].addr = ((int64_t) offset) + 
                                                           ((int64_t) bb_info_table[id].mem_op_info[i].loopIVSize) * (clt->clb.startValue) + 
                                                           clt->baseAddr[loopMemOpId];
                            
                            /*
                             * The following code verified the loop elide addresses are computed
                             *   correctly.  Along with a change in the LLVM Pass to not omit these operations.
                             */
                             /*ct_memory_op tmo;
                             tmo.data = 0;
                             fread_check(&tmo.data32[0], sizeof(unsigned int), 1, fptr);
                            fread_check(&tmo.data32[1], sizeof(unsigned short), 1, fptr);
                            
                            if (tmo.addr != npe->bb.mem_op_array[i].addr)
                            {
                                fprintf(stderr, "In loopTrack[%d] size %d:\n", npe->contech_id, lv.size());
                                fprintf(stderr, "%d.%d of loop %d.%d with %d in %d\n", id, i, loopId, loopMemOpId, clt->clb.step, clt->clb.stepBlock);
                                fprintf(stderr, "%p != %p\n", tmo.addr, npe->bb.mem_op_array[i].addr);
                                fprintf(stderr, "%p[%d * %ld] + %d -> %p\n", clt->baseAddr[loopMemOpId], 
                                                                            bb_info_table[id].mem_op_info[i].loopIVSize, 
                                                                            clt->clb.startValue, 
                                                                            offset, 
                                                                            npe->bb.mem_op_array[i].addr);
                                assert(0);
                            }*/
                            
                            npe->bb.mem_op_array[i].is_write = bb_info_table[id].mem_op_info[i].memFlags & 0x1;
                            npe->bb.mem_op_array[i].pow_size = size;
                        }
                        else
                        {
                            //fread_check(&npe->bb.mem_op_array[i].data32[0], sizeof(unsigned int), 1, fptr);
                            //fread_check(&npe->bb.mem_op_array[i].data32[1], sizeof(unsigned short), 1, fptr);
                            readMemOp(&npe->bb.mem_op_array[i], fptr);
                            
                            npe->bb.mem_op_array[i].is_write = bb_info_table[id].mem_op_info[i].memFlags & 0x1;
                            npe->bb.mem_op_array[i].pow_size = bb_info_table[id].mem_op_info[i].size;
                        }
                    }
                }
            }
            else 
            {
                npe->bb.mem_op_array = NULL;
            }
            
            auto lb = loopBlock[npe->contech_id].find(npe->bb.basic_block_id);
            if (lb != loopBlock[npe->contech_id].end())
            {
                auto clt = lb->second.back();
                clt->clb.startValue += clt->clb.step;
            }
        }
        break;
        
        case (ct_event_basic_block_info):
        {
            unsigned int id, len, line;
            int32_t nbi;
            char* tStr = NULL;
            fread_check(&id, sizeof(unsigned int), 1, fptr);
            if (id >= bb_count)
            {
                fprintf(stderr, "ERROR: INFO for block %d exceeds number of unique basic blocks (%d)\n", id, bb_count);
                dumpAndTerminate(fptr);
            }
            npe->bbi.basic_block_id = id;
            
            fread_check(&nbi, sizeof(int32_t), 1, fptr);
            npe->bbi.next_basic_block_id[0] = nbi;
            bb_info_table[id].next_basic_block_id[0] = nbi;
            fread_check(&nbi, sizeof(int32_t), 1, fptr);
            npe->bbi.next_basic_block_id[1] = nbi;
            bb_info_table[id].next_basic_block_id[1] = nbi;
            
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
                    fprintf(stderr, "ERROR: Failed to allocate %lu bytes for function name\n", sizeof(char) * (len + 1));
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
                    fprintf(stderr, "ERROR: Failed to allocate %lu bytes for file name\n", sizeof(char) * (len + 1));
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
                    fprintf(stderr, "ERROR: Failed to allocate %lu bytes for called function name\n", sizeof(char) * (len + 1));
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
            
            bool loopEntry = false;
            fread_check(&loopEntry, sizeof(bool), 1, fptr);
            if (loopEntry == false)
            {
                bb_info_table[id].loopStepBlock = -1;
            }
            else
            {
                fread_check(&bb_info_table[id].loopStepBlock, sizeof(int), 1, fptr);
                fread_check(&bb_info_table[id].loopStepValue, sizeof(unsigned int), 1, fptr);
                //printf("%d has %d of %d\n", id, bb_info_table[id].loopStepBlock, bb_info_table[id].loopStepValue);
            }
            
            fread_check(&len, sizeof(unsigned int), 1, fptr);
            bb_info_table[id].len = len;
            npe->bbi.num_mem_ops = len;
            
            //fprintf(stderr, "Store INFO [%d].len = %d\n", id, len);
            
            if (len > 0)
            {
                bb_info_table[id].mem_op_info = (pinternal_memory_op_info) malloc(sizeof(internal_memory_op_info) * len);
                assert(bb_info_table[id].mem_op_info != NULL);

                for (int i = 0; i < len; i++)
                {
                    fread_check(&bb_info_table[id].mem_op_info[i], sizeof(char), 2, fptr);
                    if ((bb_info_table[id].mem_op_info[i].memFlags & BBI_FLAG_MEM_DUP) == BBI_FLAG_MEM_DUP ||
                        (bb_info_table[id].mem_op_info[i].memFlags & BBI_FLAG_MEM_GV) == BBI_FLAG_MEM_GV ||
                        (bb_info_table[id].mem_op_info[i].memFlags & BBI_FLAG_MEM_LOOP) == BBI_FLAG_MEM_LOOP)
                    {
                        if ((bb_info_table[id].mem_op_info[i].memFlags & BBI_FLAG_MEM_GV) == BBI_FLAG_MEM_GV ||
                            (bb_info_table[id].mem_op_info[i].memFlags & BBI_FLAG_MEM_LOOP) == BBI_FLAG_MEM_LOOP)
                        {
                            // The global value flag also sets the dup flag in the info, clear now.
                            bb_info_table[id].mem_op_info[i].memFlags &= ~(BBI_FLAG_MEM_DUP);
                        }
                        
                        if ((bb_info_table[id].mem_op_info[i].memFlags & BBI_FLAG_MEM_LOOP) == BBI_FLAG_MEM_LOOP)
                        {
                            fread_check(&bb_info_table[id].mem_op_info[i].loopIVSize, sizeof(int), 1, fptr);
                            fread_check(&bb_info_table[id].mem_op_info[i].headerLoopId, sizeof(uint32_t), 1, fptr);
                            fread_check(&bb_info_table[id].mem_op_info[i].loopMemOpId, sizeof(unsigned short), 1, fptr);
                            fread_check(&bb_info_table[id].mem_op_info[i].baseOffset, sizeof(int64_t), 1, fptr);
                            if (bb_info_table[id].mem_op_info[i].headerLoopId >= bb_count)
                            {
                                fprintf(stderr, "ERROR: Loop INFO for memop %d in block %d wants block %d exceeds number of unique basic blocks (%d)\n", i, id, bb_info_table[id].mem_op_info[i].headerLoopId, bb_count);
                                dumpAndTerminate(fptr);
                            }
                        }
                        else
                        {
                            fread_check(&bb_info_table[id].mem_op_info[i].baseOp, sizeof(unsigned short), 1, fptr);
                            fread_check(&bb_info_table[id].mem_op_info[i].baseOffset, sizeof(int64_t), 1, fptr);
                        }
                    }
                    else
                    {
                        bb_info_table[id].mem_op_info[i].baseOp = 0;
                        bb_info_table[id].mem_op_info[i].baseOffset = 0;
                    }
                }
            }
            else
            {
                bb_info_table[id].mem_op_info = NULL;
            }
            
            bb_info_table[id].count = 0;
            bb_info_table[id].totalBytes = 0;
        }
        break;
        
        case (ct_event_path_info):
        {
            const int path_size = sizeof(npe->pi.pathID) +
                                  sizeof(npe->pi.startID) +
                                  sizeof(npe->pi.depth);
            uint8_t buf[path_size];
            int bytesConsume = 0;
            
            fread_check(buf, sizeof(uint8_t), path_size, fptr);   
            bytesConsume = unpack(buf, "lll", &npe->pi.pathID, &npe->pi.startID, &npe->pi.depth);
            assert(bytesConsume == path_size);
            
            {
                internal_path_info ipi;
                ipi.startID = npe->pi.startID;
                ipi.pathDepth = npe->pi.depth;
                
                assert(ipi.pathDepth > 0 && ipi.pathDepth <= MAX_PATH_DEPTH);
                
                fread_check(ipi.pathMap, sizeof(uint32_t), ipi.pathDepth, fptr);
                
                
                path_info_table[npe->pi.pathID] = ipi;
                ipi.startID = -1;
                if (path_info_table.find(npe->pi.pathID + (1 << ipi.pathDepth)) == path_info_table.end())
                {
                    path_info_table[npe->pi.pathID + (1 << ipi.pathDepth)] = ipi; // dummy entry
                }
                printf("R: %u D: %u\n", npe->pi.pathID, npe->pi.pathID + (1 << ipi.pathDepth));
                printf("RID: %u\n", path_info_table[npe->pi.pathID].startID);
            }
        }
        break;
        
        case (ct_event_task_create):
        {
            const int create_size = sizeof(npe->tc.start_time) +
                                    sizeof(npe->tc.end_time) + 
                                    sizeof(npe->tc.other_id) +
                                    sizeof(npe->tc.approx_skew);
            uint8_t buf[create_size];
            int bytesConsume = 0;
            
            fread_check(buf, sizeof(uint8_t), create_size, fptr);
            bytesConsume = unpack(buf, "ttlp", &npe->tc.start_time, 
                                               &npe->tc.end_time, 
                                               &npe->tc.other_id, 
                                               &npe->tc.approx_skew);
            assert(bytesConsume == create_size);
            
            if (npe->tc.approx_skew != 0 ||
                npe->tc.other_id == 0)
            {
                std::vector<pinternal_loop_track> vl;
                vl.push_back(NULL);
                loopTrack[npe->contech_id] = vl;
                
                std::map< uint32_t, std::vector<pinternal_loop_track> > ml;
                loopBlock[npe->contech_id] = ml;
            }
        }
        break;
        
        case (ct_event_task_join):
        {
            const int join_size = sizeof(npe->tj.isExit) + 
                                  sizeof(npe->tj.start_time) + 
                                  sizeof(npe->tj.end_time) + 
                                  sizeof(npe->tj.other_id);
            uint8_t buf[join_size];
            int bytesConsume = 0;
            
            fread_check(buf, sizeof(uint8_t), join_size, fptr);
            bytesConsume = unpack(buf, "bttl", &npe->tj.isExit, 
                                               &npe->tj.start_time, 
                                               &npe->tj.end_time, 
                                               &npe->tj.other_id);
            assert(bytesConsume == join_size);
            
        }
        break;
        
        case (ct_event_sync):
        {
            const int sync_size = sizeof(npe->sy.start_time) + 
                                  sizeof(npe->sy.end_time) + 
                                  sizeof(npe->sy.sync_type) + 
                                  sizeof(npe->sy.sync_addr) +
                                  sizeof(npe->sy.ticketNum);
            uint8_t buf[sync_size];
            int bytesConsume = 0;
            fread_check(buf, sizeof(uint8_t), sync_size, fptr);
            bytesConsume = unpack(buf, "ttlpp", &npe->sy.start_time, 
                                                &npe->sy.end_time, 
                                                &npe->sy.sync_type, 
                                                &npe->sy.sync_addr, 
                                                &npe->sy.ticketNum);
            assert(bytesConsume == sync_size);
        }
        break;
        
        case (ct_event_barrier):
        {
            const size_t bar_size = sizeof(npe->bar.onEnter) + 
                                 sizeof(npe->bar.start_time) +
                                 sizeof(npe->bar.end_time) +
                                 sizeof(npe->bar.sync_addr) +
                                 sizeof(npe->bar.barrierNum);
            uint8_t buf[bar_size];
            int bytesConsume = 0;
            fread_check(buf, sizeof(uint8_t), bar_size, fptr);
            bytesConsume = unpack(buf, "btttt", &npe->bar.onEnter, 
                                                &npe->bar.start_time,
                                                &npe->bar.end_time, 
                                                &npe->bar.sync_addr, 
                                                &npe->bar.barrierNum);
            assert(bytesConsume == bar_size);
        }
        break;
        
        case (ct_event_memory):
        {
            const size_t mem_size = sizeof(npe->mem.isAllocate) +
                                    sizeof(npe->mem.size) +
                                    sizeof(npe->mem.alloc_addr);
            uint8_t buf[mem_size];
            int bytesConsume = 0;
            fread_check(buf, sizeof(uint8_t), mem_size, fptr);
            bytesConsume = unpack(buf, "btt", &npe->mem.isAllocate,
                                              &npe->mem.size,
                                              &npe->mem.alloc_addr);
            assert(bytesConsume == mem_size);
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
            if (maxBufPos == 0) initBufList(fptr);
            
            // If the next buffer is valid, keep reading sequentially
            auto ss = skipSet.find(npe->contech_id);
            if ((ss == skipSet.end() || ss->second == false) &&
                skipList[npe->contech_id].size() > 0 &&
                ((ftell(fptr) - 12) == skipList[npe->contech_id].front()))
            {
                skipList[npe->contech_id].pop_front();
                //fprintf(stderr, "CONT: %ld (%u)\n", ftell(fptr) - 12, npe->contech_id);
            }
            else
            {
                free(npe);
                sum -= 12;
                
                long earliestPos = LONG_MAX;
                int zeroCount = 0;
                for (auto it = skipList.begin(), et = skipList.end(); it != et; ++it)
                {
                    ss = skipSet.find(it->first);
                    if (ss != skipSet.end() && ss->second == true) continue;
                    if (it->second.size() == 0)
                    {
                        zeroCount++;
                        //it = skipList.erase(it);
                        //--it;
                        continue;
                    }
                    long newPos = it->second.front();
                    if (newPos < earliestPos)
                    {
                        earliestPos = newPos;
                    }
                }
                
                if (earliestPos < LONG_MAX)
                {
                    fseek(fptr, earliestPos, SEEK_SET);
                    //fprintf(stderr, "Skipping to: %ld\n", earliestPos);
                    
                    return createContechEvent(fptr);
                }
                
                // IF the zero count is the number of entries, then every context
                //   has finished processing successfully.  We've reached this state
                //   when the last buffer read was not the last buffer in the file.
                //   The exit case for the last buffer in the file is at the start
                //   of this function.
                if (zeroCount == skipList.size())
                {
                    return NULL;
                }
                else
                {
                    assert(0 && "All remaining CTs buffers are blocked in event list");
                }
            }
            
            if (npe->contech_id == 0)
            {
                //fprintf(stderr, "BUF(0) - %ld\n", ftell(fptr) - 12);
            }
            if (bufSum == 0)
            {
                // Everything we've read so far, except this event (12B)
                bufSum = sum - 12;
                
            }
            else if ((sum - 12) != bufSum)
            {
                fprintf(stderr, "Marker at %lu bytes, should be at 12 + %lu\n", sum, bufSum);
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
            const size_t bulk_size = sizeof(npe->bm.size) +
                                     sizeof(npe->bm.dst_addr) +
                                     sizeof(npe->bm.src_addr);
            uint8_t buf[bulk_size];
            int bytesConsume = 0;
            fread_check(buf, sizeof(uint8_t), bulk_size, fptr);
            bytesConsume = unpack(buf, "ttt", &npe->bm.size,
                                              &npe->bm.dst_addr,
                                              &npe->bm.src_addr);
                                              
            assert(bytesConsume == bulk_size);
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
            const size_t mpi_size = sizeof(npe->mpixf.isSend) +
                                    sizeof(npe->mpixf.isBlocking) +
                                    sizeof(npe->mpixf.comm_rank) +
                                    sizeof(npe->mpixf.tag) +
                                    sizeof(npe->mpixf.buf_ptr) +
                                    sizeof(npe->mpixf.buf_size) +
                                    sizeof(npe->mpixf.start_time) +
                                    sizeof(npe->mpixf.end_time) +
                                    sizeof(npe->mpixf.req_ptr);
            uint8_t buf[mpi_size];
            int bytesConsume = 0;
            fread_check(buf, sizeof(uint8_t), mpi_size, fptr);
            bytesConsume = unpack(buf, "cclltsttt", &npe->mpixf.isSend, 
                                                    &npe->mpixf.isBlocking, 
                                                    &npe->mpixf.comm_rank, 
                                                    &npe->mpixf.tag,
                                                    &npe->mpixf.buf_ptr, 
                                                    &npe->mpixf.buf_size, 
                                                    &npe->mpixf.start_time, 
                                                    &npe->mpixf.end_time, 
                                                    &npe->mpixf.req_ptr);
            assert(bytesConsume == mpi_size);
        }
        break;
        
        case (ct_event_mpi_allone):
        {
            const size_t mpi_size = sizeof(npe->mpiao.isToAll) +
                                    sizeof(npe->mpiao.one_comm_rank) +
                                    sizeof(npe->mpiao.buf_ptr) +
                                    sizeof(npe->mpiao.buf_size) +
                                    sizeof(npe->mpiao.start_time) +
                                    sizeof(npe->mpiao.end_time);
            uint8_t buf[mpi_size];
            int bytesConsume = 0;
            fread_check(buf, sizeof(uint8_t), mpi_size, fptr);
            bytesConsume = unpack(buf, "cltstt", &npe->mpiao.isToAll,
                                           &npe->mpiao.one_comm_rank,
                                           &npe->mpiao.buf_ptr,
                                           &npe->mpiao.buf_size,
                                           &npe->mpiao.start_time,
                                           &npe->mpiao.end_time);
            assert(bytesConsume == mpi_size);
        }
        break;
        
        case (ct_event_mpi_wait):
        {
            fread_check(&npe->mpiw.req_ptr, sizeof(ct_addr_t), 1, fptr);
            fread_check(&npe->mpiw.start_time, sizeof(ct_tsc_t), 1, fptr);
            fread_check(&npe->mpiw.end_time, sizeof(ct_tsc_t), 1, fptr);
        }
        break;
        
        case (ct_event_gv_info):
        {
            fread_check(&npe->gvi.id, sizeof(npe->gvi.id), 1, fptr);
            fread_check(&npe->gvi.constantGV, sizeof(npe->gvi.constantGV), 1, fptr);
            //fprintf(stderr, "%d %lx %d %d\n", npe->gvi.id, npe->gvi.constantGV, sizeof(npe->gvi.id), sizeof(npe->gvi.constantGV));
            if (constGVAddr == NULL)
            {
                constGVAddr = (ct_addr_t*) malloc(sizeof(uint64_t) * (npe->gvi.id + 1));
                maxConstGVId = npe->gvi.id + 1;
            }
            else if (npe->gvi.id >= maxConstGVId)
            {
                fprintf(stderr, "ERROR: TODO\n");
                dumpAndTerminate(fptr);
            }
            constGVAddr[npe->gvi.id] =  npe->gvi.constantGV;
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
            fread_check(&npe->roi.start_time, sizeof(ct_tsc_t), 1, fptr);
        }
        break;
        
        case (ct_event_loop_enter):
        {
            const int loop_size = sizeof(npe->loop.preLoopId);
            uint8_t buf[loop_size];
            int bytesConsume = 0;
            fread_check(buf, sizeof(uint8_t), loop_size, fptr);
            bytesConsume = unpack(buf, "l", &npe->loop.preLoopId);
            assert(bytesConsume == loop_size);
            
            auto lv = loopTrack.find(npe->contech_id);
            assert(lv != loopTrack.end());
            if (lv == loopTrack.end())
            {
                dumpAndTerminate(fptr);
            }
            
            // Loop start and end events are slightly different.
            const int loop_start_size = sizeof(npe->loop.clb.startValue);
            uint8_t bufS[loop_start_size];
            fread_check(bufS, sizeof(uint8_t), loop_start_size, fptr);
            bytesConsume = unpack(bufS, "t", &npe->loop.clb.startValue);
                                                 
            npe->loop.clm.memOpId = 0;
            npe->loop.clb.step = bb_info_table[npe->loop.preLoopId].loopStepValue;
            npe->loop.clb.stepBlock = bb_info_table[npe->loop.preLoopId].loopStepBlock;
            
            ct_memory_op pmo;
            readMemOp(&pmo, fptr);
            npe->loop.clm.baseAddr = pmo.data;
            
            internal_loop_track* clt = lv->second.back();
            if (clt == NULL || 
                clt->preLoopId != npe->loop.preLoopId ||
                npe->loop.clm.memOpId == 0)
            {
                clt = new internal_loop_track;
                clt->clb = npe->loop.clb;
                clt->preLoopId = npe->loop.preLoopId;
                lv->second.push_back(clt);
                loopBlock[npe->contech_id][clt->clb.stepBlock].push_back(clt);
            }
            
            // resize will not shrink
            if (clt->baseAddr.size() <= npe->loop.clm.memOpId)
            {
                clt->baseAddr.resize(npe->loop.clm.memOpId + 1);
            }
            clt->baseAddr[npe->loop.clm.memOpId] = npe->loop.clm.baseAddr;
        }
        break;
        
        case ct_event_loop_short:
        {
            int bytesConsume = 0;
            auto lv = loopTrack.find(npe->contech_id);
            assert(lv != loopTrack.end());
            if (lv == loopTrack.end())
            {
                dumpAndTerminate(fptr);
            }
            
            internal_loop_track* clt = lv->second.back();
            
            /*const int loop_start_size = sizeof(npe->loop.clm.memOpId);
            uint8_t bufS[loop_start_size];
            fread_check(bufS, sizeof(uint8_t), loop_start_size, fptr);
            bytesConsume = unpack(bufS, "s", &npe->loop.clm.memOpId);*/
                                              
            ct_memory_op pmo;
            readMemOp(&pmo, fptr);
            npe->loop.clm.baseAddr = pmo.data;
            
            // resize will not shrink
            /*if (clt->baseAddr.size() <= npe->loop.clm.memOpId)
            {
                clt->baseAddr.resize(npe->loop.clm.memOpId + 1);
            }
            clt->baseAddr[npe->loop.clm.memOpId] = npe->loop.clm.baseAddr;*/
            clt->baseAddr.push_back(npe->loop.clm.baseAddr);
        }
        break;
        
        case ct_event_loop_exit:
        {
            const int loop_size = sizeof(npe->loop.preLoopId);
            uint8_t buf[loop_size];
            int bytesConsume = 0;
            fread_check(buf, sizeof(uint8_t), loop_size, fptr);
            bytesConsume = unpack(buf, "l", &npe->loop.preLoopId);
            assert(bytesConsume == loop_size);
            
            auto lv = loopTrack.find(npe->contech_id);
            assert(lv != loopTrack.end());
            if (lv == loopTrack.end())
            {
                dumpAndTerminate(fptr);
            }
            
            // Loop start and end events are slightly different.
            internal_loop_track* clt = lv->second.back();
            if (clt->preLoopId != npe->loop.preLoopId)
            {
                printf("In %d, loop %d was instead %d\n", lastBBID, npe->loop.preLoopId, clt->preLoopId);
            }
            assert(clt->preLoopId == npe->loop.preLoopId);
            lv->second.pop_back();
            loopBlock[npe->contech_id][clt->clb.stepBlock].pop_back();
            delete clt;
        }
        break;
        
        default:
        {
            fprintf(stderr, "ERROR: type %d not supported at %lu\n", npe->event_type, sum);
            fprintf(stderr, "\tPrevious event - %d with ID - %d\n", lastType, lastID);
            dumpAndTerminate(fptr);
        }
        break;
    }
    
    // If this is a basic block, then record all of the prior space
    if (npe->event_type == ct_event_basic_block)
    {
        if (lastBBIDPos > 0)
        {
            if (bb_info_table[lastBBID].totalBytes == 0)
            {
                bb_info_table[lastBBID].totalBytes = startSum - lastBBIDPos;
            }
        }
        if (lastBBID < bb_count)
        {
            bb_info_table[lastBBID].count++;
        }
        
        lastBBIDPos = startSum;
    }
    else if (npe->event_type == ct_event_delay ||
             npe->event_type == ct_event_buffer)
    {
        // Do not record space across artificial events
        lastBBIDPos = 0;
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
    else if (npe->event_type == ct_event_basic_block_info)
    {
        ced[cedPos].data0 = npe->bbi.basic_block_id;
        ced[cedPos].data1 = npe->bbi.num_mem_ops;
    }
    else
    {
        ced[cedPos].data0 = npe->mem.isAllocate;
        ced[cedPos].data1 = npe->sy.ticketNum;
    }

    if (sum > bufSum && bufSum > 0) 
    {
        fprintf(stderr, "ERROR: Missing buffer event at %lx.  Should be after %d bytes.\n", sum, lastBufPos);
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
        if (e->bbi.callFun_name != NULL) free(e->bbi.callFun_name);
    }    
    free(e);
}

void EventLib::dumpAndTerminate(FILE *fh)
{
    struct stat buf;
    char d = 0;
    fstat(fileno(fh), &buf);
    fprintf(stderr, "%p - %d - %d - %lx - %ld - %lx\n", 
                    (void*)fh, ferror(fh), feof(fh), ftell(fh), fread(&d, 1, 1, fh), buf.st_size);
    displayContechEventDebugInfo();
    assert(0);
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
    fprintf(stderr, "Consumed %lu bytes, in buffer of %d to %lu\n", sum, lastBufPos, bufSum);
    fprintf(stderr, "\tOFF(ty(id) - data0 data 1\n");
    for (i = cedPos; i >= 0; i--)
    {
        fprintf(stderr, "\t0x%x(%d(%d) - %d %d)\n", ced[i].sum, ced[i].type, ced[i].id, ced[i].data0, ced[i].data1);
    }
    for (i = 64 - 1; i > cedPos; i--)
    {
        fprintf(stderr, "\t0x%x(%d(%d) - %d %d)\n", ced[i].sum, ced[i].type, ced[i].id, ced[i].data0, ced[i].data1);
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

void EventLib::debugSkipStatus()
{
    for (auto it = skipList.begin(), et = skipList.end(); it != et; ++it)
    {
        fprintf(stderr, "CTID: %d\tBlock: %d\tPos: %ld\n", it->first, skipSet[it->first], it->second.front());
    }
}

void EventLib::initBufList(FILE* fptr)
{
    uint64_t resetSum = sum;
    long firstBufPos = ftell(fptr);
    long fileLen = 0;
    uint32_t buf[3];
    
    fseek(fptr, 0, SEEK_END);
    fileLen = ftell(fptr);
    fseek(fptr, firstBufPos - 12, SEEK_SET);
    
    while (1)
    {
        long curPos = ftell(fptr);
        fread_check(buf, sizeof(uint32_t), 3, fptr);
        assert(buf[0] == ct_event_buffer);
        uint32_t ctid = buf[1];
        uint32_t bufLen = buf[2];
        
        skipList[ctid].push_back(curPos);
        
        if ((curPos + (sizeof(uint32_t) * 3) + (long)bufLen) >= fileLen) break;
        fseek(fptr, bufLen, SEEK_CUR);
    }
    
    fseek(fptr, firstBufPos, SEEK_SET);
    sum = resetSum;
    maxBufPos = 1;
}

void EventLib::blockCTID(FILE* fptr, uint32_t ctid)
{
    skipSet[ctid] = true;
}

void EventLib::unblockCTID(uint32_t ctid)
{
    skipSet[ctid] = false;
}

bool EventLib::getBlockCTID(uint32_t ctid)
{
    return skipSet[ctid];
}
