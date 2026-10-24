/* Every AMO must return the old word and store its combination, also once
 * the loop running them is compiled, and one into x0 must still store. A
 * misaligned AMO in that loop traps and stores nothing.
 */
#ifndef AMO_ITERATIONS
#define AMO_ITERATIONS 4096
#endif
.option norvc
.option norelax
.section .text
.globl _start
_start:
    la t0, trap
    csrw stvec, t0
    la s1, word
    sw zero, 0(s1)
    li s2, 0                  /* iteration */
    li s3, 0                  /* expected word */
    li s4, 5
    li s5, 0x00ff00ff
    li s6, -7
    li s7, 0x7fff0000
    li s8, AMO_ITERATIONS
    li s9, 0                  /* misaligned traps taken */
    addi s10, s1, 2
1:
    amoswap.w t1, s2, (s1)    /* word = i */
    bne t1, s3, fail
    mv s3, s2
    amoadd.w t1, s4, (s1)     /* word = i + 5 */
    bne t1, s3, fail
    add s3, s3, s4
    amoxor.w t1, s5, (s1)
    bne t1, s3, fail
    xor s3, s3, s5
    amoor.w t1, s4, (s1)
    bne t1, s3, fail
    or s3, s3, s4
    amoand.w t1, s7, (s1)
    bne t1, s3, fail
    and s3, s3, s7
    amomin.w t1, s6, (s1)     /* signed: -7 wins */
    bne t1, s3, fail
    mv t2, s3
    blt t2, s6, 2f
    mv t2, s6
2:
    mv s3, t2
    amomax.w t1, s2, (s1)
    bne t1, s3, fail
    mv t2, s3
    bge t2, s2, 3f
    mv t2, s2
3:
    mv s3, t2
    amominu.w t1, s7, (s1)
    bne t1, s3, fail
    mv t2, s3
    bltu t2, s7, 4f
    mv t2, s7
4:
    mv s3, t2
    amomaxu.w t1, s4, (s1)
    bne t1, s3, fail
    mv t2, s3
    bgeu t2, s4, 5f
    mv t2, s4
5:
    mv s3, t2
    amoadd.w zero, s4, (s1)   /* x0 stays zero; the word still changes */
    bnez zero, fail
    add s3, s3, s4
    lw t1, 0(s1)
    bne t1, s3, fail
    /* A store/AMO address-misaligned exception, storing nothing. */
misaligned:
    amoadd.w t1, s4, (s10)
    addi s2, s2, 1
    bne s9, s2, fail
    lw t1, 0(s1)
    bne t1, s3, fail
    bltu s2, s8, 1b
    la a0, success
    j print

.balign 4
trap:
    csrr t0, scause
    li t1, 6
    bne t0, t1, fail
    csrr t0, stval
    bne t0, s10, fail
    csrr t0, sepc
    la t1, misaligned
    bne t0, t1, fail
    addi s9, s9, 1
    addi t0, t0, 4
    csrw sepc, t0
    sret

#include "system-exit.inc"
success: .asciz "SYSTEM AMO OK\n"
failure: .asciz "SYSTEM AMO FAIL\n"
.balign 4
word: .word 0
