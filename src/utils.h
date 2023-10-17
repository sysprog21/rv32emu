#pragma once

#include <sys/time.h>
#include <time.h>

/* Obtain the system 's notion of the current Greenwich time.
 * TODO: manipulate current time zone.
 */
void rv_gettimeofday(struct timeval *tv);

/* Retrieve the value used by a clock which is specified by clock_id. */
void rv_clock_gettime(struct timespec *tp);

/* This hashing routine is adapted from Linux kernel.
 * See
 * https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/include/linux/hash.h
 */
#define HASH_FUNC_IMPL(name, size_bits, size)                        \
    FORCE_INLINE uint32_t name(uint32_t val)                         \
    {                                                                \
        /* 0x61C88647 is 32-bit golden ratio */                      \
        return (val * 0x61C88647 >> (32 - size_bits)) & ((size) -1); \
    }
