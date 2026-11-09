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
    vmv.v.x v1, t0              /* sentinels for destinations that trap */
    li   t0, 0x66
    vmv.v.x v10, t0
    li   t0, 0x77
    vmv.v.x v11, t0

    /* vrgather: vd may overlap neither the data nor the index group. */
    vmv.v.v v5, v2
    must_trap 1, vrgather.vv v5, v5, v3
    expect_bytes 2, v5, 0x04030201
    vmv.v.v v5, v3
    must_trap 3, vrgather.vv v5, v2, v5
    expect_bytes 4, v5, 0x00010203
    must_trap 5, vrgather.vi v1, v1, 0
    expect_bytes 6, v1, 0x55555555
    /* vrgatherei16 indexes with EEW=16, so its index group is v10-v11. */
    must_trap 7, vrgatherei16.vv v10, v2, v10
    expect_bytes 19, v10, 0x66666666
    expect_bytes 20, v11, 0x77777777
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
    expect_bytes 21, v2, 0x04030201
    must_trap 14, vcompress.vm v0, v2, v0
    expect_bytes 22, v0, 0x00000005
    vmv.v.x v7, zero
    must_not_trap 15, vcompress.vm v7, v2, v0
    expect_bytes 16, v7, 0x00000301

    /* A masked segment load may not write v0, its own mask. */
    la   a1, seg
    must_trap 17, vlseg2e8.v v0, (a1), v0.t
    expect_bytes 23, v0, 0x00000005
    expect_bytes 24, v1, 0x55555555
    la   a1, seg
    must_not_trap 18, vlseg2e8.v v8, (a1), v0.t

    /* v0 is the mask (EEW=1) of a masked instruction, so it cannot also be a
     * data source or the data destination of that instruction (V 1.0 5.2),
     * and that holds for the compares and carry forms too: they read v0 as a
     * mask and as data. A mask or scalar result may still be v0.
     */
    must_trap 25, vadd.vv v0, v2, v3, v0.t
    must_trap 26, vadd.vv v8, v0, v3, v0.t
    must_trap 27, vadd.vv v8, v2, v0, v0.t
    must_not_trap 28, vadd.vv v8, v0, v3
    must_not_trap 29, vadd.vv v8, v2, v3, v0.t
    must_trap 30, vadc.vvm v8, v0, v3, v0
    must_not_trap 50, vadc.vvm v8, v2, v3, v0
    must_trap 31, vadc.vvm v0, v2, v3, v0
    must_trap 32, vmseq.vv v9, v0, v3, v0.t
    must_not_trap 51, vmseq.vv v0, v2, v3, v0.t
    must_trap 52, vredsum.vs v8, v0, v3, v0.t
    must_not_trap 53, vredsum.vs v0, v2, v3, v0.t

    /* A mask destination may overlap a source group only at its lowest
     * register; the mask generators may not overlap their source at all.
     */
    vsetvli t0, a0, e8, m2, tu, mu
    must_trap 33, vmseq.vv v3, v2, v4
    must_not_trap 34, vmseq.vv v2, v2, v4
    must_trap 35, vmsbf.m v2, v2
    must_not_trap 36, vmsbf.m v6, v2
    must_trap 37, viota.m v4, v5
    must_not_trap 38, viota.m v4, v7
    must_trap 39, vmsbf.m v0, v2, v0.t

    /* One register cannot be read at two EEWs: the wide vs2 group of a .wv
     * form and a widening reduction's scalar may not overlap their narrow
     * operand.
     */
    vsetvli t0, a0, e8, m1, tu, mu
    must_trap 40, vwadd.wv v4, v4, v4
    must_not_trap 41, vwadd.wv v4, v4, v6
    must_trap 42, vnsra.wv v8, v4, v5
    must_trap 43, vwredsum.vs v8, v4, v4
    must_not_trap 44, vwredsum.vs v8, v4, v5

    /* vwmacc reads vd at 2*SEW, so a narrow source may not share a register
     * with it, unlike the plain widening add where the top half is fine.
     */
    must_not_trap 54, vwadd.vv v8, v9, v10
    must_trap 55, vwmaccsu.vv v8, v9, v10
    must_trap 56, vwmacc.vx v8, a1, v9
    must_not_trap 57, vwmacc.vx v8, a1, v10

    /* A reduction's vd and vs1 hold one element, so only vs2 is a group; it
     * also needs vstart 0, floating point included.
     */
    vsetvli t0, a0, e8, m2, tu, mu
    must_not_trap 45, vredsum.vs v9, v2, v11
    vsetvli t0, a0, e32, m2, tu, mu
    must_not_trap 46, vfredusum.vs v9, v2, v11
    li   t0, 1
    csrw vstart, t0
    must_trap 47, vfredusum.vs v8, v2, v10
    csrw vstart, zero

    /* A masked load cannot write v0, its own mask, whatever its addressing
     * mode, nor can a masked store take its data, or an indexed access its index,
     * from v0; unmasked, or with
     * another register, either is fine.
     */
    vsetvli t0, a0, e8, m1, tu, mu
    la   a1, src
    li   a2, 1
    must_trap 58, vle8.v v0, (a1), v0.t
    must_trap 59, vlse8.v v0, (a1), a2, v0.t
    must_trap 60, vluxei8.v v0, (a1), v3, v0.t
    must_trap 61, vle8ff.v v0, (a1), v0.t
    must_not_trap 62, vle8.v v0, (a1)
    must_not_trap 63, vle8.v v9, (a1), v0.t
    must_trap 64, vse8.v v0, (a1), v0.t
    must_not_trap 65, vse8.v v0, (a1)
    must_trap 66, vluxei8.v v8, (a1), v0, v0.t
    must_trap 67, vsoxei8.v v8, (a1), v0, v0.t
    must_not_trap 68, vluxei8.v v8, (a1), v0

    /* vfslide1up cannot overlap its source, and a three-field segment is
     * legal.
     */
    vsetvli t0, a0, e32, m1, tu, mu
    must_trap 48, vfslide1up.vf v2, v2, ft0
    vsetvli t0, a0, e8, m1, tu, mu
    la   a1, seg
    must_not_trap 49, vlseg3e8.v v12, (a1)

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
