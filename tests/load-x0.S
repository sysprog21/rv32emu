/* A load into x0 performs its access but leaves x0 zero. Load nonzero words
 * into x0 with every width, including a run of LHU that the interpreter may
 * fuse, then read x0 through a store and a comparison. Repeat the loop so that
 * T1 also compiles it.
 */
    .option norvc
    .option norelax
    .text
    .global _start
_start:
    li   s0, 1000
    la   s1, words
    la   s2, scratch
loop:
    lw   zero, 0(s1)
    lh   zero, 4(s1)
    lhu  zero, 4(s1)
    lhu  zero, 6(s1)        /* consecutive lhu records may be fused */
    lb   zero, 6(s1)
    lbu  zero, 6(s1)
    j    1f
1:
    li   t0, -1
    sw   t0, 0(s2)
    sw   zero, 0(s2)
    lw   t0, 0(s2)
    bnez t0, fail
    lw   t1, 0(s1)
    sltu t2, zero, t1
    li   t3, 1
    bne  t2, t3, fail
    addi s0, s0, -1
    bnez s0, loop

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

    .data
msg:
    .ascii "load into x0 OK\n"
msg_end:
    .balign 4
words:
    .word 0x80000001
    .word 0xfff2fff1
scratch:
    .word 0
