/* Differential sweep: tier-1 code generation must agree with the interpreter.
 *
 * The x86-64 emitter once selected an unsigned divide for signed DIV and REM.
 * That returned wrong quotients for negative operands and, on INT_MIN / -1,
 * overflowed the 32-bit divide and killed the emulator with SIGFPE. Nothing in
 * the suite noticed, because no test applied a JIT-compiled arithmetic
 * instruction to an awkward operand and checked the answer.
 *
 * So do that systematically. Every RV32IM arithmetic form is applied to each
 * ordered pair drawn from a table of awkward values - zero, one, both signs,
 * INT_MIN, INT_MAX, shift amounts either side of the 32-bit boundary - and the
 * results are folded into per-class accumulators. Loads, stores and branches
 * get their own. The expected values are what the interpreter produces, so a
 * divergence in generated code shows up as a mismatch here.
 *
 * Operands are loaded from memory so constant folding cannot answer for the
 * generated code, and the work sits in a small inner loop so the translator
 * stays inside its per-block jump budget and actually compiles it.
 *
 * Assembled for both rv32im and rv32imc, so the compressed encodings of the
 * same program must reach the same results.
 *
 * Generated; see tools/gen-jit-interp-diff.py to regenerate after changing the
 * operand table or the instruction list, which also recomputes the constants.
 */
    .text
    .global _start
_start:
    li s1, 0
    li s2, 0
    li s3, 0
    li s8, 0
    li s9, 0
    li s7, 16
    li s0, 120
outer:
    la s4, values
    li s5, 0
loop_i:
    slli t3, s5, 2
    add t4, s4, t3
    lw t0, 0(t4)
    li s6, 0
loop_j:
    slli t3, s6, 2
    add t4, s4, t3
    lw t1, 0(t4)
    add t2, t0, t1
    slli t5, s1, 1
    srli t6, s1, 31
    or s1, t5, t6
    add s1, s1, t2
    sub t2, t0, t1
    slli t5, s1, 1
    srli t6, s1, 31
    or s1, t5, t6
    add s1, s1, t2
    sll t2, t0, t1
    slli t5, s1, 1
    srli t6, s1, 31
    or s1, t5, t6
    add s1, s1, t2
    slt t2, t0, t1
    slli t5, s1, 1
    srli t6, s1, 31
    or s1, t5, t6
    add s1, s1, t2
    sltu t2, t0, t1
    slli t5, s1, 1
    srli t6, s1, 31
    or s1, t5, t6
    add s1, s1, t2
    xor t2, t0, t1
    slli t5, s1, 1
    srli t6, s1, 31
    or s1, t5, t6
    add s1, s1, t2
    srl t2, t0, t1
    slli t5, s1, 1
    srli t6, s1, 31
    or s1, t5, t6
    add s1, s1, t2
    sra t2, t0, t1
    slli t5, s1, 1
    srli t6, s1, 31
    or s1, t5, t6
    add s1, s1, t2
    or t2, t0, t1
    slli t5, s1, 1
    srli t6, s1, 31
    or s1, t5, t6
    add s1, s1, t2
    and t2, t0, t1
    slli t5, s1, 1
    srli t6, s1, 31
    or s1, t5, t6
    add s1, s1, t2
    mul t2, t0, t1
    slli t5, s3, 1
    srli t6, s3, 31
    or s3, t5, t6
    add s3, s3, t2
    mulh t2, t0, t1
    slli t5, s3, 1
    srli t6, s3, 31
    or s3, t5, t6
    add s3, s3, t2
    mulhsu t2, t0, t1
    slli t5, s3, 1
    srli t6, s3, 31
    or s3, t5, t6
    add s3, s3, t2
    mulhu t2, t0, t1
    slli t5, s3, 1
    srli t6, s3, 31
    or s3, t5, t6
    add s3, s3, t2
    div t2, t0, t1
    slli t5, s3, 1
    srli t6, s3, 31
    or s3, t5, t6
    add s3, s3, t2
    divu t2, t0, t1
    slli t5, s3, 1
    srli t6, s3, 31
    or s3, t5, t6
    add s3, s3, t2
    rem t2, t0, t1
    slli t5, s3, 1
    srli t6, s3, 31
    or s3, t5, t6
    add s3, s3, t2
    remu t2, t0, t1
    slli t5, s3, 1
    srli t6, s3, 31
    or s3, t5, t6
    add s3, s3, t2
    addi t2, t0, -2048
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    slti t2, t0, -2048
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    sltiu t2, t0, -2048
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    xori t2, t0, -2048
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    ori t2, t0, -2048
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    andi t2, t0, -2048
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    addi t2, t0, -1
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    slti t2, t0, -1
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    sltiu t2, t0, -1
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    xori t2, t0, -1
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    ori t2, t0, -1
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    andi t2, t0, -1
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    addi t2, t0, 0
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    slti t2, t0, 0
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    sltiu t2, t0, 0
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    xori t2, t0, 0
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    ori t2, t0, 0
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    andi t2, t0, 0
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    addi t2, t0, 1
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    slti t2, t0, 1
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    sltiu t2, t0, 1
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    xori t2, t0, 1
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    ori t2, t0, 1
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    andi t2, t0, 1
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    addi t2, t0, 2047
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    slti t2, t0, 2047
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    sltiu t2, t0, 2047
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    xori t2, t0, 2047
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    ori t2, t0, 2047
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    andi t2, t0, 2047
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    slli t2, t0, 0
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    srli t2, t0, 0
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    srai t2, t0, 0
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    slli t2, t0, 1
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    srli t2, t0, 1
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    srai t2, t0, 1
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    slli t2, t0, 31
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    srli t2, t0, 31
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    srai t2, t0, 31
    slli t5, s2, 1
    srli t6, s2, 31
    or s2, t5, t6
    add s2, s2, t2
    la t3, scratch
    sw t0, 0(t3)
    sw t1, 4(t3)
    sh t1, 8(t3)
    sb t0, 10(t3)
    lb t2, 0(t3)
    slli t5, s8, 1
    srli t6, s8, 31
    or s8, t5, t6
    add s8, s8, t2
    lb t2, 4(t3)
    slli t5, s8, 1
    srli t6, s8, 31
    or s8, t5, t6
    add s8, s8, t2
    lbu t2, 0(t3)
    slli t5, s8, 1
    srli t6, s8, 31
    or s8, t5, t6
    add s8, s8, t2
    lbu t2, 4(t3)
    slli t5, s8, 1
    srli t6, s8, 31
    or s8, t5, t6
    add s8, s8, t2
    lh t2, 0(t3)
    slli t5, s8, 1
    srli t6, s8, 31
    or s8, t5, t6
    add s8, s8, t2
    lh t2, 4(t3)
    slli t5, s8, 1
    srli t6, s8, 31
    or s8, t5, t6
    add s8, s8, t2
    lhu t2, 0(t3)
    slli t5, s8, 1
    srli t6, s8, 31
    or s8, t5, t6
    add s8, s8, t2
    lhu t2, 4(t3)
    slli t5, s8, 1
    srli t6, s8, 31
    or s8, t5, t6
    add s8, s8, t2
    lw t2, 0(t3)
    slli t5, s8, 1
    srli t6, s8, 31
    or s8, t5, t6
    add s8, s8, t2
    lw t2, 4(t3)
    slli t5, s8, 1
    srli t6, s8, 31
    or s8, t5, t6
    add s8, s8, t2
    lb t2, 8(t3)
    slli t5, s8, 1
    srli t6, s8, 31
    or s8, t5, t6
    add s8, s8, t2
    lbu t2, 8(t3)
    slli t5, s8, 1
    srli t6, s8, 31
    or s8, t5, t6
    add s8, s8, t2
    lh t2, 8(t3)
    slli t5, s8, 1
    srli t6, s8, 31
    or s8, t5, t6
    add s8, s8, t2
    lhu t2, 8(t3)
    slli t5, s8, 1
    srli t6, s8, 31
    or s8, t5, t6
    add s8, s8, t2
    lw t2, 8(t3)
    slli t5, s8, 1
    srli t6, s8, 31
    or s8, t5, t6
    add s8, s8, t2
    beq t0, t1, brA0
    addi s9, s9, 1
brA0:
    bne t0, t1, brB0
    addi s9, s9, 3
brB0:
    slli t5, s9, 1
    srli t6, s9, 31
    or s9, t5, t6
    add s9, s9, t0
    blt t0, t1, brA1
    addi s9, s9, 1
brA1:
    bge t0, t1, brB1
    addi s9, s9, 3
brB1:
    slli t5, s9, 1
    srli t6, s9, 31
    or s9, t5, t6
    add s9, s9, t0
    bltu t0, t1, brA2
    addi s9, s9, 1
brA2:
    bgeu t0, t1, brB2
    addi s9, s9, 3
brB2:
    slli t5, s9, 1
    srli t6, s9, 31
    or s9, t5, t6
    add s9, s9, t0
    addi s6, s6, 1
    blt s6, s7, loop_j
    addi s5, s5, 1
    blt s5, s7, loop_i
    addi s0, s0, -1
    bnez s0, outer

    /* Compare against the interpreter's results. */
    li t0, 0x2aff1ce5
    bne s1, t0, fail
    li t0, 0xe45a9e79
    bne s2, t0, fail
    li t0, 0xdd880af2
    bne s3, t0, fail
    li t0, 0x853ce706
    bne s8, t0, fail
    li t0, 0x0eb28e20
    bne s9, t0, fail
    li a0, 1
    la a1, passed
    li a2, 31
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
    .ascii "JIT matches the interpreter OK\n"
    .section .data
    .balign 4
scratch:
    .space 32
    .section .rodata
    .balign 4
values:
    .word 0
    .word 1
    .word -1
    .word 2
    .word -2
    .word 2147483647
    .word -2147483648
    .word 1515870810
    .word -1515870811
    .word 65536
    .word -65536
    .word 31
    .word 32
    .word 33
    .word -559038737
    .word 7
