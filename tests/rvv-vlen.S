/*
 * VLEN-parametric RVV checks.
 *
 * tests/rvv-smoke.S assumes VLEN=128. This program is assembled with
 * -DVLEN=<n> and covers what depends on the vector register width: vlenb,
 * VLMAX for several SEW/LMUL pairs, loads and stores of whole registers, the
 * whole-register moves, and arithmetic, indexing and slides that must reach
 * the last lane of a register.
 *
 * A failing case exits with its own status and prints "RVV vlen FAIL".
 */
    /* Nothing sets gp in this freestanding program, so keep la PC-relative. */
    .option norelax

    .equ SYSWRITE, 64
    .equ SYSEXIT, 93
    .equ VLENB, VLEN / 8

.macro expect reg, imm, id
    li   t6, \imm
    li   a3, \id
    bne  \reg, t6, fail
.endm

    .section .rodata
ok_msg:
    .ascii "RVV vlen OK\n"
    .equ ok_len, . - ok_msg
fail_msg:
    .ascii "RVV vlen FAIL\n"
    .equ fail_len, . - fail_msg

    .section .bss
    .balign 16
src:
    .space VLENB * 8
dst:
    .space VLENB * 8

    .text
    .global _start
_start:
    # vlenb mirrors VLEN.
    csrr t1, vlenb
    expect t1, VLENB, 1

    # VLMAX = VLEN / SEW * LMUL. rs1=x0 with rd!=x0 requests VLMAX.
    vsetvli t1, x0, e8, m1, ta, ma
    expect t1, VLEN / 8, 2
    vsetvli t1, x0, e16, m2, ta, ma
    expect t1, VLEN / 16 * 2, 3
    vsetvli t1, x0, e32, m1, ta, ma
    expect t1, VLEN / 32, 4
    vsetvli t1, x0, e32, m8, ta, ma
    expect t1, VLEN / 32 * 8, 5
    vsetvli t1, x0, e16, m4, ta, ma
    expect t1, VLEN / 16 * 4, 6
    vsetvli t1, x0, e8, mf8, ta, ma
    expect t1, VLEN / 64, 7

    # A request above VLMAX is clamped.
    li   a0, 0x7fffffff
    vsetvli t1, a0, e32, m1, ta, ma
    expect t1, VLEN / 32, 8

    # src[i] = i * 7 + 3 for 8 registers' worth of bytes.
    la   a1, src
    li   t0, VLENB * 8
    li   t2, 0
1:
    li   t3, 7
    mul  t4, t2, t3
    addi t4, t4, 3
    add  t5, a1, t2
    sb   t4, 0(t5)
    addi t2, t2, 1
    bne  t2, t0, 1b

    # Whole-register load/store round trip, 1 and 4 registers, with vl=0 to
    # show that vl is ignored.
    vsetivli x0, 0, e8, m1, ta, ma
    la   a1, src
    la   a2, dst
    vl4re8.v v8, (a1)
    vs4r.v v8, (a2)
    li   a3, 9
    li   t0, VLENB * 4
    call compare

    # vmv<nr>r.v copies NREG whole registers regardless of vl (still 0 here).
    la   a1, src
    la   a2, dst
    vl8re8.v v8, (a1)
    vmv1r.v v24, v8
    vmv1r.v v25, v9
    vmv2r.v v26, v10
    vs4r.v v24, (a2)
    li   a3, 10
    li   t0, VLENB * 4
    call compare
    vmv4r.v v28, v8
    la   a2, dst
    vs4r.v v28, (a2)
    li   a3, 11
    li   t0, VLENB * 4
    call compare
    vmv8r.v v16, v8
    la   a2, dst
    vs8r.v v16, (a2)
    la   a1, src
    li   a3, 18
    li   t0, VLENB * 8
    call compare

    # Resume a two-register move in its second register at SEW=32.
    # Everything before vstart must retain its sentinel value.
    vsetvli t1, zero, e32, m2, tu, mu
    vmv.v.i v16, -1
    li   t2, VLEN / 32 + 1
    csrw vstart, t2
    vmv2r.v v16, v8
    csrr t1, vstart
    expect t1, 0, 19
    la   a2, dst
    vs2r.v v16, (a2)
    li   t0, VLENB + 4
    li   t2, 0
1:
    add  t5, a2, t2
    lbu  t1, 0(t5)
    expect t1, 255, 20
    addi t2, t2, 1
    bne  t2, t0, 1b
    la   a1, src
    add  a1, a1, t0
    add  a2, a2, t0
    li   t0, VLENB - 4
    li   a3, 21
    call compare

    # Vector instructions with an integer destination must discard x0 writes.
    vsetivli zero, 1, e32, m1, tu, mu
    vmv.v.i v20, -1
    vmv.x.s zero, v20
    vcpop.m zero, v20
    vmv.v.i v20, 0
    vfirst.m zero, v20
    la   a2, dst
    sw   zero, 0(a2)
    lw   t1, 0(a2)
    expect t1, 0, 22

    # Last lane of an SEW=8 register: add one to every byte.
    vsetvli t1, x0, e8, m1, ta, ma
    la   a1, src
    vle8.v v8, (a1)
    vadd.vi v9, v8, 1
    la   a2, dst
    vse8.v v9, (a2)
    li   t0, VLENB - 1
    add  t5, a2, t0
    lbu  t1, 0(t5)
    add  t5, a1, t0
    lbu  t2, 0(t5)
    addi t2, t2, 1
    andi t2, t2, 0xff
    li   a3, 12
    bne  t1, t2, fail

    # vid.v indexes every lane, including the last at SEW=32.
    vsetvli t1, x0, e32, m1, ta, ma
    vid.v v10
    la   a2, dst
    vse32.v v10, (a2)
    li   t0, (VLEN / 32 - 1) * 4
    add  t5, a2, t0
    lw   t1, 0(t5)
    expect t1, VLEN / 32 - 1, 13

    # vslidedown by VLMAX-1 moves the last lane to lane 0.
    li   t0, VLEN / 32 - 1
    vslidedown.vx v11, v10, t0
    vmv.x.s t1, v11
    expect t1, VLEN / 32 - 1, 14

    # vslideup reaches the last lane.
    vmv.v.i v12, 0
    vmv.v.i v13, 5
    vslideup.vx v12, v13, t0
    vse32.v v12, (a2)
    li   t0, (VLEN / 32 - 1) * 4
    add  t5, a2, t0
    lw   t1, 0(t5)
    expect t1, 5, 15

    # A mask register spans the full VLEN: vmset.m then vcpop.m counts VLMAX.
    vsetvli t1, x0, e8, m1, ta, ma
    vmset.m v0
    vcpop.m t1, v0
    expect t1, VLEN / 8, 16

    # vmv.v.x at LMUL=8 fills all eight registers.
    vsetvli t1, x0, e32, m8, ta, ma
    li   t0, 0x12345678
    vmv.v.x v8, t0
    la   a2, dst
    vs8r.v v8, (a2)
    li   t3, VLENB * 8 - 4
    add  t5, a2, t3
    lw   t1, 0(t5)
    expect t1, 0x12345678, 17

    # Success.
    li   a7, SYSWRITE
    li   a0, 1
    la   a1, ok_msg
    li   a2, ok_len
    ecall
    li   a7, SYSEXIT
    li   a0, 0
    ecall

/* Compare t0 bytes at src (a1) and dst (a2); a3 is the failure status. */
compare:
    li   t2, 0
1:
    add  t3, a1, t2
    lbu  t4, 0(t3)
    add  t3, a2, t2
    lbu  t5, 0(t3)
    bne  t4, t5, fail
    addi t2, t2, 1
    bne  t2, t0, 1b
    ret

fail:
    mv   s0, a3
    li   a7, SYSWRITE
    li   a0, 1
    la   a1, fail_msg
    li   a2, fail_len
    ecall
    li   a7, SYSEXIT
    mv   a0, s0
    ecall
