/*
 * Many rounds of self-modifying code made visible by FENCE.I.
 *
 * Each round calls f more often than the 4096 dispatches that make a block hot
 * (FENCE.I resets the count), which lets a tier compile it, then rewrites f's
 * immediate (alternating +1 and -1) and issues FENCE.I. Every later call must
 * return the new constant. Run for 100 rounds it forces a hundred retirements
 * and recompilations of the same code, in packed and non-packed builds alike.
 */
    .text
    .global _start

    .equ CALLS, 5000
    .equ ROUNDS, 100

_start:
    li   s2, 1              /* the constant f returns */
    li   s3, ROUNDS
    li   s4, 1 << 20        /* the immediate step added to f's encoding */
round:
    li   s1, CALLS
1:
    jal  ra, f
    bne  a0, s2, fail
    addi s1, s1, -1
    bnez s1, 1b

    addi s3, s3, -1
    beqz s3, pass

    la   t0, f
    lw   t1, 0(t0)
    add  t1, t1, s4
    sw   t1, 0(t0)
    fence.i
    srai t2, s4, 20         /* +1 or -1: the constant f now returns */
    add  s2, s2, t2
    neg  s4, s4             /* the next round undoes this one */
    j    round

pass:
    li   a7, 64
    li   a0, 1
    la   a1, msg
    la   a2, msg_end
    sub  a2, a2, a1
    ecall
    li   a7, 93
    li   a0, 0
    ecall
fail:
    li   a7, 93
    li   a0, 1
    ecall

    .balign 4
    .option push
    .option norvc
f:
    li   a0, 1
    ret
    .option pop

    .section .rodata
msg:
    .ascii "FENCE.I rounds OK\n"
msg_end:
