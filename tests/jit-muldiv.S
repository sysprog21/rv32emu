/* Widening multiplies and native-width division under register pressure.
 * Expected values use full-width products and truncation toward zero.
 * Each operation covers destination/source aliases and preservation of other
 * cached registers, through interpreter warmup and tier-1 execution.
 */
    .option norvc
    .text
    .global _start

    .macro check op, offset
    lw t0, 0(s1)
    lw t1, 4(s1)
    lw t3, \offset(s1)
    \op t2, t0, t1
    bne t2, t3, fail
    \op t0, t0, t1
    bne t0, t3, fail
    lw t0, 0(s1)
    \op t1, t0, t1
    bne t1, t3, fail

    /* Keep enough operands live to occupy and evict fixed host registers. */
    lw t0, 0(s1)
    lw t1, 4(s1)
    .irp reg, a0, a1, a2, a3, a4, a5, a6, a7, t4, t5, t6, ra
    lw \reg, 0(s1)
    .endr
    \op t2, t0, t1
    xor t2, t2, t3
    .irp reg, a0, a1, a2, a3, a4, a5, a6, a7, t4, t5, t6, ra
    xor t2, t2, \reg
    .endr
    bnez t2, fail
    lw t3, 0(s1)
    bne t0, t3, fail
    lw t3, 4(s1)
    bne t1, t3, fail
    \op zero, t0, t1
    add t2, zero, t0
    bne t2, t0, fail
    bnez zero, fail
    lw t3, 0(s1)
    bne t0, t3, fail
    lw t3, 4(s1)
    bne t1, t3, fail
    .endm

_start:
    li s0, 500
outer:
    la s1, operands
    li s2, 16
loop:
    check mulh, 8
    check mulhsu, 12
    check mulhu, 16
    check div, 20
    check divu, 24
    check rem, 28
    check remu, 32
    addi s1, s1, 36
    addi s2, s2, -1
    bnez s2, loop
    addi s0, s0, -1
    bnez s0, outer
    li a0, 1
    la a1, passed
    li a2, 23
    li a7, 64
    ecall
    li a0, 0
    j exit
fail:
    li a0, 1
exit:
    li a7, 93
    ecall
    .section .rodata
passed:
    .ascii "JIT multiply/divide OK\n"
    .balign 4
operands:
    .word 0x00000000, 0x00000000, 0x00000000
    .word 0x00000000, 0x00000000, 0xffffffff
    .word 0xffffffff, 0x00000000, 0x00000000
    .word 0x00000001, 0xffffffff, 0xffffffff
    .word 0x00000000, 0x00000000, 0xffffffff
    .word 0x00000000, 0x00000000, 0x00000001
    .word 0xffffffff, 0xffffffff, 0x00000000
    .word 0xffffffff, 0xfffffffe, 0x00000001
    .word 0x00000001, 0x00000000, 0x00000000
    .word 0x80000000, 0xffffffff, 0x00000000
    .word 0x80000000, 0x7fffffff, 0x80000000
    .word 0x00000000, 0x00000000, 0x80000000
    .word 0x80000001, 0x00000003, 0xfffffffe
    .word 0xfffffffe, 0x00000001, 0xd5555556
    .word 0x2aaaaaab, 0xffffffff, 0x00000000
    .word 0x7fffffff, 0x7fffffff, 0x3fffffff
    .word 0x3fffffff, 0x3fffffff, 0x00000001
    .word 0x00000001, 0x00000000, 0x00000000
    .word 0x00010001, 0x00010001, 0x00000001
    .word 0x00000001, 0x00000001, 0x00000001
    .word 0x00000001, 0x00000000, 0x00000000
    .word 0x89abcdef, 0x13579bdf, 0xf70f419f
    .word 0xf70f419f, 0x0a66dd7e, 0xfffffffa
    .word 0x00000007, 0xfdb97529, 0x02468ad6
    .word 0x00000007, 0xfffffffe, 0xffffffff
    .word 0x00000006, 0x00000006, 0xfffffffd
    .word 0x00000000, 0x00000001, 0x00000007
    .word 0xfffffff9, 0x00000002, 0xffffffff
    .word 0xffffffff, 0x00000001, 0xfffffffd
    .word 0x7ffffffc, 0xffffffff, 0x00000001
    .word 0xfffffff9, 0xfffffffe, 0x00000000
    .word 0xfffffff9, 0xfffffff7, 0x00000003
    .word 0x00000000, 0xffffffff, 0xfffffff9
    .word 0x80000000, 0x00000000, 0x00000000
    .word 0x00000000, 0x00000000, 0xffffffff
    .word 0xffffffff, 0x80000000, 0x80000000
    .word 0x7fffffff, 0x80000000, 0xc0000000
    .word 0x3fffffff, 0x3fffffff, 0x00000000
    .word 0x00000000, 0x7fffffff, 0x7fffffff
    .word 0xffffffff, 0x80000000, 0x00000000
    .word 0xffffffff, 0x7fffffff, 0x00000000
    .word 0x00000001, 0xffffffff, 0x7fffffff
    .word 0x80000000, 0x00000001, 0xffffffff
    .word 0xffffffff, 0x00000000, 0x80000000
    .word 0x80000000, 0x00000000, 0x00000000
    .word 0x00000000, 0x80000000, 0x00000000
    .word 0x00000000, 0x00000000, 0x00000000
    .word 0x00000000, 0x00000000, 0x00000000
