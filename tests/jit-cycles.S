/*
 * Exact cycle and instret counts under JIT code generation.
 *
 * Every executed instruction counts one cycle and one retired instruction.
 * Tier-1 code chains blocks without returning to the dispatcher, so each block
 * must add its own cost; counting only the block a chain was entered through
 * undercounts a loop. The emulator may count only at chain entry for a program
 * that reads no counter, so this one reads cycle and instret around a loop that
 * runs long enough to be compiled, and checks both deltas.
 */
    .text
    .global _start

    .equ ITER, 200000

_start:
    li   s2, 8                  /* rounds: the later ones run compiled code */
1:
    li   a0, ITER
    rdcycle   s0
    rdinstret s1
    jal  ra, spin
    rdcycle   t1
    rdinstret t2
    sub  t1, t1, s0
    sub  t2, t2, s1

    /* The loop, the call, the LI and return in spin, and two reads: a counter
     * read sees the count including itself, so each delta covers the read
     * between the pair and the closing read.
     */
    li   t3, 5 * ITER + 5
    bne  t1, t3, fail
    bne  t2, t3, fail
    addi s2, s2, -1
    bnez s2, 1b

    li   a0, 1
    la   a1, passed
    li   a2, 20
    li   a7, 64
    ecall
    li   a0, 0
    j    exit
fail:
    li   a0, 1
exit:
    li   a7, 93
    ecall

    /* The dispatcher enters spin, and compiled code chains from that block into
     * the loop, which is the block a count at chain entry misses. The loop
     * loads and stores like compiled C: a bare decrement and branch stays in
     * the interpreter's fast path and is never compiled, as would a loop in the
     * block that reads the counters, since CSR instructions are never compiled.
     */
spin:
    li   t4, 0
3:
    lw   t5, 0(sp)              /* five instructions per iteration */
    xor  t5, t5, t4
    sw   t5, 0(sp)
    addi t4, t4, 1
    bne  t4, a0, 3b
    ret

    .section .rodata
passed:
    .string "JIT cycle counts OK\n"
