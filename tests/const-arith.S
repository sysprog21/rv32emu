    .option norvc
    .text
    .global _start

    .macro check_unsigned shift, dst
    lw t0, 0(s1)
    lw t1, (4 * \shift)(s2)
    srl t3, t0, t1
    li t2, (1 << \shift)
    divu \dst, t0, t2
    bne \dst, t3, fail

    lw t0, 0(s1)
    li t2, 1
    sll t3, t2, t1
    addi t3, t3, -1
    and t3, t0, t3
    li t2, (1 << \shift)
    remu \dst, t0, t2
    bne \dst, t3, fail
    .endm

_start:
    li s0, 2000
    la s2, shifts
loop:
    la s1, dividends
    li s3, 7
next:
    .irp shift, 0, 1, 5, 16, 31
    .irp dst, t0, t2, t4
    check_unsigned \shift, \dst
    .endr
    .endr
    addi s1, s1, 4
    addi s3, s3, -1
    bnez s3, next

    .irp dst, t0, t1, t2
    li t0, -1
    li t1, -1
    mulhu \dst, t0, t1
    li t3, -2
    bne \dst, t3, fail
    .endr
    addi s0, s0, -1
    bnez s0, loop

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
    .ascii "constant arithmetic OK\n"
    .balign 4
dividends:
    .word 0, 1, 31, 32, 0x7fffffff, 0x80000000, 0xffffffff
shifts:
    .set shift, 0
    .rept 32
    .word shift
    .set shift, shift + 1
    .endr
