    .option norvc
    .text
    .global _start

    .macro check dst, lhs, rhs, offset
    lw t0, 0(s1)
    lw t1, 4(s1)
    lw t3, \offset(s1)
    mul \dst, \lhs, \rhs
    bne \dst, t3, fail
    .endm

_start:
    li s0, 2000
    .irp reg, s3, s4, s5, s6, s7, s8, s9, s10, s11
    li \reg, 0x76543210
    .endr
loop:
    la s1, operands
    li s2, 8
next:
    check t2, t0, t1, 8
    lw t3, 0(s1)
    bne t0, t3, fail
    lw t3, 4(s1)
    bne t1, t3, fail
    check t0, t0, t1, 8
    check t1, t0, t1, 8
    check t2, t0, t0, 12
    check t0, t0, t0, 12

    /* Keep twelve more registers live across MUL within one block, so the JIT
     * allocates the product while host registers are scarce. Each holds the
     * same value; an even count cancels in the XOR chain unless one was
     * clobbered.
     */
    lw t0, 0(s1)
    lw t1, 4(s1)
    lw t3, 8(s1)
    .irp reg, a0, a1, a2, a3, a4, a5, a6, a7, t4, t5, t6, ra
    lw \reg, 0(s1)
    .endr
    mul t2, t0, t1
    xor t2, t2, t3
    .irp reg, a0, a1, a2, a3, a4, a5, a6, a7, t4, t5, t6, ra
    xor t2, t2, \reg
    .endr
    bnez t2, fail
    lw t3, 0(s1)
    bne t0, t3, fail
    lw t3, 4(s1)
    bne t1, t3, fail

    lw t0, 0(s1)
    lw t1, 4(s1)
    mul zero, t0, t1
    lw t3, 0(s1)
    bne t0, t3, fail
    lw t3, 4(s1)
    bne t1, t3, fail
    bnez zero, fail
    mul t2, t0, zero
    bnez t2, fail
    mul t2, zero, t1
    bnez t2, fail

    li t3, 0x76543210
    .irp reg, s3, s4, s5, s6, s7, s8, s9, s10, s11
    bne \reg, t3, fail
    .endr
    addi s1, s1, 16
    addi s2, s2, -1
    bnez s2, next
    addi s0, s0, -1
    bnez s0, loop

    li a0, 1
    la a1, passed
    li a2, 16
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
    .ascii "JIT multiply OK\n"
    .balign 4
operands:
    .word 0, 0xffffffff, 0, 0
    .word 1, 0xffffffff, 0xffffffff, 1
    .word 0xffffffff, 0xffffffff, 1, 1
    .word 0x80000000, 2, 0, 0
    .word 0x80000001, 3, 0x80000003, 1
    .word 0x7fffffff, 0x7fffffff, 1, 1
    .word 0x00010001, 0x00010001, 0x00020001, 0x00020001
    .word 0x89abcdef, 0x13579bdf, 0xab911831, 0x90f2a521
