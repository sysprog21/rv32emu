/*
 * Register-register operations with one operand known inside the block.
 *
 * Constant propagation rewrites them into immediate forms, a constant or a
 * shift (MUL by zero or a power of two). Each case compares the rewritten
 * instruction against the same operation on a copy of the constant that went
 * through memory, which the pass cannot see, with the destination aliasing
 * either operand or neither.
 */
    .option norvc
    .text
    .global _start

    .macro check op, k, dst, lhs_known
    lw t0, 0(s1)
    li t4, \k
    sw t4, 0(s3)
    lw t1, 0(s3)
    .if \lhs_known
    \op t3, t1, t0
    li t1, \k
    \op \dst, t1, t0
    .else
    \op t3, t0, t1
    li t1, \k
    \op \dst, t0, t1
    .endif
    bne \dst, t3, fail
    .endm

_start:
    li s0, 1000
    la s3, scratch
loop:
    la s1, values
    li s2, 5
next:
    .irp op, add, sub, xor, or, and, slt, sltu, mul
    .irp k, 0, 1, 16, -1, 0x800, 0x7fffffff, 0x80000000
    .irp dst, t0, t1, t2
    check \op, \k, \dst, 0
    check \op, \k, \dst, 1
    .endr
    .endr
    .endr
    addi s1, s1, 4
    addi s2, s2, -1
    bnez s2, next
    addi s0, s0, -1
    bnez s0, loop

    li a0, 1
    la a1, passed
    li a2, 21
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
    .ascii "constant operands OK\n"
    .balign 4
values:
    .word 0, 1, 0x7fffffff, 0x80000000, 0xfffffffe

    .bss
    .balign 4
scratch:
    .space 4
