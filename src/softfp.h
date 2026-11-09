/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdint.h>

#include "riscv.h"
#include "riscv_private.h"

/* clang-format off */
enum {
    //                    ....xxxx....xxxx....xxxx....xxxx
    FMASK_SIGN        = 0b10000000000000000000000000000000,
    FMASK_EXPN        = 0b01111111100000000000000000000000,
    FMASK_FRAC        = 0b00000000011111111111111111111111,
    FMASK_QNAN        = 0b00000000010000000000000000000000,
    //                    ....xxxx....xxxx....xxxx....xxxx
    FFLAG_MASK        = 0b00000000000000000000000000011111,
    FFLAG_INVALID_OP  = 0b00000000000000000000000000010000,
    FFLAG_DIV_BY_ZERO = 0b00000000000000000000000000001000,
    FFLAG_OVERFLOW    = 0b00000000000000000000000000000100,
    FFLAG_UNDERFLOW   = 0b00000000000000000000000000000010,
    FFLAG_INEXACT     = 0b00000000000000000000000000000001,
    FCSR_MASK         = 0b00000000000000000000000011111111,
    FRM_MASK          = 0b00000000000000000000000000000111,
    FRM_SHIFT         = 5,
    //                    ....xxxx....xxxx....xxxx....xxxx
    RV_NAN            = 0b01111111110000000000000000000000
};
/* clang-format on */

#if RV32_HAS(EXT_D)
enum {
    RV_NAN32_BOX =
        0xFFFFFFFFu, /* upper half of a boxed single-precision value */
};
#define FMASK64_SIGN UINT64_C(0x8000000000000000)
#define FMASK64_EXPN UINT64_C(0x7ff0000000000000)
#define FMASK64_FRAC UINT64_C(0x000fffffffffffff)
#define FMASK64_QNAN UINT64_C(0x0008000000000000)
#define RV_NAN64 UINT64_C(0x7ff8000000000000)

/* Float registers are 64 bits wide: F[] holds the lower half and F_hi[] the
 * upper. A single-precision value must be NaN-boxed, its upper half all ones;
 * reading an unboxed register as single precision yields the canonical NaN
 * (RISC-V Unprivileged ISA, D extension).
 */
#define F32_SRC(rv, r)                              \
    ((rv)->F_hi[(r)] == RV_NAN32_BOX ? (rv)->F[(r)] \
                                     : (riscv_float_t) {.v = RV_NAN})
#define F32_SET(rv, r, val)             \
    do {                                \
        (rv)->F[(r)] = (val);           \
        (rv)->F_hi[(r)] = RV_NAN32_BOX; \
    } while (0)
#define F32_SETV(rv, r, bits) F32_SET(rv, r, ((riscv_float_t) {.v = (bits)}))

static inline bool is_nan64(uint64_t f)
{
    return (f & FMASK64_EXPN) == FMASK64_EXPN && (f & FMASK64_FRAC);
}

static inline softfloat_float64_t f64_reg(const riscv_t *rv, uint32_t r)
{
    softfloat_float64_t value;

    value.v = ((uint64_t) rv->F_hi[r] << 32) | rv->F[r].v;
    return value;
}

static inline void f64_set(riscv_t *rv, uint32_t r, softfloat_float64_t value)
{
    rv->F[r].v = (uint32_t) value.v;
    rv->F_hi[r] = (uint32_t) (value.v >> 32);
}

static inline uint32_t calc_fclass64(uint64_t f)
{
    const uint64_t sign = f & FMASK64_SIGN;
    const uint64_t expn = f & FMASK64_EXPN;
    const uint64_t frac = f & FMASK64_FRAC;

    if (expn == FMASK64_EXPN) {
        if (frac)
            return (frac & FMASK64_QNAN) ? 0x200 : 0x100;
        return sign ? 0x001 : 0x080;
    }
    if (expn)
        return sign ? 0x002 : 0x040;
    if (frac)
        return sign ? 0x004 : 0x020;
    return sign ? 0x008 : 0x010;
}
#else
#define F32_SRC(rv, r) ((rv)->F[(r)])
#define F32_SET(rv, r, val) ((rv)->F[(r)] = (val))
#define F32_SETV(rv, r, bits) ((rv)->F[(r)].v = (bits))
#endif

/* compute the fclass result */
static inline uint32_t calc_fclass(uint32_t f)
{
    const uint32_t sign = f & FMASK_SIGN;
    const uint32_t expn = f & FMASK_EXPN;
    const uint32_t frac = f & FMASK_FRAC;

    uint32_t out = 0;

    /*
     * 0x001    rs1 is -INF
     * 0x002    rs1 is negative normal
     * 0x004    rs1 is negative subnormal
     * 0x008    rs1 is -0
     * 0x010    rs1 is +0
     * 0x020    rs1 is positive subnormal
     * 0x040    rs1 is positive normal
     * 0x080    rs1 is +INF
     * 0x100    rs1 is a signaling NaN
     * 0x200    rs1 is a quiet NaN
     */

    /* Check the exponent bits */
    if (expn) {
        if (expn != FMASK_EXPN) {
            /* Check if it is negative normal or positive normal */
            out = sign ? 0x002 : 0x040;
        } else {
            /* Check if it is NaN */
            if (frac) {
                out = frac & FMASK_QNAN ? 0x200 : 0x100;
            } else if (!sign) {
                /* Check if it is +INF */
                out = 0x080;
            } else {
                /* Check if it is -INF */
                out = 0x001;
            }
        }
    } else if (frac) {
        /* Check if it is negative or positive subnormal */
        out = sign ? 0x004 : 0x020;
    } else {
        /* Check if it is +0 or -0 */
        out = sign ? 0x008 : 0x010;
    }

    return out;
}

static inline bool is_nan(uint32_t f)
{
    const uint32_t expn = f & FMASK_EXPN;
    const uint32_t frac = f & FMASK_FRAC;
    return (expn == FMASK_EXPN && frac);
}

static inline void set_fflag(riscv_t *rv)
{
    if (softfloat_exceptionFlags & softfloat_flag_invalid)
        rv->csr_fcsr |= FFLAG_INVALID_OP;
    if (softfloat_exceptionFlags & softfloat_flag_infinite)
        rv->csr_fcsr |= FFLAG_DIV_BY_ZERO;
    if (softfloat_exceptionFlags & softfloat_flag_overflow)
        rv->csr_fcsr |= FFLAG_OVERFLOW;
    if (softfloat_exceptionFlags & softfloat_flag_underflow)
        rv->csr_fcsr |= FFLAG_UNDERFLOW;
    if (softfloat_exceptionFlags & softfloat_flag_inexact)
        rv->csr_fcsr |= FFLAG_INEXACT;
    softfloat_exceptionFlags = 0;
}

static inline void set_rounding_mode(riscv_t *rv, uint8_t rm)
{
    const uint32_t frm = (rv->csr_fcsr >> FRM_SHIFT) & FRM_MASK;

    if (likely(rm == 0b111))
        rm = frm;

    switch (rm) {
    case 0b000:
        softfloat_roundingMode = softfloat_round_near_even;
        break;
    case 0b001:
        softfloat_roundingMode = softfloat_round_minMag;
        break;
    case 0b010:
        softfloat_roundingMode = softfloat_round_min;
        break;
    case 0b011:
        softfloat_roundingMode = softfloat_round_max;
        break;
    case 0b100:
        softfloat_roundingMode = softfloat_round_near_maxMag;
        break;
    default:
        __UNREACHABLE;
        break;
    }
}
