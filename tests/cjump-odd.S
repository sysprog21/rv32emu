/*
 * Compressed indirect jumps to odd addresses.
 *
 * C.JR and C.JALR expand to JALR with a zero offset, so the target's low bit is
 * cleared rather than fetched. The loop runs long enough for the JIT tiers to
 * compile it, and each pass also checks the C.JALR link value.
 */
    .option rvc
    /* Nothing sets gp, so la must not relax to a gp-relative addi. */
    .option norelax
    .text
    .global _start

_start:
    li s0, 100000
loop:
    la t0, after_jr
    addi t0, t0, 1
    c.jr t0
    j fail
after_jr:
    la t1, after_jalr
    addi t1, t1, 1
    c.jalr t1
link:
    j fail
after_jalr:
    la t2, link
    bne ra, t2, fail
    addi s0, s0, -1
    bnez s0, loop

    li a0, 1
    la a1, passed
    li a2, 24
    li a7, 64
    ecall
    li a0, 0
    j exit
fail:
    li a0, 1
exit:
    li a7, 93
    ecall

    .section .rodata
passed:
    .ascii "compressed odd jumps OK\n"
