/* Register shifts must match repeated one-bit immediate shifts for every
 * masked count, through interpreter warmup and tier-1 execution. Exercise
 * both destination aliases and preserve live values under register pressure.
 */
    .option norvc
    .text
    .global _start

    .macro check op, expected
    lw t0, 0(s1)
    lw t1, 0(s2)
    \op t2, t0, t1
    bne t2, \expected, fail
    \op t0, t0, t1
    bne t0, \expected, fail
    lw t0, 0(s1)
    \op t1, t0, t1
    bne t1, \expected, fail

    lw t0, 0(s1)
    lw t1, 0(s2)
    .irp reg, a0, a1, a2, a6, a7, t4, t5, t6, ra
    lw \reg, 0(s1)
    .endr
    \op t2, t0, t1
    xor t2, t2, \expected
    .irp reg, a0, a1, a2, a6, a7, t4, t5, t6, ra
    xor t2, t2, \reg
    .endr
    /* Nine unchanged operands leave exactly one copy of the input. */
    bne t2, t0, fail
    lw t3, 0(s1)
    bne t0, t3, fail
    lw t3, 0(s2)
    bne t1, t3, fail
    \op zero, t0, t1
    add t2, zero, t0
    bne t2, t0, fail
    .endm

_start:
    li s0, 500
outer:
    la s1, values
    li s3, 6
value_loop:
    la s2, counts
    li s4, 12
count_loop:
    lw a3, 0(s1)
    mv a4, a3
    mv a5, a3
    lw t3, 0(s2)
    andi t3, t3, 31
    beqz t3, check_results
reference:
    slli a3, a3, 1
    srli a4, a4, 1
    srai a5, a5, 1
    addi t3, t3, -1
    bnez t3, reference
check_results:
    check sll, a3
    check srl, a4
    check sra, a5
    addi s2, s2, 4
    addi s4, s4, -1
    bnez s4, count_loop
    addi s1, s1, 4
    addi s3, s3, -1
    bnez s3, value_loop
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
    .ascii "JIT register shifts OK\n"
    .balign 4
values:
    .word 0, 1, 0x7fffffff, 0x80000000, 0x89abcdef, 0xffffffff
counts:
    .word 0, 1, 7, 31, 32, 33, 63, 64, 0x80000000, 0x80000001
    .word 0xfffffffe, 0xffffffff
