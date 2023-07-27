/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include "feature.h"

#if defined(__GNUC__) || defined(__clang__)
#define UNUSED __attribute__((unused))
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define UNUSED
#define likely(x) (x)
#define unlikely(x) (x)
#endif

/* Alignment macro */
#if defined(__GNUC__) || defined(__clang__)
#define __ALIGNED(x) __attribute__((aligned(x)))
#elif defined(_MSC_VER)
#define __ALIGNED(x) __declspec(align(x))
#else /* unspported compilers */
#define __ALIGNED(x)
#endif

/* The purpose of __builtin_unreachable() is to assist the compiler in:
 * - Eliminating dead code that the programmer knows will never be executed.
 * - Linearizing the code by indicating to the compiler that the path is 'cold'
 *   (a similar effect can be achieved by calling a noreturn function).
 */
#if defined(__GNUC__) || defined(__clang__)
#define __UNREACHABLE __builtin_unreachable()
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

/* Pattern Matching for C macros.
 * https://github.com/pfultz2/Cloak/wiki/C-Preprocessor-tricks,-tips,-and-idioms
 */

/* In Visual Stido, __VA_ARGS__ is treated as a separate parameter. */
#define FIX_VC_BUG(x) x

/* catenate */
#define PRIMITIVE_CAT(a, ...) FIX_VC_BUG(a##__VA_ARGS__)

#define IIF(c) PRIMITIVE_CAT(IIF_, c)
/* run the 2nd parameter */
#define IIF_0(t, ...) __VA_ARGS__
/* run the 1st parameter */
#define IIF_1(t, ...) t

#if defined(__GNUC__) || defined(__clang__)
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
