#ifndef CT_EVENT_H
#define CT_EVENT_H

#include "../taskLib/ct_file.h"
#include "ct_event_st.h"
#include <stdint.h>
#include <stdarg.h>

namespace contech
{
    class EventLib
    {
        private:
            // DEBUG information
            unsigned long long sum ;
            unsigned long long bufSum ;
            unsigned int lastBufPos ;
            unsigned int lastID ;
            unsigned int lastBBID ;
            unsigned int lastType ;
            
            typedef struct _ct_event_debug
            {
                unsigned int sum, id, type;
                unsigned int data0, data1;
            } ct_event_debug, *pct_event_debug;
            
            ct_event_debug  ced[64];

            unsigned int cedPos ;

            unsigned int binInfo[1024];

            FILE* debug_file;

            // Per 9/17/13, event list will now contain a version event
            //   This will help with detecting compatibility issues
            unsigned int version ;

            // In version 1, we get currentID from the header events, instead
            // of from the individual events
            unsigned int currentID;

            // Interpret basic blocks using the following information
            unsigned int bb_count;
            
            typedef struct _internal_memory_op_info
            {
                char isWrite, size;
            } internal_memory_op_info, *pinternal_memory_op_info;

            typedef struct _internal_basic_block_info
            {
                unsigned int len;
                pinternal_memory_op_info mem_op_info;
            } internal_basic_block_info, *pinternal_basic_block_info;

            pinternal_basic_block_info bb_info_table;
            
            int unpack(uint8_t *buf, char *fmt, ...);
            void dumpAndTerminate(ct_file *fptr);
    
        public:
            EventLib();
            pct_event createContechEvent(ct_file*);
            static void deleteContechEvent(pct_event);
            void displayContechEventDebugInfo();
            void displayContechEventDiagInfo();
            void displayContechEventStats();
            void resetEventLib();
    };
}

#endif

