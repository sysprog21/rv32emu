/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdint.h>

#include "feature.h"

#if defined(__GNUC__) || defined(__clang__)
#define UNUSED __attribute__((unused))
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define FORCE_INLINE static inline __attribute__((always_inline))
#else
#define UNUSED
#define likely(x) (x)
#define unlikely(x) (x)
#if defined(_MSC_VER)
#define FORCE_INLINE static inline __forceinline
#else
#define FORCE_INLINE static inline
#endif
#endif

#define ARRAYS_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

#define MASK(n) (~((~0U << (n))))

#if defined(_MSC_VER)
#include <intrin.h>
static inline int rv_clz(uint32_t v)
{
    uint32_t leading_zero = 0;
    if (_BitScanReverse(&leading_zero, v))
        return 31 - leading_zero;
    return 32; /* undefined behavior */
}
#elif defined(__GNUC__) || defined(__clang__)
static inline int rv_clz(uint32_t v)
{
    /*  https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html */
    /*  If x is 0, the result is undefined. */
    return __builtin_clz(v);
}
#else /* generic implementation */
static inline int rv_clz(uint32_t v)
{
    /* http://graphics.stanford.edu/~seander/bithacks.html#IntegerLogDeBruijn */
    static const uint8_t mul_debruijn[] = {
        0, 9,  1,  10, 13, 21, 2,  29, 11, 14, 16, 18, 22, 25, 3, 30,
        8, 12, 20, 28, 15, 17, 24, 7,  19, 27, 23, 6,  26, 5,  4, 31,
    };

    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;

    return mul_debruijn[(uint32_t) (v * 0x07C4ACDDU) >> 27];
}
#endif

/*
 * Integer log base 2
 *
 * The input x must not be zero.
 * Otherwise, the result is undefined on some platform.
 *
 */
static inline uint8_t ilog2(uint32_t x)
{
    return 31 - rv_clz(x);
}

/* Alignment macro */
#if defined(__GNUC__) || defined(__clang__)
#define __ALIGNED(x) __attribute__((aligned(x)))
#elif defined(_MSC_VER)
#define __ALIGNED(x) __declspec(align(x))
#else /* unsupported compilers */
#define __ALIGNED(x)
#endif

/* Packed macro */
#if defined(__GNUC__) || defined(__clang__)
#define PACKED(name) name __attribute__((packed))
#elif defined(_MSC_VER)
#define PACKED(name) __pragma(pack(push, 1)) name __pragma(pack(pop))
#else /* unsupported compilers */
#define PACKED(name)
#endif

/* Endianness */
#if defined(__GNUC__) || defined(__clang__)
#define bswap16(x) __builtin_bswap16(x)
#define bswap32(x) __builtin_bswap32(x)
#else
#define bswap16(x) ((x & 0xff) << 8) | ((x >> 8) & 0xff)
#define bswap32(x)                                                    \
    (bswap16(((x & 0xffff) << 16) | ((x >> 16) & 0xffff)) & 0xffff) | \
        (bswap16(((x & 0xffff) << 16) | ((x >> 16) & 0xffff)) & 0xffff) << 16
#endif

/* The purpose of __builtin_unreachable() is to assist the compiler in:
 * - Eliminating dead code that the programmer knows will never be executed.
 * - Linearizing the code by indicating to the compiler that the path is 'cold'
 *   (a similar effect can be achieved by calling a noreturn function).
 */
#if defined(__GNUC__) || defined(__clang__)
#define __UNREACHABLE __builtin_unreachable()
#elif defined(_MSC_VER)
#define __UNREACHABLE __assume(false)
#else /* unspported compilers */
/* clang-format off */
#define __UNREACHABLE do { /* nop */ } while (0)
/* clang-format on */
#endif

/* Non-optimized builds do not have tail-call optimization (TCO). To work
 * around this, the compiler attribute 'musttail' is used, which forces TCO
 * even without optimizations enabled.
 */
#if defined(__has_attribute) && __has_attribute(musttail)
#define MUST_TAIL __attribute__((musttail))
#else
#define MUST_TAIL
#endif

/* Assume that all POSIX-compatible environments provide mmap system call. */
#if defined(_WIN32)
#define HAVE_MMAP 0
#else
/* Assume POSIX-compatible runtime */
#define HAVE_MMAP 1
#endif

/* Pattern Matching for C macros.
 * https://github.com/pfultz2/Cloak/wiki/C-Preprocessor-tricks,-tips,-and-idioms
 */

/* In Visual Studio, __VA_ARGS__ is treated as a separate parameter. */
#define FIX_VC_BUG(x) x

/* catenate */
#define PRIMITIVE_CAT(a, ...) FIX_VC_BUG(a##__VA_ARGS__)

#define IIF(c) PRIMITIVE_CAT(IIF_, c)
/* run the 2nd parameter */
#define IIF_0(t, ...) __VA_ARGS__
/* run the 1st parameter */
#define IIF_1(t, ...) t

/* Accept any number of args >= N, but expand to just the Nth one. The macro
 * that calls the function still only supports 4 args, but the set of values
 * that might need to be returned is 1 larger, so N is increased to 6.
 */
#define _GET_NTH_ARG(_1, _2, _3, _4, _5, N, ...) N

/* Count how many args are in a variadic macro. The GCC/Clang extension is used
 * to handle the case where ... expands to nothing. A placeholder arg is added
 * before ##VA_ARGS (its value is irrelevant but necessary to preserve the
 * shifting offset).
 * Additionally, 0 is added as a valid value in the N position.
 */
#define COUNT_VARARGS(...) _GET_NTH_ARG("ignored", ##__VA_ARGS__, 4, 3, 2, 1, 0)

/* As of C23, typeof is now included as part of the C standard. */
#if defined(__GNUC__) || defined(__clang__) ||         \
    (defined(__STDC__) && defined(__STDC_VERSION__) && \
     (__STDC_VERSION__ >= 202000L)) /* C2x/C23 ?*/
#define __HAVE_TYPEOF 1
#endif

/**
 * container_of() - Calculate address of object that contains address ptr
 * @ptr: pointer to member variable
 * @type: type of the structure containing ptr
 * @member: name of the member variable in struct @type
 *
 * Return: @type pointer of object containing ptr
 */
#ifndef container_of
#ifdef __HAVE_TYPEOF
#define container_of(ptr, type, member)                            \
    __extension__({                                                \
        const __typeof__(((type *) 0)->member) *__pmember = (ptr); \
        (type *) ((char *) __pmember - offsetof(type, member));    \
    })
#else
#define container_of(ptr, type, member) \
    ((type *) ((char *) (ptr) - (offsetof(type, member))))
#endif
#endif
