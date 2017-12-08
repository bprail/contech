#include "eventQ.hpp"
#include "../common/taskLib/Task.hpp"
#include "../common/eventLib/ct_event.h"
#include <map>
#include <deque>

using namespace std;
using namespace contech;

void eventDebugPrint(TaskId first, string verb, TaskId second, ct_tsc_t start, ct_tsc_t end)
{
    cerr << start << " - " << end << ": ";
    cerr << first << " " << verb << " " << second << endl;
}

EventQ::EventQ()
{
    currentTrace = traces.begin();
    totalSpace = 0;
}

EventQ::~EventQ()
{
    for (auto it = traces.begin(), et = traces.end(); it != et; ++it)
    {
        fclose((*it)->file);
        delete *it;
    }
}

void EventQ::printSpaceTime(ct_tsc_t tcyc)
{
    cerr << "Middle Space Time: " << ((double)totalSpace) / tcyc << endl;
}

void EventQ::registerEventList(FILE* f)
{
    traces.push_back(new EventList(f));
}

void EventQ::readyEvents(int rank, unsigned int context)
{
    for (auto it = traces.begin(), et = traces.end(); it != et; ++it)
    {
        if ((*it)->mpiRank == rank)
        {
            (*it)->readyEvents(context);
        }
    }
}

pct_event EventQ::getNextContechEvent(int* rank)
{
    pct_event event = NULL;
    *rank = -1;
    
    while (!traces.empty() && event == NULL)
    {
        event = (*currentTrace)->getNextContechEvent();
        
        if (event == NULL)
        {
            fclose((*currentTrace)->file);
            totalSpace += (*currentTrace)->getSpace();
            delete *currentTrace;
            currentTrace = traces.erase(currentTrace);
        }
        else
        {
            *rank = (*currentTrace)->mpiRank;
            ++currentTrace;
        }
        if (currentTrace == traces.end()) currentTrace = traces.begin();
    }
    
    return event;
}

EventList::EventList(FILE* f)
{
    file = f;
    el = new EventLib;
    currentQueuedCount = 0;
    maxQueuedCount = 0;
    barrierNum = 0;
    ticketNum = 0;
    minQueuedTicket = 0;
    resetMinTicket = false;
    mpiRank = 0;
    eventQueueCurrent = queuedEvents.begin();
}

EventList::~EventList()
{
    if (el != NULL)
    {
        delete el;
        el = NULL;
    }
}

uint64_t EventList::getSpace()
{
    return el->getSum();
}

void EventList::rescanMinTicket()
{
    for (auto it = queuedEvents.begin(), et = queuedEvents.end(); it != et; ++it)
    {
        pct_event event = it->second.front();
        if (event->event_type == ct_event_sync)
        {
            printf("%u: on ticket %lu\n", event->contech_id, event->sy.ticketNum);
            if (event->sy.ticketNum < minQueuedTicket)
            {
                minQueuedTicket = event->sy.ticketNum;
            }
        }
    }
}

void EventList::rescanMinTicketDeep()
{
    for (auto it = queuedEvents.begin(), et = queuedEvents.end(); it != et; ++it)
    {
        uint64_t tNum = 0;
        pct_event tevent = NULL;
        
        for (auto ivt = it->second.begin(), evt = it->second.end(); ivt != evt; ++ivt)
        {
            pct_event event = *ivt;
            
            if (event->event_type == ct_event_sync)
            {
               
                if (event->sy.ticketNum < tNum)
                {
                     printf("%u: non ticket on %lu, as < %lu of %p\n", event->contech_id, 
                                                                       event->sy.ticketNum,
                                                                       tNum,
                                                                      (void*)tevent);
                     assert(0);
                }
                else
                {
                    tNum = event->sy.ticketNum;
                    tevent = event;
                }
            }
        }
    }
}

void EventList::barrierTicket()
{
    for (auto it = queuedEvents.begin(), et = queuedEvents.end(); it != et; ++it)
    {
        pct_event event = it->second.front();
        if (event->event_type == ct_event_barrier)
        {
            printf("%u: on ticket %lu\n", event->contech_id, event->bar.barrierNum);
        }
    }
}

pct_event EventList::getNextContechEvent()
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
        if (ticketNum < minQueuedTicket && resetMinTicket == false) break;
        if (eventQueueCurrent->second.empty())
        {
            auto t = eventQueueCurrent;
            ++eventQueueCurrent;
            queuedEvents.erase(t);
            // While this loops, the main loop guarantees that there will be at least one
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
        if (event->event_type == ct_event_rank)
        {
            mpiRank = event->rank.rank;
            EventLib::deleteContechEvent(event);
        }
        else if (event->event_type == ct_event_barrier)
        {
            // Barriers have ordering numbers too
            if (event->bar.barrierNum == barrierNum)
            {
                el->unblockCTID(event->contech_id);
                barrierNum++;
                eventQueueCurrent->second.pop_front();
                eventQueueCurrent = queuedEvents.begin();
                assert(currentQueuedCount > 0);
                currentQueuedCount--;
                return event;
            }
            else
            {
                ++eventQueueCurrent;
                if (eventQueueCurrent == queuedEvents.end())
                {
                    if (resetMinTicket == true)
                    {
                        resetMinTicket = false;
                    }
                    else
                    {
                        resetMinTicket = true;
                    }
                    eventQueueCurrent = queuedEvents.begin();
                    break;
                }
            }
        }
        else if (event->event_type != ct_event_sync)
        {
            eventQueueCurrent->second.pop_front();
            assert(currentQueuedCount > 0);
            currentQueuedCount--;
            return event;
        }
        else if (event->sy.ticketNum == ticketNum)
        {
            el->unblockCTID(event->contech_id);
            
            // This is the next ticket
            ticketNum++;
            eventQueueCurrent->second.pop_front();
            eventQueueCurrent = queuedEvents.begin();
            assert(currentQueuedCount > 0);
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
        event = el->createContechEvent(file);
        if (event == NULL) return NULL;
        if (queuedEvents.find(event->contech_id) != queuedEvents.end())
        {
            queuedEvents[event->contech_id].push_back(event);
            currentQueuedCount++;
            if (currentQueuedCount > maxQueuedCount) maxQueuedCount = currentQueuedCount;
            continue;
        }
        else if (waitingEvents.find(event->contech_id) != waitingEvents.end())
        {
            if (waitingEvents[event->contech_id].front() == NULL)// if head is NULL, then clear queue
            {
                ;
            }
            else
            {
                waitingEvents[event->contech_id].push_back(event);
                continue;
            }
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
                el->blockCTID(file, event->contech_id);
                
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
                event = getNextContechEvent();
            }
            else {
                //printf("Ticket:%llu %d, %u\n", event->sy.ticketNum, queuedEvents.size(), event->contech_id);
                ticketNum ++;
            }
            break;
        }
        case ct_event_barrier:
        {
            if (event->bar.barrierNum > barrierNum)
            {
                el->blockCTID(file, event->contech_id);
                
                queuedEvents[event->contech_id].push_back(event);
                eventQueueCurrent = queuedEvents.begin();
                resetMinTicket = true;
                currentQueuedCount++;
                if (currentQueuedCount > maxQueuedCount) maxQueuedCount = currentQueuedCount;
                event = getNextContechEvent();
            }
            else
            {
                barrierNum++;
            }
        }
        break;
        case ct_event_task_create:
        {
            // if approx_skew != 0, then this is the child (i.e. created context)
            if (event->tc.approx_skew != 0)
            {
                if (waitingEvents.find(event->contech_id) != waitingEvents.end() &&
                    waitingEvents[event->contech_id].front() == NULL)
                {
                    waitingEvents.erase(event->contech_id);
                }
                else
                {
                    waitingEvents[event->contech_id].push_back(event);
                    event = getNextContechEvent();
                }
            }
        }
        break;
        
        // NB Optimizing compiler may point to this getNextContechEvent() even if it is from one of the other cases
        case ct_event_rank:
        {
            mpiRank = event->rank.rank;
            EventLib::deleteContechEvent(event);
            event = getNextContechEvent();
        }
        break;
        default:
            break;
    }

    return event;
}

void EventList::readyEvents(unsigned int context)
{
    auto deq = waitingEvents.find(context);
    
    if (deq == waitingEvents.end())
    {
        // This case is when the creator is before the create
        waitingEvents[context].push_back(NULL);
    }
    else
    {
        currentQueuedCount += deq->second.size();
        if (currentQueuedCount > maxQueuedCount) maxQueuedCount = currentQueuedCount;
        queuedEvents[context] = deq->second;
        waitingEvents.erase(deq);
        
        // As the queues may go from empty to non-empty, the iterator needs to be initialized here
        eventQueueCurrent = queuedEvents.begin();
    }
}
