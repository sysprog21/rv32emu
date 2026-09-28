/*
 * Self-modifying code made visible by FENCE.I.
 *
 * f returns a constant. It is called often enough to be compiled, then its
 * first instruction is rewritten to return another constant and FENCE.I is
 * issued: every later call must see the new instruction. An emulator that
 * kept the decoded block, or rebuilt it but reused the code translated from
 * the old instruction, returns the old constant.
 *
 * Only system JIT builds honor FENCE.I, so this runs as a directly loaded
 * system program, without paging. The system interpreter keeps its decoded
 * blocks across FENCE.I.
 */
    .text
    .global _start

    .equ CALLS, 20000

_start:
    li   s2, 1              /* the constant f returns */
    li   s3, 2              /* rounds: before and after the rewrite */
round:
    li   s1, CALLS
1:
    jal  ra, f
    bne  a0, s2, fail
    addi s1, s1, -1
    bnez s1, 1b

    addi s3, s3, -1
    beqz s3, pass

    /* Rewrite "li a0, 1" as "li a0, 2", then make it visible */
    la   t0, f
    lw   t1, 0(t0)
    li   t2, 1 << 20        /* the I-type immediate starts at bit 20 */
    add  t1, t1, t2
    sw   t1, 0(t0)
    fence.i
    li   s2, 2
    j    round

pass:
    li   a7, 64             /* write(1, msg, len) */
    li   a0, 1
    la   a1, msg
    la   a2, msg_end
    sub  a2, a2, a1
    ecall
    li   a7, 93             /* exit(0) */
    li   a0, 0
    ecall
fail:
    li   a7, 93             /* exit(1) */
    li   a0, 1
    ecall

    /* Kept 4-byte encoded and aligned, so one word store rewrites it */
    .balign 4
    .option push
    .option norvc
f:
    li   a0, 1
    ret
    .option pop

    .section .rodata
msg:
    .ascii "FENCE.I made the rewrite visible OK\n"
msg_end:
