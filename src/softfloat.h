/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdint.h>

#include "riscv.h"

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
    //                    ....xxxx....xxxx....xxxx....xxxx
    RV_NAN            = 0b01111111110000000000000000000000
};
/* clang-format on */

/* compute the fclass result */
static inline uint32_t calc_fclass(uint32_t f)
{
    const uint32_t sign = f & FMASK_SIGN;
    const uint32_t expn = f & FMASK_EXPN;
    const uint32_t frac = f & FMASK_FRAC;

    /* TODO: optimize with a binary decision tree */

    uint32_t out = 0;
    /* 0x001    rs1 is -INF */
    out |= (f == 0xff800000) ? 0x001 : 0;
    /* 0x002    rs1 is negative normal */
    out |= (expn && (expn != FMASK_EXPN) && sign) ? 0x002 : 0;
    /* 0x004    rs1 is negative subnormal */
    out |= (!expn && frac && sign) ? 0x004 : 0;
    /* 0x008    rs1 is -0 */
    out |= (f == 0x80000000) ? 0x008 : 0;
    /* 0x010    rs1 is +0 */
    out |= (f == 0x00000000) ? 0x010 : 0;
    /* 0x020    rs1 is positive subnormal */
    out |= (!expn && frac && !sign) ? 0x020 : 0;
    /* 0x040    rs1 is positive normal */
    out |= (expn && (expn != FMASK_EXPN) && !sign) ? 0x040 : 0;
    /* 0x080    rs1 is +INF */
    out |= (expn == FMASK_EXPN && !frac && !sign) ? 0x080 : 0;
    /* 0x100    rs1 is a signaling NaN */
    out |= (expn == FMASK_EXPN && frac && !(frac & FMASK_QNAN)) ? 0x100 : 0;
    /* 0x200    rs1 is a quiet NaN */
    out |= (expn == FMASK_EXPN && (frac & FMASK_QNAN)) ? 0x200 : 0;

    return out;
}

static inline bool is_nan(uint32_t f)
{
    const uint32_t expn = f & FMASK_EXPN;
    const uint32_t frac = f & FMASK_FRAC;
    return (expn == FMASK_EXPN && frac);
}

static inline bool is_snan(uint32_t f)
{
    const uint32_t expn = f & FMASK_EXPN;
    const uint32_t frac = f & FMASK_FRAC;
    return (expn == FMASK_EXPN && frac && !(frac & FMASK_QNAN));
}
