/* Native T1 arithmetic throughput. Build with -DOP=mulh (or another
 * integer instruction) and -DDIVISOR=7, -1, or 0. Memory operands prevent
 * folding.
 * Compare identical guests with T1-only binaries; correctness lives in
 * jit-muldiv.S and jit-interp-diff.S.
 */
#ifndef OP
#define OP div
#endif
#ifndef DIVISOR
#define DIVISOR 7
#endif
#ifndef ITERATIONS
#define ITERATIONS 3000000
#endif
    .option norvc
    .text
    .global _start
_start:
    la s1, operands
    lw t0, 0(s1)
    lw t1, 4(s1)
    li s0, ITERATIONS
loop:
    .rept 8
#ifdef RANDOM_INPUT
    slli t3, t0, 13
    xor t0, t0, t3
    srli t3, t0, 17
    xor t0, t0, t3
    slli t3, t0, 5
    xor t0, t0, t3
#endif
    OP t2, t0, t1
    .endr
    addi s0, s0, -1
    bnez s0, loop
    li a0, 0
    li a7, 93
    ecall
    .section .rodata
    .balign 4
operands:
    .word -123456789, DIVISOR
