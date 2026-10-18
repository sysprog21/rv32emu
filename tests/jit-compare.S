/* Exact boolean results for signed/unsigned comparisons, including partial
 * register writes, destination aliases, and immediate sign extension.
 */
    .option norvc
    .text
    .global _start
    .macro reg_check op, offset
    lw t0, 0(s1)
    lw t1, 4(s1)
    lw t3, \offset(s1)
    li t2, -1
    \op t2, t0, t1
    bne t2, t3, fail
    \op t0, t0, t1
    bne t0, t3, fail
    lw t0, 0(s1)
    \op t1, t0, t1
    bne t1, t3, fail
    .endm
    .macro imm_check op, imm, offset
    lw t0, 0(s1)
    lw t3, \offset(s1)
    li t2, -1
    \op t2, t0, \imm
    bne t2, t3, fail
    \op t0, t0, \imm
    bne t0, t3, fail
    .endm
_start:
    li s0, 1000
outer:
    la s1, operands
    li s2, 16
loop:
    reg_check slt, 8
    reg_check sltu, 12
    imm_check slti, -2048, 16
    imm_check sltiu, -2048, 20
    imm_check slti, -1, 24
    imm_check sltiu, -1, 28
    imm_check slti, 0, 32
    imm_check sltiu, 0, 36
    imm_check slti, 1, 40
    imm_check sltiu, 1, 44
    imm_check slti, 2047, 48
    imm_check sltiu, 2047, 52
    addi s1, s1, 56
    addi s2, s2, -1
    bnez s2, loop
    addi s0, s0, -1
    bnez s0, outer
    li a0, 1
    la a1, passed
    li a2, 15
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
    .ascii "JIT compare OK\n"
    .balign 4
operands:
    .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
    .word 0x00000000, 0x00000001, 0x00000000, 0x00000001
    .word 0x00000000, 0x00000000, 0x00000001, 0x00000001
    .word 0x00000001, 0x00000001
    .word 0x00000001, 0xffffffff, 0x00000000, 0x00000001
    .word 0x00000000, 0x00000001, 0x00000000, 0x00000001
    .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
    .word 0x00000001, 0x00000001
    .word 0xffffffff, 0xffffffff, 0x00000000, 0x00000000
    .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
    .word 0x00000001, 0x00000000, 0x00000001, 0x00000000
    .word 0x00000001, 0x00000000
    .word 0x80000000, 0xffffffff, 0x00000001, 0x00000001
    .word 0x00000001, 0x00000001, 0x00000001, 0x00000001
    .word 0x00000001, 0x00000000, 0x00000001, 0x00000000
    .word 0x00000001, 0x00000000
    .word 0x80000001, 0x00000003, 0x00000001, 0x00000000
    .word 0x00000001, 0x00000001, 0x00000001, 0x00000001
    .word 0x00000001, 0x00000000, 0x00000001, 0x00000000
    .word 0x00000001, 0x00000000
    .word 0x7fffffff, 0x7fffffff, 0x00000000, 0x00000000
    .word 0x00000000, 0x00000001, 0x00000000, 0x00000001
    .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
    .word 0x00000000, 0x00000000
    .word 0x00010001, 0x00010001, 0x00000000, 0x00000000
    .word 0x00000000, 0x00000001, 0x00000000, 0x00000001
    .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
    .word 0x00000000, 0x00000000
    .word 0x89abcdef, 0x13579bdf, 0x00000001, 0x00000000
    .word 0x00000001, 0x00000001, 0x00000001, 0x00000001
    .word 0x00000001, 0x00000000, 0x00000001, 0x00000000
    .word 0x00000001, 0x00000000
    .word 0x00000007, 0xfffffffe, 0x00000000, 0x00000001
    .word 0x00000000, 0x00000001, 0x00000000, 0x00000001
    .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
    .word 0x00000001, 0x00000001
    .word 0xfffffff9, 0x00000002, 0x00000001, 0x00000000
    .word 0x00000000, 0x00000000, 0x00000001, 0x00000001
    .word 0x00000001, 0x00000000, 0x00000001, 0x00000000
    .word 0x00000001, 0x00000000
    .word 0xfffffff9, 0xfffffffe, 0x00000001, 0x00000001
    .word 0x00000000, 0x00000000, 0x00000001, 0x00000001
    .word 0x00000001, 0x00000000, 0x00000001, 0x00000000
    .word 0x00000001, 0x00000000
    .word 0x80000000, 0x00000000, 0x00000001, 0x00000000
    .word 0x00000001, 0x00000001, 0x00000001, 0x00000001
    .word 0x00000001, 0x00000000, 0x00000001, 0x00000000
    .word 0x00000001, 0x00000000
    .word 0x7fffffff, 0x80000000, 0x00000000, 0x00000001
    .word 0x00000000, 0x00000001, 0x00000000, 0x00000001
    .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
    .word 0x00000000, 0x00000000
    .word 0xffffffff, 0x80000000, 0x00000000, 0x00000000
    .word 0x00000000, 0x00000000, 0x00000000, 0x00000000
    .word 0x00000001, 0x00000000, 0x00000001, 0x00000000
    .word 0x00000001, 0x00000000
    .word 0x80000000, 0x00000001, 0x00000001, 0x00000000
    .word 0x00000001, 0x00000001, 0x00000001, 0x00000001
    .word 0x00000001, 0x00000000, 0x00000001, 0x00000000
    .word 0x00000001, 0x00000000
    .word 0x00000000, 0x80000000, 0x00000000, 0x00000001
    .word 0x00000000, 0x00000001, 0x00000000, 0x00000001
    .word 0x00000000, 0x00000000, 0x00000001, 0x00000001
    .word 0x00000001, 0x00000001
