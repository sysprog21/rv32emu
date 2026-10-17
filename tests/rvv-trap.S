/*
 * Illegal-instruction traps raised by vector instructions at run time.
 *
 * The RVV smoke test can only check its last output line, so a trap there is
 * indistinguishable from any other abort. Here a machine-mode trap vector
 * counts every exception and records mcause and mepc, then resumes after the
 * faulting instruction. Each case checks that exactly one trap was taken,
 * that it was an illegal instruction, and that mepc names
 * the instruction itself rather than the start of its block. Cases that must
 * trap also check that the destination register was left untouched, and each
 * has a legal neighbour that must not trap.
 *
 * Fault-only-first loads need a memory fault, which the user-mode emulator
 * never raises, so they are not covered here.
 *
 * A failing case exits with its own status and prints "RVV trap FAIL".
 */
    /* Nothing sets gp in this freestanding program, so keep la PC-relative. */
    .option norelax

    .equ SYSWRITE, 64
    .equ SYSEXIT, 93
    .equ ILLEGAL_INSN, 2

/* Run insn, which must raise exactly one illegal-instruction trap. */
.macro must_trap id, insn:vararg
    li   s1, \id
    addi s2, s10, 1
1:
    \insn
    bne  s10, s2, fail
    li   t0, ILLEGAL_INSN
    bne  s11, t0, fail
    la   t0, 1b
    bne  s8, t0, fail
.endm

/* Run insn, which must not trap. */
.macro must_not_trap id, insn:vararg
    li   s1, \id
    mv   s2, s10
    \insn
    bne  s10, s2, fail
.endm

/* Store the first four bytes of vreg to out and compare them with word. */
.macro expect_bytes id, vreg, word
    li   s1, \id
    la   a1, out
    vse8.v \vreg, (a1)
    lw   t0, 0(a1)
    li   t1, \word
    bne  t0, t1, fail
.endm

    .data
src:    .byte 0x01, 0x02, 0x03, 0x04
idx:    .byte 0x03, 0x02, 0x01, 0x00
mask:   .byte 0x05, 0x00, 0x00, 0x00
seg:    .byte 0x11, 0x21, 0x12, 0x22, 0x13, 0x23, 0x14, 0x24
    .balign 4
out:    .space 16
ok_msg:
    .ascii "RVV trap OK\n"
    .equ ok_len, . - ok_msg
fail_msg:
    .ascii "RVV trap FAIL\n"
    .equ fail_len, . - fail_msg

    .text
    .global _start
_start:
    la   t0, handler
    csrw mtvec, t0
    li   s10, 0                 /* traps taken */

    li   a0, 4
    vsetvli t0, a0, e8, m1, tu, mu
    la   a1, src
    vle8.v v2, (a1)             /* v2 = 01 02 03 04 */
    la   a1, idx
    vle8.v v3, (a1)             /* v3 = 03 02 01 00 */
    la   a1, mask
    vle8.v v0, (a1)             /* v0 = 0b0101 */
    li   t0, 0x55
    vmv.v.x v1, t0              /* sentinel for destinations that trap */

    /* vrgather: vd may overlap neither the data nor the index group. */
    vmv.v.v v5, v2
    must_trap 1, vrgather.vv v5, v5, v3
    expect_bytes 2, v5, 0x04030201
    vmv.v.v v5, v3
    must_trap 3, vrgather.vv v5, v2, v5
    expect_bytes 4, v5, 0x00010203
    must_trap 5, vrgather.vi v1, v1, 0
    expect_bytes 6, v1, 0x55555555
    must_trap 7, vrgatherei16.vv v4, v2, v4
    must_not_trap 8, vrgather.vv v6, v2, v3
    expect_bytes 9, v6, 0x01020304

    /* vcompress: vstart must be 0, and vd may overlap neither source. */
    li   t0, 2
    csrw vstart, t0
    must_trap 10, vcompress.vm v1, v2, v0
    csrr t0, vstart             /* the trap must leave vstart as it was */
    li   t1, 2
    li   s1, 11
    bne  t0, t1, fail
    csrw vstart, zero
    expect_bytes 12, v1, 0x55555555
    must_trap 13, vcompress.vm v2, v2, v0
    must_trap 14, vcompress.vm v0, v2, v0
    vmv.v.x v7, zero
    must_not_trap 15, vcompress.vm v7, v2, v0
    expect_bytes 16, v7, 0x00000301

    /* A masked segment load may not write v0, its own mask. */
    la   a1, seg
    must_trap 17, vlseg2e8.v v0, (a1), v0.t
    must_not_trap 18, vlseg2e8.v v8, (a1), v0.t

    li   a0, 1
    la   a1, ok_msg
    li   a2, ok_len
    li   a7, SYSWRITE
    ecall
    li   a0, 0
    j    exit

fail:
    li   a0, 1
    la   a1, fail_msg
    li   a2, fail_len
    li   a7, SYSWRITE
    ecall
    mv   a0, s1
exit:
    li   a7, SYSEXIT
    ecall

    /* Record the exception and resume after the faulting instruction. */
    .balign 4
handler:
    addi s10, s10, 1
    csrr s11, mcause
    csrr s8, mepc
    addi t6, s8, 4
    csrw mepc, t6
    mret
