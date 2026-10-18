/* Scaled address arithmetic, including fused shift prefixes and carry users.
 * Inputs are loaded at run time; repeated ADD provides an independent oracle.
 */
    .option norvc
    .text
    .global _start
    .macro scaled_case scale
    mv t0, a0
    .rept \scale
    add t0, t0, t0
    .endr
    add t0, t0, a1
    slli a2, a0, \scale
    add a2, a2, a1
    bne a2, t0, fail
    slli a2, a0, \scale
    add a2, a1, a2
    bne a2, t0, fail
    mv a2, a0
    slli a2, a2, \scale
    add a2, a2, a1
    bne a2, t0, fail
    /* The shift prefix must execute before its final scaled addition. */
    srli t0, a0, 5
    .rept \scale
    add t0, t0, t0
    .endr
    add t0, t0, a1
    srli a2, a0, 5
    slli a2, a2, \scale
    add a2, a1, a2
    bne a2, t0, fail
    /* Both ADD inputs denote the shifted result, rather than an old base. */
    mv t0, a0
    .rept (\scale + 1)
    add t0, t0, t0
    .endr
    slli a2, a0, \scale
    add a2, a2, a2
    bne a2, t0, fail
    .endm
_start:
    li s0, 20000
    li s4, 0
    la s3, inputs
loop:
    lw a0, 0(s3)
    lw a1, 4(s3)
    scaled_case 1
    scaled_case 2
    scaled_case 3
    /* Carry forwarding requires the original addition's flags. */
    slli a2, a0, 1
    add a2, a2, a1
    sltu a3, a2, a1
    li a4, 0
    bgeu a2, a1, no_carry
    li a4, 1
no_carry:
    bne a3, a4, fail
    /* A sign-extended load may leave high host bits set. LEA still writes
     * exactly the low 32 bits of the guest sum.
     */
    lh a0, 0(s3)
    scaled_case 3
    addi s3, s3, 8
    addi s4, s4, 1
    li t0, 4
    bne s4, t0, next
    li s4, 0
    la s3, inputs
next:
    addi s0, s0, -1
    bnez s0, loop
    li a0, 1
    la a1, passed
    li a2, 18
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
    .ascii "JIT scaled add OK\n"
    .balign 4
inputs:
    .word 0x80000001, 0xfffffff0
    .word 0xffffffff, 0xffffffff
    .word 0x7fffffff, 0x7fffffff
    .word 0, 0x80000000
