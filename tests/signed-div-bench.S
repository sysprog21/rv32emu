/* Interpreter DIV/REM throughput. Build with -DDIVISOR=7, -1, or 0.
 * Load operands so constant folding cannot replace the measured operations.
 */
#ifndef DIVISOR
#define DIVISOR 7
#endif
#ifndef ITERATIONS
#define ITERATIONS 10000000
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
    div t2, t0, t1
    rem t3, t0, t1
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
