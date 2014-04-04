#include "eventQ.hpp"
#include "../common/taskLib/Task.hpp"
#include "../common/eventLib/ct_event.h"
#include <map>
#include <deque>

using namespace std;
using namespace contech;

unsigned int currentQueuedCount = 0;
unsigned int maxQueuedCount = 0;

void eventDebugPrint(TaskId first, string verb, TaskId second, ct_tsc_t start, ct_tsc_t end)
{
    cerr << start << " - " << end << ": ";
    cerr << first << " " << verb << " " << second << endl;
}

unsigned long long ticketNum = 0;
unsigned long long minQueuedTicket = 0;
bool resetMinTicket = false;
map <unsigned int, deque <pct_event> > queuedEvents;
map <unsigned int, deque <pct_event> >::iterator eventQueueCurrent;
pct_event getNextContechEvent(ct_file* inFile)
{
    bool nextEvent = false;
    pct_event event = NULL;
    
    //
    // This loop checks if any of the queues of events can provide the next event.
    //   Each queue is either blocked on a ticketed event, or is unblocked.
    //
    unsigned long long currMinTicket = ~0;
    while (!queuedEvents.empty())
    {
        // Fast check whether a queued event may be removed.
        if (ticketNum < minQueuedTicket) break;
        if (eventQueueCurrent->second.empty())
        {
            auto t = eventQueueCurrent;
            ++eventQueueCurrent;
            queuedEvents.erase(t);
            // While this loops, the main loop guarentees that there will be at least one
            //   queue with events.
            if (eventQueueCurrent == queuedEvents.end())
            {
                eventQueueCurrent = queuedEvents.begin();
            }
            
            continue;
        }
        event = eventQueueCurrent->second.front();
        //
        // Currently, only syncs are blocking in the event queues, so any other type of
        //   event is clear to be returned.  If the event is a sync, then it is only
        //   clear when it is the next ticket number.
        //
        if (event->event_type != ct_event_sync)
        {
            eventQueueCurrent->second.pop_front();
            currentQueuedCount--;
            return event;
        }
        else if (event->sy.ticketNum == ticketNum)
        {
            // This is the next ticket
            ticketNum++;
            eventQueueCurrent->second.pop_front();
            eventQueueCurrent = queuedEvents.begin();
            currentQueuedCount--;
            return event;
        }
        else
        {
            // No valid events at this queue position
            ++eventQueueCurrent;
            
            // Is this the lowest ticket we've seen so far
            if (event->sy.ticketNum < currMinTicket) currMinTicket = event->sy.ticketNum;
            
            // End of the queue, next request should start over
            if (eventQueueCurrent == queuedEvents.end())
            {
                // If true, then this was likely a single loop through each queue
                //   to find the new minimum ticket number.
                if (resetMinTicket == true)
                {
                    resetMinTicket = false;
                    minQueuedTicket = currMinTicket;
                }
                else
                {
                    resetMinTicket = true;
                    minQueuedTicket = 0;
                }
                eventQueueCurrent = queuedEvents.begin();
                break;
            }
        }
    }
    
    //
    // Get events from the file
    //
    //   Look for one that is not blocked.
    //
    while (!nextEvent)
    {
        event = createContechEvent(inFile);
        if (event == NULL) return NULL;
        if (queuedEvents.find(event->contech_id) != queuedEvents.end())
        {
            queuedEvents[event->contech_id].push_back(event);
            currentQueuedCount++;
            if (currentQueuedCount > maxQueuedCount) maxQueuedCount = currentQueuedCount;
            continue;
        }
        nextEvent = true;
    }
    
    // Leave the switch in case other types need to be checked in the future
    //   We assume that the compiler can convert this into an if / else
    switch (event->event_type)
    {
        case ct_event_sync:
        {
            if (event->sy.ticketNum > ticketNum)
            {
                //printf("Delay :%llu %d %d\n", event->sy.ticketNum, event->contech_id, queuedEvents.size());
                
                queuedEvents[event->contech_id].push_back(event);
                eventQueueCurrent = queuedEvents.begin();
                resetMinTicket = true;
                minQueuedTicket = 0;
                currentQueuedCount++;
                if (currentQueuedCount > maxQueuedCount) maxQueuedCount = currentQueuedCount;
                // Yes, recursion
                //   This should only happen a limited number of times
                //   At most N-1, where N is the number of contexts and the next N-2 events
                //   are all ticketed events that must be queued.
                event = getNextContechEvent(inFile);
            }
            else {
                //printf("Ticket:%llu %d\n", event->sy.ticketNum, queuedEvents.size());
                ticketNum ++;
            }
            break;
        }
        default:
            break;
    }

    return event;
}
