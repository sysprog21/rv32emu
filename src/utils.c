#include <stdint.h>
#include <sys/time.h>
#include <time.h>

#include "utils.h"

#if defined(__APPLE__)
#define HAVE_MACH_TIMER
#include <mach/mach_time.h>
#elif !defined(_WIN32) && !defined(_WIN64)
#define HAVE_POSIX_TIMER
#ifdef CLOCK_MONOTONIC
#define CLOCKID CLOCK_MONOTONIC
#else
#define CLOCKID CLOCK_REALTIME
#endif
#endif

void rv_gettimeofday(struct timeval *tv)
{
#if defined(HAVE_POSIX_TIMER)
    struct timespec t;
    clock_gettime(CLOCKID, &t);
    int32_t tv_sec = t.tv_sec;
    int32_t tv_usec = t.tv_nsec / 1000;
#elif defined(HAVE_MACH_TIMER)
    static mach_timebase_info_data_t info;
    /* If this is the first time we have run, get the timebase.
     * We can use denom == 0 to indicate that sTimebaseInfo is
     * uninitialized.
     */
    if (info.denom == 0)
        (void) mach_timebase_info(&info);
    /* Hope that the multiplication doesn't overflow. */
    uint64_t nsecs = mach_absolute_time() * info.numer / info.denom;
    int32_t tv_sec = nsecs / 1e9;
    int32_t tv_usec = (nsecs / 1e3) - (tv_sec * 1e6);
#else /* low resolution timer */
    clock_t t = clock();
    int32_t tv_sec = t / CLOCKS_PER_SEC;
    int32_t tv_usec = (t % CLOCKS_PER_SEC) * (1000000 / CLOCKS_PER_SEC);
#endif
    tv->tv_sec = tv_sec;
    tv->tv_usec = tv_usec;
}
