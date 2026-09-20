/* Freestanding RV32I regression for T1 two-operand ALU and shift lowering.
 * Build with -march=rv32i -mabi=ilp32 -nostdlib -static -Wl,-e,_start.
 * Repeat to exercise interpreted warm-up and compiled execution.
 */
    .option norvc
    .text
    .global _start

    .macro check op, rd, rs1, rs2, expected
    /* Load at runtime so constant folding cannot replace the tested ALU. */
    la t4, operands
    lw t0, 0(t4)
    lw t1, 4(t4)
    \op \rd, \rs1, \rs2
    li t3, \expected
    bne \rd, t3, fail
    .endm

    .macro aliases op, normal, same
    check \op, t2, t0, t1, \normal
    check \op, t0, t0, t1, \normal
    check \op, t1, t0, t1, \normal
    check \op, t2, t0, t0, \same
    check \op, t0, t0, t0, \same
    check \op, zero, t0, t1, 0
    .endm

    /* Dirty operands must survive writeback on either successor. Load first
     * to prevent constant folding, then cross the signed boundary with ADDI.
     */
    .macro branch op, lhs, rhs, expected
    la t4, branch_operands
    lw t0, 0(t4)
    lw t1, 4(t4)
    addi t0, t0, 1
    addi t1, t1, 1
    \op \lhs, \rhs, 1f
    li t2, 0
    j 2f
1:
    li t2, 1
2:
    li t3, \expected
    bne t2, t3, fail
    li t3, 0x80000000
    bne t0, t3, fail
    li t3, 1
    bne t1, t3, fail
    .endm

_start:
    li s0, 20000
loop:
    aliases add, 0x9d0369ce, 0x13579bde
    aliases sub, 0x76543210, 0
    aliases xor, 0x9afc5630, 0
    aliases or, 0x9bffdfff, 0x89abcdef
    aliases and, 0x010389cf, 0x89abcdef
    /* Shifts and compares lower through their own macros, each doing its own
     * source-overwrite handling, so alias them too. Every expected value
     * differs from both operands, so a stale source changes the result.
     */
    aliases sll, 0x80000000, 0xe6f78000
    aliases srl, 0x00000001, 0x00011357
    aliases sra, 0xffffffff, 0xffff1357
    aliases slt, 1, 0
    aliases sltu, 0, 0
    branch beq, t0, t0, 1
    branch beq, t0, t1, 0
    branch bne, t0, t1, 1
    branch bne, t0, t0, 0
    branch blt, t0, t1, 1
    branch blt, t1, t0, 0
    branch bge, t1, t0, 1
    branch bge, t0, t1, 0
    branch bltu, t1, t0, 1
    branch bltu, t0, t1, 0
    branch bgeu, t0, t1, 1
    branch bgeu, t1, t0, 0
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
    .ascii "JIT ALU aliases OK\n"
    .balign 4
operands:
    .word 0x89abcdef, 0x13579bdf
branch_operands:
    .word 0x7fffffff, 0
