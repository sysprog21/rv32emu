/* CSR instructions must return the old value and update the CSR as they do
 * interpreted, also once the loop running them is compiled, and a counter
 * read must count exactly the instructions retired since the previous one.
 */
#ifndef CSR_ITERATIONS
#define CSR_ITERATIONS 4096
#endif
.option norvc
.option norelax
.section .text
.globl _start
_start:
    li s2, 0                  /* iteration */
    li s3, 0                  /* expected sscratch */
    li s4, 0x0f0f0f0f
    li s5, CSR_ITERATIONS
    csrw sscratch, zero
1:
    csrrw t1, sscratch, s2    /* sscratch = i */
    bne t1, s3, fail
    mv s3, s2
    csrrs t1, sscratch, s4
    bne t1, s3, fail
    or s3, s3, s4
    csrrc t1, sscratch, s2
    bne t1, s3, fail
    not t2, s2
    and s3, s3, t2
    csrrsi t1, sscratch, 0x15
    bne t1, s3, fail
    ori s3, s3, 0x15
    csrrci t1, sscratch, 0x3
    bne t1, s3, fail
    andi s3, s3, -4
    csrrs t1, sscratch, zero  /* a pure read */
    bne t1, s3, fail
    csrrwi t1, sscratch, 9
    bne t1, s3, fail
    li s3, 9
    csrr t1, sscratch
    bne t1, s3, fail
    /* Two reads three instructions apart count exactly three. */
    rdinstret t3
    addi t0, t0, 1
    addi t0, t0, 1
    rdinstret t4
    sub t4, t4, t3
    li t0, 3
    bne t4, t0, fail
    addi s2, s2, 1
    bltu s2, s5, 1b
    la a0, success
    j print

#include "system-exit.inc"
success: .asciz "SYSTEM CSR OK\n"
failure: .asciz "SYSTEM CSR FAIL\n"
