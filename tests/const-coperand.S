/*
 * Compressed register-register operations with one operand known inside the
 * block.
 *
 * Constant propagation rewrites C.ADD, C.SUB and C.AND with a known rs2 into
 * 2-byte immediate forms. Each case compares the result against the same
 * operation on a copy of the constant that went through memory, which the pass
 * cannot see, with the known value in either operand.
 */
    .option rvc
    .text
    .global _start

    .macro check op, k
    /* rs2 known */
    lw a0, 0(s1)
    li t4, \k
    sw t4, 0(s3)
    lw a2, 0(s3)
    \op a3, a0, a2
    li a1, \k
    c.\op a0, a1
    bne a0, a3, fail
    /* rs1, the destination, known */
    lw a2, 0(s1)
    lw a4, 0(s3)
    \op a3, a4, a2
    li a0, \k
    c.\op a0, a2
    bne a0, a3, fail
    .endm

_start:
    li s0, 1000
    la s3, scratch
loop:
    la s1, values
    li s2, 5
next:
    .irp op, add, sub, and, or, xor
    .irp k, 0, 1, 16, -1, 0x800, 0x7fffffff, 0x80000000
    check \op, \k
    .endr
    .endr
    addi s1, s1, 4
    addi s2, s2, -1
    bnez s2, next
    addi s0, s0, -1
    bnez s0, loop

    li a0, 1
    la a1, passed
    li a2, 32
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
    .ascii "compressed constant operands OK\n"
    .balign 4
values:
    .word 0, 1, 0x7fffffff, 0x80000000, 0xfffffffe

    .bss
    .balign 4
scratch:
    .space 4
