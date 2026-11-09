/* Multi-block loops that T1C may keep in host registers as region loops: an
 * if-then body around a conditional store, a loop that leaves from its middle,
 * and a body that touches more guest registers than a host has, so some values
 * are committed at every backedge. Each loop runs past 1024 iterations, so the
 * budget exit is taken as well as the loop exit, and repeats until T1C has
 * compiled it. Every result is folded into s11, whose expected value comes
 * from an interpreter build.
 */
#define EXPECTED 0xee2bc853

    .text
    .global _start

    /* s11 = rotl(s11, 1) + reg */
    .macro fold reg
    slli t5, s11, 1
    srli t6, s11, 31
    or s11, t5, t6
    add s11, s11, \reg
    .endm

_start:
    li s11, 0
    li s10, 100
repeat:
    /* If-then body: set bit i * 7 mod 64 of a two-word map unless already
     * set, counting new bits in a2 and repeats in a3, as a sieve does.
     */
    la a0, map
    sw zero, 0(a0)
    sw zero, 4(a0)
    li a1, 0
    li a2, 0
    li a3, 0
    li a4, 3000
sieve:
    slli t0, a1, 3
    sub t0, t0, a1
    andi t0, t0, 63
    srli t1, t0, 5
    slli t1, t1, 2
    add t1, a0, t1
    lw t2, 0(t1)
    li t3, 1
    sll t3, t3, t0
    and t4, t2, t3
    bnez t4, sieve_seen
    or t2, t2, t3
    sw t2, 0(t1)
    addi a2, a2, 1
    j sieve_next
sieve_seen:
    addi a3, a3, 1
sieve_next:
    addi a1, a1, 1
    bltu a1, a4, sieve
    fold a2
    fold a3
    lw t0, 0(a0)
    fold t0
    lw t0, 4(a0)
    fold t0

    /* Leave from the middle: accumulate until the running sum passes a limit
     * reached only after the budget has expired a few times.
     */
    li a1, 0
    li a2, 0
    li a4, 5000000
middle:
    addi a1, a1, 1
    add a2, a2, a1
    bltu a4, a2, middle_done
    xori a3, a2, 0x55
    andi a3, a3, 0xff
    add a2, a2, a3
    j middle
middle_done:
    fold a1
    fold a2

    /* Leave from the middle with values defined only after the exit test:
     * when the loop ends, they must hold the previous iteration's results.
     * Ten registers spill on x86-64, fifteen on AArch64.
     */
    li a1, 0
    li a2, 0
    li a4, 3000000
    /* Enter at the exit test, so that a pass leaves before redefining. */
    j late10
late10:
    addi a1, a1, 1
    add a2, a2, a1
    bltu a4, a2, late10_done
    xori a3, a2, 0x33
    addi t0, a1, 7
    xor t1, a2, t0
    add t2, t1, a1
    sub t3, t2, a3
    slli t4, t3, 3
    xor s2, t4, t1
    j late10
late10_done:
    fold a1
    fold a2
    fold a3
    fold t0
    fold t1
    fold t2
    fold t3
    fold t4
    fold s2

    li a1, 0
    li a2, 0
    li a4, 4000000
    /* Enter at the exit test, so that a pass leaves before redefining. */
    j late15
late15:
    addi a1, a1, 1
    add a2, a2, a1
    bltu a4, a2, late15_done
    xori a3, a2, 0x5a
    addi t0, a1, 3
    xor t1, a2, t0
    add t2, t1, a1
    sub t3, t2, a3
    slli t4, t3, 2
    xor s2, t4, t1
    add s3, s2, t0
    xor s4, s3, a3
    sub s5, s4, t2
    add s6, s5, s3
    srli s7, s6, 1
    xor a5, s7, s4
    j late15
late15_done:
    fold a1
    fold a2
    fold a3
    fold t0
    fold t1
    fold t2
    fold t3
    fold t4
    fold s2
    fold s3
    fold s4
    fold s5
    fold s6
    fold s7
    fold a5

    /* Many registers: rotate twelve accumulators through a branchy body. */
    li a1, 1
    li a2, 2
    li a3, 3
    li a4, 4
    li a5, 5
    li a6, 6
    li a7, 7
    li s2, 8
    li s3, 9
    li s4, 10
    li s5, 11
    li s6, 12
    li s7, 2500
many:
    add a1, a1, a2
    xor a2, a2, a3
    add a3, a3, a4
    sub a4, a4, a5
    add a5, a5, a6
    xor a6, a6, a7
    andi t0, s7, 1
    beqz t0, many_even
    add a7, a7, s2
    xor s2, s2, s3
    j many_join
many_even:
    sub s3, s3, s4
    add s4, s4, s5
many_join:
    xor s5, s5, s6
    add s6, s6, a1
    addi s7, s7, -1
    bnez s7, many
    fold a1
    fold a2
    fold a3
    fold a4
    fold a5
    fold a6
    fold a7
    fold s2
    fold s3
    fold s4
    fold s5
    fold s6

    addi s10, s10, -1
    bnez s10, repeat

#ifndef PRINT_RESULT
    li t0, EXPECTED
    bne s11, t0, fail
    li a0, 1
    la a1, passed
    li a2, 15
    li a7, 64
    ecall
    li a0, 0
    j exit
fail:
    li a0, 1
#else
    /* Print s11 in hex, to derive EXPECTED from an interpreter build. */
    la a1, hexbuf
    li t1, 8
    mv t2, s11
hex:
    srli t3, t2, 28
    li t4, 10
    blt t3, t4, hex_digit
    addi t3, t3, 'a' - '0' - 10
hex_digit:
    addi t3, t3, '0'
    sb t3, 0(a1)
    addi a1, a1, 1
    slli t2, t2, 4
    addi t1, t1, -1
    bnez t1, hex
    li a0, 1
    la a1, hexbuf
    li a2, 9
    li a7, 64
    ecall
    li a0, 0
#endif
exit:
    li a7, 93
    ecall

    .section .rodata
passed:
    .ascii "region loop OK\n"
    .data
    .balign 4
map:
    .word 0, 0
hexbuf:
    .ascii "00000000\n"
