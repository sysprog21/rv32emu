/* The blocked loop stays inside the interpreter chain. Its entry count must
 * still equal ITERATIONS, including the first pass in the startup block.
 */
#define ITERATIONS 100000
#ifndef STATS_FUSED_HEAD
#define STATS_FUSED_HEAD 0
#endif
.option norvc
.option norelax
.text
.globl _start
_start:
    li s0, ITERATIONS
    la s1, counter
    li s2, 1
loop:
#if STATS_FUSED_HEAD == 1
    lw t2, 4(s1)
    lw t3, 8(s1)
    lw t4, 12(s1)
#elif STATS_FUSED_HEAD == 2
    sw zero, 4(s1)
    sw zero, 8(s1)
    sw zero, 12(s1)
#endif
    addi s0, s0, -1
    amoadd.w zero, s2, (s1)
    bnez s0, loop
    lw t0, 0(s1)
    li t1, ITERATIONS
    bne t0, t1, fail
    li a0, 0
    j stop
fail:
    li a0, 1
stop:
    li a7, 93
    ecall
.data
.balign 4
counter:
    .word 0, 0, 0, 0
