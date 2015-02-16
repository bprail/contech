#ifndef EVENT_Q_H
#define EVENT_Q_H

#include "../common/taskLib/Task.hpp"
#include "../common/eventLib/ct_event.h"
#include <map>
#include <deque>

namespace contech {

    class EventList 
    {
        private:
        EventLib* el;
        
        unsigned long int currentQueuedCount ;
        unsigned long int maxQueuedCount ;
        unsigned long long ticketNum ;
        unsigned long long minQueuedTicket ;
        bool resetMinTicket;
        
        map <unsigned int, deque <pct_event> > queuedEvents;
        map <unsigned int, deque <pct_event> > waitingEvents;
        map <unsigned int, deque <pct_event> >::iterator eventQueueCurrent;
        
        void rescanMinTicket();
        
        public:
        EventList(ct_file*);
        pct_event getNextContechEvent();
        void readyEvents(unsigned int);
        int mpiRank;
        ct_file* file;
    };

    class EventQ
    {
        private:
            
            deque <EventList*> traces;
            deque <EventList*>::iterator currentTrace;
            
            //pct_event getNextContechEvent(EventList*);
    
        public:
            EventQ();
            ~EventQ();
            pct_event getNextContechEvent(int*);
            void readyEvents(int, unsigned int);
            void registerEventList(ct_file*);
    
    };

}

#endif