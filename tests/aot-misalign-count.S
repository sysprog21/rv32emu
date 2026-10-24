/*
 * Instruction counts across a misaligned access in user-mode AOT code.
 *
 * A native block charges all of its instructions on entry. When a misaligned
 * access in the middle of it traps, the default handler emulates the access
 * and execution resumes after it, outside the block. The instructions after
 * the access therefore run, and are charged, again; the block must refund
 * them. The program prints the instret delta of a hot loop whose block starts
 * with such an access, so every tier must print the same number. It writes a
 * machine CSR, so the code generators keep their alignment checks, and
 * installs no trap vector, so the default handler emulates the access.
 */
    /* Nothing sets gp, so la must not relax to a gp-relative addi. */
    .option norelax
    .text
    .global _start

    .equ ITER, 20000

_start:
    csrw mscratch, zero
    la   s0, buf + 1            /* odd: the word load is misaligned */
    li   s2, ITER
    rdinstret s3
1:
    lw   a0, 0(s0)
    addi a1, a1, 1
    addi a2, a2, 2
    xor  a3, a1, a2
    add  a4, a4, a3
    addi s2, s2, -1
    bnez s2, 1b
    rdinstret s4
    sub  s4, s4, s3

    /* Print the delta as eight hexadecimal digits and a newline. */
    la   t0, out
    li   t1, 28
2:
    srl  t2, s4, t1
    andi t2, t2, 15
    li   t3, 10
    blt  t2, t3, 3f
    addi t2, t2, 'a' - '0' - 10
3:
    addi t2, t2, '0'
    sb   t2, 0(t0)
    addi t0, t0, 1
    addi t1, t1, -4
    bgez t1, 2b
    li   t2, '\n'
    sb   t2, 0(t0)

    li   a0, 1
    la   a1, out
    li   a2, 9
    li   a7, 64
    ecall
    li   a0, 0
    li   a7, 93
    ecall

    .data
    .balign 4
buf:
    .byte 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
out:
    .space 9
