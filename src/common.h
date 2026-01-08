/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <assert.h>
#include <stdint.h>

#include "feature.h"
#include "log.h"

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

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

#define MASK(n) ((n) >= 64 ? ~0ULL : ~(~0ULL << (n)))

#if defined(_MSC_VER)
#include <intrin.h>
static inline int rv_clz(uint32_t v)
{
    /* 0 is considered as undefined behavior */
    assert(v);

    uint32_t leading_zero = 0;
    _BitScanReverse(&leading_zero, v);
    return 31 - leading_zero;
}
#elif defined(__GNUC__) || defined(__clang__)
static inline int rv_clz(uint32_t v)
{
    /* https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html */
    /* 0 is considered as undefined behavior */
    assert(v);

    return __builtin_clz(v);
}
#else /* generic implementation */
static inline int rv_clz(uint32_t v)
{
    /* 0 is considered as undefined behavior */
    assert(v);

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

#if defined(_MSC_VER)
#include <intrin.h>
static inline int rv_ctz(uint32_t v)
{
    /* 0 is considered as undefined behavior */
    assert(v);

    uint32_t trailing_zero = 0;
    _BitScanForward(&trailing_zero, v);
    return trailing_zero;
}
#elif defined(__GNUC__) || defined(__clang__)
static inline int rv_ctz(uint32_t v)
{
    /* https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html */
    /* 0 is considered as undefined behavior */
    assert(v);

    return __builtin_ctz(v);
}
#else /* generic implementation */
static inline int rv_ctz(uint32_t v)
{
    /* 0 is considered as undefined behavior */
    assert(v);

    /* https://graphics.stanford.edu/~seander/bithacks.html#ZerosOnRightMultLookup
     */

    static const int mul_debruijn[32] = {
        0,  1,  28, 2,  29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4,  8,
        31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6,  11, 5,  10, 9};

    return mul_debruijn[((uint32_t) ((v & -v) * 0x077CB531U)) >> 27];
}
#endif

#if defined(__GNUC__) || defined(__clang__)
static inline int rv_popcount(uint32_t v)
{
    /* https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html */

    return __builtin_popcount(v);
}
#else /* generic implementation */
static inline int rv_popcount(uint32_t v)
{
    /* https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
     */

    v -= (v >> 1) & 0x55555555;
    v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
    v = (v + (v >> 4)) & 0x0f0f0f0f;
    return (v * 0x01010101) >> 24;
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

/* The preserve_none calling convention minimizes register preservation overhead
 * in the interpreter's threaded dispatch. It must be applied to function
 * declarations, not return statements. macOS clang falsely reports support.
 */
#if defined(__has_attribute) && __has_attribute(preserve_none) && \
    !defined(__APPLE__)
#define PRESERVE_NONE __attribute__((preserve_none))
#else
#define PRESERVE_NONE
#endif

/* Assume that all POSIX-compatible environments provide mmap system call.
 * Emscripten is excluded because it lacks signal-based demand paging support
 * and C11 atomics require special compilation flags not enabled by default.
 */
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
#define HAVE_MMAP 0
#else
/* Assume POSIX-compatible runtime */
#define HAVE_MMAP 1
#endif

/* Portable atomic operations
 *
 * Prefer GNU __atomic builtins (GCC 4.7+, Clang 3.1+) as they work with
 * non-_Atomic types, which is required for our existing struct definitions.
 * C11 stdatomic requires _Atomic type qualifiers on all atomic variables.
 *
 * Memory ordering constants:
 *   ATOMIC_RELAXED - No synchronization, only atomicity guaranteed
 *   ATOMIC_ACQUIRE - Prevents reordering of subsequent reads
 *   ATOMIC_RELEASE - Prevents reordering of preceding writes
 *   ATOMIC_SEQ_CST - Full sequential consistency (strongest)
 */
#if defined(__GNUC__) || defined(__clang__)
/* GNU __atomic builtins (GCC 4.7+, Clang 3.1+) */
#define HAVE_C11_ATOMICS 0

#define ATOMIC_RELAXED __ATOMIC_RELAXED
#define ATOMIC_ACQUIRE __ATOMIC_ACQUIRE
#define ATOMIC_RELEASE __ATOMIC_RELEASE
#define ATOMIC_SEQ_CST __ATOMIC_SEQ_CST

#define ATOMIC_LOAD(ptr, order) __atomic_load_n(ptr, order)
#define ATOMIC_STORE(ptr, val, order) __atomic_store_n(ptr, val, order)
#define ATOMIC_FETCH_ADD(ptr, val, order) __atomic_fetch_add(ptr, val, order)
#define ATOMIC_FETCH_SUB(ptr, val, order) __atomic_fetch_sub(ptr, val, order)
#define ATOMIC_EXCHANGE(ptr, val, order) __atomic_exchange_n(ptr, val, order)
#define ATOMIC_COMPARE_EXCHANGE_WEAK(ptr, expected, desired, succ, fail) \
    __atomic_compare_exchange_n(ptr, expected, desired, 1, succ, fail)

#elif !defined(__EMSCRIPTEN__) && defined(__STDC_VERSION__) && \
    (__STDC_VERSION__ >= 201112L) && !defined(__STDC_NO_ATOMICS__)
/* C11 atomics fallback - requires GNU __typeof__ extension for type inference.
 * Note: The cast to (_Atomic T*) is technically undefined behavior per C11,
 * but is a widely-used idiom that works correctly on GCC/Clang.
 */
#include <stdatomic.h>
#define HAVE_C11_ATOMICS 1

#define ATOMIC_RELAXED memory_order_relaxed
#define ATOMIC_ACQUIRE memory_order_acquire
#define ATOMIC_RELEASE memory_order_release
#define ATOMIC_SEQ_CST memory_order_seq_cst

#define ATOMIC_LOAD(ptr, order) \
    atomic_load_explicit((_Atomic __typeof__(*(ptr)) *) (ptr), order)
#define ATOMIC_STORE(ptr, val, order) \
    atomic_store_explicit((_Atomic __typeof__(*(ptr)) *) (ptr), val, order)
#define ATOMIC_FETCH_ADD(ptr, val, order) \
    atomic_fetch_add_explicit((_Atomic __typeof__(*(ptr)) *) (ptr), val, order)
#define ATOMIC_FETCH_SUB(ptr, val, order) \
    atomic_fetch_sub_explicit((_Atomic __typeof__(*(ptr)) *) (ptr), val, order)
#define ATOMIC_EXCHANGE(ptr, val, order) \
    atomic_exchange_explicit((_Atomic __typeof__(*(ptr)) *) (ptr), val, order)
#define ATOMIC_COMPARE_EXCHANGE_WEAK(ptr, expected, desired, succ, fail) \
    atomic_compare_exchange_weak_explicit(                               \
        (_Atomic __typeof__(*(ptr)) *) (ptr), expected, desired, succ, fail)

#else
/* No atomic support - single-threaded fallback (T2C requires atomics) */
#define HAVE_C11_ATOMICS 0

#if defined(_MSC_VER)
#pragma message("No atomic operations available. T2C JIT will be disabled.")
#else
#warning "No atomic operations available. T2C JIT will be disabled."
#endif

#define ATOMIC_RELAXED 0
#define ATOMIC_ACQUIRE 0
#define ATOMIC_RELEASE 0
#define ATOMIC_SEQ_CST 0

/* Simple non-atomic fallback - only safe for single-threaded use.
 * WARNING: ATOMIC_EXCHANGE returns new value (not old) without extensions.
 * T2C/GDBSTUB require proper atomics and will have data races on these
 * platforms - they should be disabled when atomics are unavailable.
 */
#define ATOMIC_LOAD(ptr, order) (*(ptr))
#define ATOMIC_STORE(ptr, val, order) ((void) (*(ptr) = (val)))
#define ATOMIC_FETCH_ADD(ptr, val, order) \
    ((*(ptr) += (val)) - (val)) /* return old value */
#define ATOMIC_FETCH_SUB(ptr, val, order) \
    ((*(ptr) -= (val)) + (val)) /* return old value */
/* ATOMIC_EXCHANGE cannot return old value without statement expressions.
 * This returns NEW value - callers must not rely on return value. */
#define ATOMIC_EXCHANGE(ptr, val, order) (*(ptr) = (val))
/* ATOMIC_COMPARE_EXCHANGE_WEAK: non-atomic, single-threaded only */
#define ATOMIC_COMPARE_EXCHANGE_WEAK(ptr, expected, desired, succ, fail) \
    ((*(ptr) == *(expected)) ? (*(ptr) = (desired), 1)                   \
                             : (*(expected) = *(ptr), 0))
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
