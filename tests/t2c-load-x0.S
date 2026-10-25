/* ECALL returns to the dispatcher every pass, so the probe itself promotes.
 * Keep the successful path translatable and exercise scalar and fused loads.
 */
#ifndef LOAD_X0_ITERATIONS
#define LOAD_X0_ITERATIONS 20000000
#endif
.option norvc
.option norelax
.text
.globl _start, probe
_start:
    li s0, LOAD_X0_ITERATIONS
    la s1, words
    la s2, scratch
    li s3, 0
outer:
    mv a0, s3
    li a7, 214              /* brk(0): force dispatcher entry to probe */
    ecall
probe:
    lw zero, 0(s1)
    bne zero, s3, fail
    lh zero, 4(s1)
    bne zero, s3, fail
    lb zero, 4(s1)
    bne zero, s3, fail
    lhu zero, 4(s1)
    bne zero, s3, fail
    lbu zero, 4(s1)
    bne zero, s3, fail
    lw zero, 0(s1)
    lw t5, 4(s1)
    lw zero, 0(s1)
    lw t6, 4(s1)
    sw zero, 0(s2)
    lw t0, 0(s2)
    bne t0, s3, fail
    bne zero, s3, fail
    bne t5, t6, fail
    lui t4, %hi(words)
    lw zero, %lo(words)(t4)
    sw zero, 0(s2)
    lw t0, 0(s2)
    bne t0, s3, fail
    bne zero, s3, fail
    lw zero, 0(s1)
    addi s1, s1, 4
    sw zero, 0(s2)
    lw t0, 0(s2)
    bne t0, s3, fail
    addi s1, s1, -4
    bne zero, s3, fail
    sw zero, 0(s2)
    lw t0, 0(s2)
    bne t0, s3, fail
    addi s0, s0, -1
    bnez s0, outer
    li a7, 64
    li a0, 1
    la a1, success
    la a2, success_end
    sub a2, a2, a1
    ecall
    mv a0, s3
    j stop
fail:
    li a0, 1
stop:
    li a7, 93
    ecall
.data
success: .ascii "tiered load into x0 OK\n"
success_end:
.balign 4
words: .word 0x80000001, 0xfff2fff1
scratch: .word 1
