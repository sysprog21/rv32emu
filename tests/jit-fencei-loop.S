/*
 * A self-looping block rewritten and retired by FENCE.I, round after round.
 *
 * Each round enters a four-instruction self-loop 20 times (300 iterations each)
 * and checks the sum it produced, then changes the constant loaded inside the
 * loop (alternating +1 and -1) and executes FENCE.I. A tier that compiles hot
 * loops from their entry count promotes the loop part-way through every round,
 * and must never run the previous round's code.
 */
    .text
    .global _start

    .equ ITERS, 300
    .equ ROUNDS, 200

_start:
    li   s3, ROUNDS
    li   s4, 1 << 20        /* step added to the loop constant's encoding */
    li   s2, 1              /* the constant the loop currently loads */
round:
    li   s5, 20
mid:
    li   a2, ITERS
    li   a1, 0
loop:
    .option push
    .option norvc
patched:
    li   t5, 1              /* rewritten every round */
    .option pop
    add  a1, a1, t5
    addi a2, a2, -1
    bnez a2, loop

    li   t0, ITERS
    mul  t0, t0, s2
    bne  a1, t0, fail
    addi s5, s5, -1
    bnez s5, mid

    addi s3, s3, -1
    beqz s3, pass

    la   t0, patched
    lw   t1, 0(t0)
    add  t1, t1, s4
    sw   t1, 0(t0)
    fence.i
    srai t2, s4, 20
    add  s2, s2, t2
    neg  s4, s4
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

    .section .rodata
msg:
    .ascii "FENCE.I loop OK\n"
msg_end:
