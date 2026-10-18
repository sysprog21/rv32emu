/* ADD followed by a carry test, including aliases that require a comparison.
 * Repetition reaches native code; each check is independent of host flags.
 */
    .option norvc
    .text
    .global _start
    .macro carry_case left, right, result
    .pushsection .rodata
    .balign 4
991:
    .word \left, \right
    .popsection
    la a5, 991b
    lw a0, 0(a5)
    lw a1, 4(a5)
    add a2, a0, a1
    sltu a3, a2, a0
    li a4, \result
    bne a3, a4, fail
    /* The other input is equally valid for detecting carry. */
    add a2, a0, a1
    sltu a3, a2, a1
    bne a3, a4, fail
    /* The flag destination may overwrite either input or the sum. */
    add a2, a0, a1
    sltu a2, a2, a0
    bne a2, a4, fail
    add a2, a0, a1
    sltu a0, a2, a1
    bne a0, a4, fail
    .endm
_start:
    li s0, 20000
loop:
    carry_case 0, 0, 0
    carry_case -1, 0, 0
    carry_case -1, 1, 1
    carry_case 0x80000000, 0x80000000, 1
    carry_case 0x7fffffff, 1, 0
    carry_case -1, -1, 1
    /* Comparing the sum to itself must stay false, even after overflow. */
    la t6, inputs
    lw a0, 0(t6)
    lw a1, 4(t6)
    add a0, a0, a1
    sltu a2, a0, a0
    bnez a2, fail
    /* An overwritten input no longer denotes its pre-addition value. */
    lw a0, 0(t6)
    add a0, a0, a1
    sltu a2, a0, a1
    li a3, 1
    bne a2, a3, fail
    /* An unrelated operand requires its own comparison. */
    lw a0, 4(t6)
    lw a1, 4(t6)
    lw a3, 8(t6)
    add a2, a0, a1
    sltu a4, a2, a3
    li a5, 1
    bne a4, a5, fail
    /* Discarding the comparison result must leave x0 intact. */
    add a2, a0, a1
    sltu zero, a2, a0
    bnez zero, fail
    addi s0, s0, -1
    bnez s0, loop
    li a0, 1
    la a1, passed
    li a2, 13
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
    .ascii "JIT carry OK\n"
    .balign 4
inputs:
    .word -1, 1, 3
