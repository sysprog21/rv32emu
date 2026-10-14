    .option norvc
    .text
    .global _start

    .macro check op, immop, count, dst
    lw t0, 0(s1)
    \immop t3, t0, (\count & 31)
    li t1, \count
    \op \dst, t0, t1
    bne \dst, t3, fail
    .endm

_start:
    li s0, 2000
loop:
    la s1, values
    li s2, 5
next:
    .irp count, 0, 1, 31, 32, 33, 63, 0x80000000, 0xffffffff
    .irp dst, t0, t1, t2
    check sll, slli, \count, \dst
    check srl, srli, \count, \dst
    check sra, srai, \count, \dst
    .endr
    .endr
    addi s1, s1, 4
    addi s2, s2, -1
    bnez s2, next
    addi s0, s0, -1
    bnez s0, loop

    li a0, 1
    la a1, passed
    li a2, 19
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
    .ascii "constant shifts OK\n"
    .balign 4
values:
    .word 0, 1, 0x7fffffff, 0x80000000, 0xffffffff
