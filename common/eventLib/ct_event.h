#ifndef CT_EVENT_H
#define CT_EVENT_H

#include "../taskLib/ct_file.h"
#include "ct_event_st.h"

pct_event createContechEvent(ct_file*);
void deleteContechEvent(pct_event);
void displayContechEventDebugInfo();
void displayContechEventDiagInfo();
void displayContechEventStats();
void resetEventLib();

#endif

