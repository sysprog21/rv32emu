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
#define HASH_FUNC_IMPL(name, size_bits, size)                           \
    FORCE_INLINE uint32_t name(uint32_t val)                            \
    {                                                                   \
        /* 0x61C88647 is 32-bit golden ratio */                         \
        return (val * 0x61C88647 >> (32 - size_bits)) & ((size) - (1)); \
    }

/*
 * Reference:
 * https://cs.opensource.google/go/go/+/refs/tags/go1.21.4:src/path/path.go;l=51
 *
 * sanitize_path returns the shortest path name equivalent to path
 * by purely lexical processing. It applies the following rules
 * iteratively until no further processing can be done:
 *
 *  1. Replace multiple slashes with a single slash.
 *  2. Eliminate each . path name element (the current directory).
 *  3. Eliminate each inner .. path name element (the parent directory)
 *     along with the non-.. element that precedes it.
 *  4. Eliminate .. elements that begin a rooted path:
 *     that is, replace "/.." by "/" at the beginning of a path.
 *
 * The returned path ends in a slash only if it is the root "/".
 *
 * If the result of this process is an empty string, Clean
 * returns the string ".".
 *
 * See also Rob Pike, “Lexical File Names in Plan 9 or
 * Getting Dot-Dot Right,”
 * https://9p.io/sys/doc/lexnames.html
 */
char *sanitize_path(const char *orig_path);

void sanitize_path_test();
