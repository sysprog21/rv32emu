/* Loads into x0 must keep their accesses while discarding every result.
 * Long runs allow asynchronous tier-2 compilation, checked separately with
 * diagnostics. Sv32 covers scalar/absolute/post-increment loads. Bare mode
 * starts with a multi-load block so it also reaches lazy fusion.
 */
#ifndef LOAD_X0_ITERATIONS
#define LOAD_X0_ITERATIONS 20000000
#endif
.option norvc
.option norelax
.text
.globl _start
_start:
#ifndef LOAD_X0_BARE
    la t0, root
    li t1, 0xcf
    sw t1, 0(t0)
    li t1, 0x3d0000cf
    li t2, 3904
    add t2, t0, t2
    sw t1, 0(t2)
    srli t0, t0, 12
    li t1, 0x80000000
    or t0, t0, t1
    csrw satp, t0
    sfence.vma
#endif
    la s1, words
    la s2, scratch
    li s0, LOAD_X0_ITERATIONS
    li s4, 0
#ifdef LOAD_X0_BARE
outer:
    csrr t0, sscratch
    j loop                 /* force a dispatcher entry to the multi-load */
#endif
loop:
#ifdef LOAD_X0_BARE
    lw zero, 0(s1)
    lw a3, 0(s1)
    lw zero, 0(s1)
    lw a4, 0(s1)
    sw zero, 0(s2)
    lw t0, 0(s2)
    bne t0, s4, fail
    bne a3, a4, fail
    bne zero, s4, fail
#endif
    lw zero, 0(s1)
    bne zero, s4, fail
    lb zero, 4(s1)
    bne zero, s4, fail
    lh zero, 4(s1)
    bne zero, s4, fail
    lbu zero, 4(s1)
    bne zero, s4, fail
    lhu zero, 4(s1)
    bne zero, s4, fail
    lw zero, 0(s1)
    lw a3, 0(s1)
    lw zero, 0(s1)
    lw a4, 0(s1)
    bne zero, s4, fail
    bne a3, a4, fail
    lui t4, %hi(words)
    lw zero, %lo(words)(t4)
    bne zero, s4, fail
    lw zero, 0(s1)
    addi s1, s1, 4
    addi s1, s1, -4
    bne zero, s4, fail
    addi s0, s0, -1
#ifdef LOAD_X0_BARE
    bnez s0, outer
#else
    bnez s0, loop
#endif
    la a0, success
    j print
#include "system-exit.inc"
success: .asciz "SYSTEM load x0 OK\n"
failure: .asciz "SYSTEM load x0 FAIL\n"
.balign 4
words: .word 0x80000001, 0xfff2fff1
scratch: .word 1
.balign 4096
root: .space 4096
