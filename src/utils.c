/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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

static void get_time_info(int32_t *tv_sec, int32_t *tv_usec)
{
#if defined(HAVE_POSIX_TIMER)
    struct timespec t;
    clock_gettime(CLOCKID, &t);
    *tv_sec = t.tv_sec;
    *tv_usec = t.tv_nsec / 1000;
#elif defined(HAVE_MACH_TIMER)
    static mach_timebase_info_data_t info;
    /* If it is the first time running, obtain the timebase. Using denom == 0
     * indicates that sTimebaseInfo is uninitialized.
     */
    if (info.denom == 0)
        (void) mach_timebase_info(&info);
    /* Hope that the multiplication doesn't overflow. */
    uint64_t nsecs = mach_absolute_time() * info.numer / info.denom;
    *tv_sec = nsecs / 1e9;
    *tv_usec = (nsecs / 1e3) - (*tv_sec * 1e6);
#else /* low resolution timer */
    clock_t t = clock();
    *tv_sec = t / CLOCKS_PER_SEC;
    *tv_usec = (t % CLOCKS_PER_SEC) * (1e6 / CLOCKS_PER_SEC);
#endif
}

void rv_gettimeofday(struct timeval *tv)
{
    int32_t tv_sec, tv_usec;
    get_time_info(&tv_sec, &tv_usec);
    tv->tv_sec = tv_sec;
    tv->tv_usec = tv_usec;
}

/*
 * TODO: Clarify newlib's handling of time units.
 * It appears that newlib is using millisecond resolution for time manipulation,
 * while clock_gettime expects nanoseconds in the timespec struct.
 * Further investigation are needed.
 */
void rv_clock_gettime(struct timespec *tp)
{
    int32_t tv_sec, tv_usec;
    get_time_info(&tv_sec, &tv_usec);
    tp->tv_sec = tv_sec;
    tp->tv_nsec = tv_usec / 1000; /* Transfer to microseconds */
}

char *sanitize_path(const char *orig_path)
{
    size_t n = strlen(orig_path);

    char *ret = (char *) malloc(n + 1);
    memset(ret, '\0', n + 1);

    /* After sanitization, the new path will only be shorter than the original
     * one. Thus, we can reuse the space */
    if (n == 0) {
        ret[0] = '.';
        return ret;
    }

    int rooted = (orig_path[0] == '/');

    /*
     * Invariants:
     *	reading from path; r is index of next byte to process -> path[r]
     *	writing to buf; w is index of next byte to write -> ret[strlen(ret)]
     *	dotdot is index in buf where .. must stop, either because
     *		a) it is the leading slash
     *      b) it is a leading ../../.. prefix.
     */
    size_t w = 0;
    size_t r = 0;
    size_t dotdot = 0;
    if (rooted) {
        ret[w] = '/';
        w++;
        r = 1;
        dotdot = 1;
    }

    while (r < n) {
        if (orig_path[r] == '/') {
            /*  empty path element */
            r++;
        } else if (orig_path[r] == '.' &&
                   (r + 1 == n || orig_path[r + 1] == '/')) {
            /* . element */
            r++;
        } else if (orig_path[r] == '.' && orig_path[r + 1] == '.' &&
                   (r + 2 == n || orig_path[r + 2] == '/')) {
            /* .. element: remove to last / */
            r += 2;

            if (w > dotdot) {
                /* can backtrack */
                w--;
                while (w > dotdot && ret[w] != '/') {
                    w--;
                }
            } else if (!rooted) {
                /* cannot backtrack, but not rooted, so append .. element. */
                if (w > 0) {
                    ret[w] = '/';
                    w++;
                }
                ret[w] = '.';
                w++;
                ret[w] = '.';
                w++;
                dotdot = w;
            }
        } else {
            /* real path element.
               add slash if needed */
            if ((rooted && w != 1) || (!rooted && w != 0)) {
                ret[w] = '/';
                w++;
            }

            /* copy element */
            for (; r < n && orig_path[r] != '/'; r++) {
                ret[w] = orig_path[r];
                w++;
            }
        }
        // printf("w = %ld, r = %ld, dotdot = %ld\nret = %s\n", w, r, dotdot,
        // ret);
    }

    /* Turn empty string into "." */
    if (w == 0) {
        ret[w] = '.';
        w++;
    }

    for (size_t i = w; i < n; i++) {
        ret[i] = '\0';
    }
    return ret;
}
