/* Alternate both outcomes of every conditional branch while definitions and
 * loads cross the edge. Some successors are also entered from another jump.
 */
#ifdef USE_RVC
    .option rvc
#else
    .option norvc
#endif
    .text
    .global _start
    .macro branch_case op, taken_when_one
    lw a0, 0(s3)
    xor a0, a0, s0
    andi a0, a0, 1
    li a1, 1
    mv a4, s1
    \op a0, a1, 1f
    addi s1, s1, 1
    j 2f
1:
    lw a2, 0(s3)
    addi a2, a2, 1
    add s1, s1, a2
2:
    sub a4, s1, a4
    .if \taken_when_one
    addi a3, a0, 1
    .else
    li a3, 2
    sub a3, a3, a0
    .endif
    bne a4, a3, fail
    .endm
_start:
    li s0, 20000
    li s1, 0
    la s3, input
#ifdef CHECK_CYCLES
    rdcycle s4
    rdinstret s5
#endif
loop:
    branch_case beq, 1
    branch_case bne, 0
    branch_case blt, 0
    branch_case bge, 1
    branch_case bltu, 0
    branch_case bgeu, 1
    addi s0, s0, -1
    bnez s0, loop
#ifdef CHECK_CYCLES
    rdcycle t1
    rdinstret t2
    sub t1, t1, s4
    sub t2, t2, s5
    /* Complementary conditions take three edges of each kind per iteration.
     * The six cases execute 72 instructions, followed by ADDI and BNE.
     */
    li t0, 74 * 20000 + 2
    bne t1, t0, fail
    bne t2, t0, fail
#endif
    li t0, 180000
    bne s1, t0, fail
    li a0, 1
    la a1, passed
    li a2, 24
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
    .ascii "JIT branch successor OK\n"
    .balign 4
input:
    .word 1
