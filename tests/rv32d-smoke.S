/*
 * Smoke test for the D extension: 64-bit float registers, NaN-boxing of
 * single-precision values, and the double-precision instructions.
 *
 * Every expected value is worked out by hand from the IEEE 754 and RISC-V
 * definitions. A failing case exits with its own status and prints
 * "RV32D smoke FAIL".
 */
    /* Nothing sets gp in this freestanding program, so keep la PC-relative. */
    .option norelax

    .equ SYSWRITE, 64
    .equ SYSEXIT, 93

/* Fail with status id unless reg equals imm. */
.macro check id, reg, imm
    li   s1, \id
    li   t6, \imm
    bne  \reg, t6, fail
.endm

/* Store f<n> to out and check its low and high words. */
.macro expect_d id, freg, lo, hi
    la   a1, out
    fsd  \freg, 0(a1)
    lw   t1, 0(a1)
    check \id, t1, \lo
    lw   t1, 4(a1)
    check \id + 100, t1, \hi
.endm

    .data
    .balign 8
d_1_0:    .dword 0x3ff0000000000000            /* 1.0 */
d_2_0:    .dword 0x4000000000000000            /* 2.0 */
d_3_0:    .dword 0x4008000000000000            /* 3.0 */
d_1_5:    .dword 0x3ff8000000000000            /* 1.5 */
d_2_5:    .dword 0x4004000000000000            /* 2.5 */
d_ninf:   .dword 0xfff0000000000000            /* -infinity */
d_snan:   .dword 0x7ff0000000000001            /* signaling NaN */
d_qnan:   .dword 0x7ff8000000000000            /* quiet NaN */
d_pzero:  .dword 0x0000000000000000
d_nzero:  .dword 0x8000000000000000
d_pat:    .dword 0x0123456789abcdef
d_unbox:  .dword 0x000000003f800000            /* 1.0f, but not NaN-boxed */
d_zero:   .dword 0
    .balign 8
out:      .space 16
stack:    .space 64
ok_msg:
    .ascii "RV32D smoke OK\n"
    .equ ok_len, . - ok_msg
fail_msg:
    .ascii "RV32D smoke FAIL\n"
    .equ fail_len, . - fail_msg

    .text
    .global _start
_start:
    /* fld and fsd move all 64 bits. */
    la   a1, d_pat
    fld  fa0, 0(a1)
    expect_d 1, fa0, 0x89abcdef, 0x01234567

    /* A single-precision write is NaN-boxed: the upper word becomes all ones. */
    li   t0, 0x3f800000
    fmv.w.x fa1, t0
    expect_d 2, fa1, 0x3f800000, 0xffffffff

    /* A value that is not NaN-boxed reads as the canonical NaN, and that is a
     * quiet NaN, so no flag is raised.
     */
    la   a1, d_unbox
    fld  fa2, 0(a1)
    csrw fflags, zero
    fadd.s fa3, fa2, fa2
    expect_d 3, fa3, 0x7fc00000, 0xffffffff
    csrr t1, fflags
    check 4, t1, 0

    /* fmv.x.w and fsw take the lower word whether or not it is boxed. */
    fmv.x.w t1, fa2
    check 5, t1, 0x3f800000

    /* Basic arithmetic. */
    la   a1, d_1_0
    fld  fa0, 0(a1)
    la   a1, d_2_0
    fld  fa1, 0(a1)
    fadd.d fa2, fa0, fa1
    expect_d 6, fa2, 0x00000000, 0x40080000
    fsub.d fa2, fa0, fa1
    expect_d 7, fa2, 0x00000000, 0xbff00000
    fmul.d fa2, fa1, fa1
    expect_d 8, fa2, 0x00000000, 0x40100000
    fdiv.d fa2, fa1, fa1
    expect_d 9, fa2, 0x00000000, 0x3ff00000
    la   a1, d_3_0
    fld  fa3, 0(a1)
    fmadd.d fa2, fa1, fa3, fa0          /* 2 * 3 + 1 = 7 */
    expect_d 10, fa2, 0x00000000, 0x401c0000
    fmsub.d fa2, fa1, fa3, fa0          /* 2 * 3 - 1 = 5 */
    expect_d 11, fa2, 0x00000000, 0x40140000
    fnmsub.d fa2, fa1, fa3, fa0         /* -(2 * 3) + 1 = -5 */
    expect_d 12, fa2, 0x00000000, 0xc0140000
    fnmadd.d fa2, fa1, fa3, fa0         /* -(2 * 3) - 1 = -7 */
    expect_d 13, fa2, 0x00000000, 0xc01c0000

    /* 1/3 is inexact; 1/0 divides by zero. */
    csrw fflags, zero
    fdiv.d fa2, fa0, fa3
    csrr t1, fflags
    check 14, t1, 1
    csrw fflags, zero
    la   a1, d_pzero
    fld  fa4, 0(a1)
    fdiv.d fa2, fa0, fa4
    csrr t1, fflags
    check 15, t1, 8
    expect_d 16, fa2, 0x00000000, 0x7ff00000

    /* Conversions between the two precisions. */
    la   a1, d_1_5
    fld  fa0, 0(a1)
    fcvt.s.d fa1, fa0
    expect_d 17, fa1, 0x3fc00000, 0xffffffff
    fcvt.d.s fa2, fa1
    expect_d 18, fa2, 0x00000000, 0x3ff80000
    li   t0, -1
    fcvt.d.w fa3, t0
    expect_d 19, fa3, 0x00000000, 0xbff00000
    li   t0, 0x80000000
    fcvt.d.wu fa3, t0                   /* 2^31 */
    expect_d 20, fa3, 0x00000000, 0x41e00000

    /* 2.5 to an integer under each rounding mode. */
    la   a1, d_2_5
    fld  fa0, 0(a1)
    fcvt.w.d t1, fa0, rne
    check 21, t1, 2
    fcvt.w.d t1, fa0, rtz
    check 22, t1, 2
    fcvt.w.d t1, fa0, rdn
    check 23, t1, 2
    fcvt.w.d t1, fa0, rup
    check 24, t1, 3
    fcvt.w.d t1, fa0, rmm
    check 25, t1, 3
    fcvt.wu.d t1, fa0, rtz
    check 26, t1, 2

    /* Classification. */
    la   a1, d_ninf
    fld  fa0, 0(a1)
    fclass.d t1, fa0
    check 27, t1, 0x001
    la   a1, d_snan
    fld  fa0, 0(a1)
    fclass.d t1, fa0
    check 28, t1, 0x100
    la   a1, d_qnan
    fld  fa0, 0(a1)
    fclass.d t1, fa0
    check 29, t1, 0x200
    la   a1, d_pzero
    fld  fa0, 0(a1)
    fclass.d t1, fa0
    check 30, t1, 0x010
    la   a1, d_nzero
    fld  fa0, 0(a1)
    fclass.d t1, fa0
    check 31, t1, 0x008

    /* A quiet comparison with a NaN is false and raises nothing; a signaling
     * one raises invalid.
     */
    la   a1, d_qnan
    fld  fa0, 0(a1)
    la   a1, d_1_0
    fld  fa1, 0(a1)
    csrw fflags, zero
    feq.d t1, fa0, fa1
    check 32, t1, 0
    csrr t1, fflags
    check 33, t1, 0
    flt.d t1, fa0, fa1
    check 34, t1, 0
    csrr t1, fflags
    check 35, t1, 0x10

    /* fmin and fmax: a NaN loses to a number, -0 is below +0, and a signaling
     * NaN raises invalid but leaves nothing behind for the next instruction.
     */
    la   a1, d_nzero
    fld  fa2, 0(a1)
    la   a1, d_pzero
    fld  fa3, 0(a1)
    fmin.d fa4, fa3, fa2
    expect_d 36, fa4, 0x00000000, 0x80000000
    fmax.d fa4, fa2, fa3
    expect_d 37, fa4, 0x00000000, 0x00000000
    fmin.d fa4, fa0, fa1                /* NaN, 1.0 */
    expect_d 38, fa4, 0x00000000, 0x3ff00000
    la   a1, d_snan
    fld  fa0, 0(a1)
    csrw fflags, zero
    fmin.d fa4, fa0, fa1
    csrr t1, fflags
    check 39, t1, 0x10
    csrw fflags, zero
    feq.d t1, fa1, fa1
    check 40, t1, 1
    csrr t1, fflags
    check 41, t1, 0

    /* Sign injection. */
    la   a1, d_1_0
    fld  fa0, 0(a1)
    la   a1, d_nzero
    fld  fa1, 0(a1)
    fsgnj.d fa2, fa0, fa1
    expect_d 42, fa2, 0x00000000, 0xbff00000
    fsgnjn.d fa2, fa0, fa1
    expect_d 43, fa2, 0x00000000, 0x3ff00000
    fsgnjx.d fa2, fa1, fa1
    expect_d 44, fa2, 0x00000000, 0x00000000

    /* fsqrt.d. */
    la   a1, d_2_0
    fld  fa0, 0(a1)
    la   a1, d_1_0
    fld  fa1, 0(a1)
    fsqrt.d fa2, fa1
    expect_d 45, fa2, 0x00000000, 0x3ff00000

    /* The compressed loads and stores. */
    la   s0, d_pat
    c.fld fs0, 0(s0)
    la   t0, out
    mv   s2, t0
    mv   s3, s0
    la   s0, out
    c.fsd fs0, 8(s0)
    lw   t1, 8(s0)
    check 46, t1, 0x89abcdef
    lw   t1, 12(s0)
    check 47, t1, 0x01234567
    la   t0, stack
    mv   s4, sp
    mv   sp, t0
    la   t0, d_pat
    fld  ft0, 0(t0)
    c.fsdsp ft0, 16(sp)
    fcvt.d.w ft0, zero
    c.fldsp ft0, 16(sp)                 /* f0 is a valid target */
    mv   sp, s4
    expect_d 48, ft0, 0x89abcdef, 0x01234567

    la   a1, ok_msg
    li   a2, ok_len
    li   a7, SYSWRITE
    li   a0, 1
    ecall
    li   a7, SYSEXIT
    li   a0, 0
    ecall

fail:
    la   a1, fail_msg
    li   a2, fail_len
    li   a7, SYSWRITE
    li   a0, 1
    ecall
    li   a7, SYSEXIT
    mv   a0, s1
    ecall
