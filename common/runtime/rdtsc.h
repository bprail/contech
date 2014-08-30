#ifndef __RDTSC_H_DEFINED__
#define __RDTSC_H_DEFINED__
#include <stdint.h>

#if defined(__i386__)

static __inline__ uint64_t rdtsc(void)
{
  uint64_t x;
     __asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
     return x;
}
#elif defined(__x86_64__)

static __inline__ uint64_t rdtsc(void)
{
  uint32_t hi, lo;
  __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
  return ( (uint64_t)lo)|( ((uint64_t)hi)<<32 );
}

#elif defined(__powerpc__)

static __inline__ uint64_t rdtsc(void)
{
  uint64_t result=0;
  uint32_t upper, lower,tmp;
  __asm__ volatile(
                "0:                  \n"
                "\tmftbu   %0           \n"
                "\tmftb    %1           \n"
                "\tmftbu   %2           \n"
                "\tcmpw    %2,%0        \n"
                "\tbne     0b         \n"
                : "=r"(upper),"=r"(lower),"=r"(tmp)
                );
  result = upper;
  result = result<<32;
  result = result|lower;

  return(result);
}

#elif defined (__arm__)

static __inline__ uint64_t rdtsc(void)
{
    unsigned int x = 0;
    // May not be enabled by kernel
    //   Replace with C - clock() or C++ chrono?
    __asm__ volatile ("MRC p15, 0, %0, c9, c13, 0\n\t":  "=r" (x)::);
    return x;
}

#else

#error "No tick counter is available!"

#endif


/*  $RCSfile:  $   $Author: kazutomo $
 *  $Revision: 1.6 $  $Date: 2005/04/13 18:49:58 $
 */

#endif

