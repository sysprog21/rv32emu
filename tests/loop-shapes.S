/* Counter-free self-loops in the shapes T1 keeps in host registers: every
 * conditional branch as the loop tail, x0 on either side of a comparison,
 * register and immediate ALU bodies, and the fused load, store, shift and
 * ADDI records. Each loop runs past 1024 iterations, so the native polling
 * exit is taken as well as the loop exit. A pointer reversal with a nonzero
 * link offset covers the interpreter's fused handler, and a reversal from an
 * unaligned base checks that its load trap resumes after the right width.
 *
 * Assembled for rv32i and rv32ic: the compressed build turns most of these
 * into C.* bodies and C.BEQZ/C.BNEZ tails, with identical results. With
 * USE_CSR, privileged state keeps alignment checks in generated code and a
 * counter read makes every native iteration charge its cycles. Every result
 * is folded into s11, whose expected value comes from an interpreter that
 * implements none of these optimizations.
 */
#ifdef USE_CSR
#define EXPECTED 0x46a04a21
#else
#define EXPECTED 0x0a7c0feb
#endif

    .text
    .global _start

    /* s11 = rotl(s11, 1) + reg */
    .macro fold reg
    slli t5, s11, 1
    srli t6, s11, 31
    or s11, t5, t6
    add s11, s11, \reg
    .endm

    /* Count n iterations down, folding the two running values. */
    .macro countdown n
    li a5, \n
    li a0, 0
    li a1, 0
iterations_\n:
    addi a0, a0, 3
    xor a1, a1, a0
    addi a5, a5, -1
    bnez a5, iterations_\n
    fold a0
    fold a1
    .endm

_start:
    li s11, 0
    /* T1 compiles a loop only once the dispatcher has seen it often enough,
     * so repeat every loop until T1 has compiled it.
     */
#ifdef USE_CSR
    /* Privileged state keeps alignment checks in generated code. */
    csrr t0, mscratch
    csrw mscratch, t0
#endif
    li s10, 100
repeat:
    /* Register ALU body; ADDI + BNE countdown tail (fused in rv32i). */
    li a5, 3000
    li a0, 0x12345
    li a1, 7
    li a2, 0
    li a3, 0
    li a4, 0
countdown:
    add a0, a0, a5
    xor a1, a1, a0
    sll a2, a1, a5
    srl a3, a0, a1
    sra a4, a2, a1
    slt t0, a3, a4
    sltu t1, a4, a3
    or a2, a2, t0
    and a3, a3, t1
    sub a0, a0, a2
    add a1, a1, a3
    addi a5, a5, -1
    bnez a5, countdown
    fold a0
    fold a1
    fold a2
    fold a3
    fold a4

    /* Immediate ALU body with consecutive ADDIs and shifts; BLT tail. */
    li t0, 0
    li t1, 2500
    li a0, 1
    li a1, 0
pc_probe:
    auipc s0, 0
immediate:
    addi a0, a0, 3
    addi a1, a1, 5
    slli a2, a0, 3
    srli a3, a1, 2
    srai a4, a2, 1
    lui a5, 0x12345
    xori a5, a5, 0x5a
    ori a4, a4, 0x101
    andi a3, a3, 0x7ff
    slti s1, a3, 100
    sltiu a2, a4, 1000
    add a0, a0, s1
    add a1, a1, a2
    xor a0, a0, a5
    addi t0, t0, 1
    blt t0, t1, immediate
    la t2, pc_probe
    sub s0, s0, t2
    fold s0
    fold a0
    fold a1
    fold a3
    fold a4

    /* Paired stores and loads; BLTU tail. */
    la a0, buf
    li t2, 1200 * 16
    add a1, a0, t2
    li a2, 0x1111
    li a3, 0x2222
memory:
    sw a2, 0(a0)
    sw a3, 4(a0)
    lw a4, 0(a0)
    lw a5, 4(a0)
    add a2, a2, a5
    xor a3, a3, a4
    sw a2, 8(a0)
    sw a3, 12(a0)
    addi a0, a0, 16
    bltu a0, a1, memory
    fold a2
    fold a3

    /* Post-increment load; BGEU tail with the bound on the left. */
    la s0, buf
    li s1, 0
    addi a1, a1, -4
post_increment:
    lw a4, 0(s0)
    addi s0, s0, 4
    add s1, s1, a4
    bgeu a1, s0, post_increment
    fold s1

    /* Post-increment store; BNE tail on two registers. */
    la s0, buf
    li t2, 1500 * 4
    add a1, s0, t2
    li a2, 0x3c3c
store_increment:
    sw a2, 0(s0)
    addi s0, s0, 4
    slli a3, a2, 1
    xor a2, a2, a3
    bne s0, a1, store_increment
    fold a2
    lw a3, -4(s0)
    fold a3

    /* BGE tail on two registers. */
    li a0, -2000
    li a1, 0
    li a2, 0
greater_equal:
    addi a0, a0, 1
    sub a2, a2, a0
    bge a1, a0, greater_equal
    fold a0
    fold a2

    /* x0 on the left: BGT, BLE, BLTU against zero. */
    li a0, 0
    li a5, 2001
positive:
    add a0, a0, a5
    addi a5, a5, -2
    blt zero, a5, positive
    fold a0
    fold a5
    li a1, 0
    li a5, -3001
non_positive:
    xor a1, a1, a5
    addi a5, a5, 3
    bge zero, a5, non_positive
    fold a1
    fold a5
    li a2, 0
    li a5, 1500
nonzero:
    add a2, a2, a5
    addi a5, a5, -1
    bltu zero, a5, nonzero
    fold a2

    /* BEQ against zero: C.BEQZ when compressed. */
    li a5, 0
    li a3, 0
equal_zero:
    addi a5, a5, 1
    add a3, a3, a5
    srli a4, a5, 11
    beq a4, zero, equal_zero
    fold a3
    fold a5

    /* Build a 3000-node list of {value, next}, linked through offset 4. */
    la a0, nodes
    li a1, 0
    li a2, 3000
build:
    sw a1, 0(a0)
    addi a3, a0, 8
    sw a3, 4(a0)
    addi a1, a1, 1
    mv a0, a3
    bne a1, a2, build
    sw zero, -4(a0)

    /* Reverse it twice; the link is 4 bytes into each node. */
    li s1, 2
    la a4, nodes
reverse_again:
    li a3, 0
reverse:
    mv s0, a4
    lw a4, 4(s0)
    sw a3, 4(s0)
    mv a3, s0
    bnez a4, reverse
    /* Fold the values in list order from the new head. */
    mv a4, a3
    li a5, 0
walk:
    lw t0, 0(a4)
    slli t1, a5, 1
    srli t2, a5, 31
    or a5, t1, t2
    add a5, a5, t0
    lw a4, 4(a4)
    bnez a4, walk
    fold a5
    mv a4, a3
    addi s1, s1, -1
    bnez s1, reverse_again
    la t0, nodes
    bne a3, t0, fail
    /* Exactly at and around the native loop's 1024-iteration poll. */
    countdown 1023
    countdown 1024
    countdown 1025

    /* 10 distinct registers, around the host register limit. */
    li t0, 1100
    li a0, 5
    li a1, 42
    li a2, 79
    li a3, 116
    li a4, 153
    li a5, 190
    li a6, 227
    li a7, 264
    li t1, 301
registers_10:
    add a0, a0, t0
    xor a1, a1, a0
    add a2, a2, a1
    xor a3, a3, a2
    add a4, a4, a3
    xor a5, a5, a4
    add a6, a6, a5
    xor a7, a7, a6
    add t1, t1, a7
    addi t0, t0, -1
    bnez t0, registers_10
    fold t1

    /* 11 distinct registers, around the host register limit. */
    li t0, 1100
    li a0, 5
    li a1, 42
    li a2, 79
    li a3, 116
    li a4, 153
    li a5, 190
    li a6, 227
    li a7, 264
    li t1, 301
    li t2, 338
registers_11:
    add a0, a0, t0
    xor a1, a1, a0
    add a2, a2, a1
    xor a3, a3, a2
    add a4, a4, a3
    xor a5, a5, a4
    add a6, a6, a5
    xor a7, a7, a6
    add t1, t1, a7
    xor t2, t2, t1
    addi t0, t0, -1
    bnez t0, registers_11
    fold t2

    /* 12 distinct registers, around the host register limit. */
    li t0, 1100
    li a0, 5
    li a1, 42
    li a2, 79
    li a3, 116
    li a4, 153
    li a5, 190
    li a6, 227
    li a7, 264
    li t1, 301
    li t2, 338
    li t3, 375
registers_12:
    add a0, a0, t0
    xor a1, a1, a0
    add a2, a2, a1
    xor a3, a3, a2
    add a4, a4, a3
    xor a5, a5, a4
    add a6, a6, a5
    xor a7, a7, a6
    add t1, t1, a7
    xor t2, t2, t1
    add t3, t3, t2
    addi t0, t0, -1
    bnez t0, registers_12
    fold t3

    /* 13 distinct registers, around the host register limit. */
    li t0, 1100
    li a0, 5
    li a1, 42
    li a2, 79
    li a3, 116
    li a4, 153
    li a5, 190
    li a6, 227
    li a7, 264
    li t1, 301
    li t2, 338
    li t3, 375
    li t4, 412
registers_13:
    add a0, a0, t0
    xor a1, a1, a0
    add a2, a2, a1
    xor a3, a3, a2
    add a4, a4, a3
    xor a5, a5, a4
    add a6, a6, a5
    xor a7, a7, a6
    add t1, t1, a7
    xor t2, t2, t1
    add t3, t3, t2
    xor t4, t4, t3
    addi t0, t0, -1
    bnez t0, registers_13
    fold t4

    /* x0 as a source: a stored zero, a copy, a comparison and a negation. */
    la a0, buf
    li a5, 1500
    li a1, 0
zero_sources:
    sw zero, 0(a0)
    add a2, zero, a5
    slt a3, a2, zero
    sub a4, zero, a2
    add a1, a1, a4
    add a1, a1, a3
    addi a5, a5, -1
    bnez a5, zero_sources
    fold a1

    /* Stack-relative and in-place forms: C.LWSP, C.SWSP, C.ADDI4SPN,
     * C.ADDI16SP, C.SRLI, C.SRAI and C.ANDI when compressed.
     */
    la sp, stack_top
    li a5, 1200
    li a0, 0
    li a1, 1
stack_forms:
    sw a1, 12(sp)
    lw a0, 12(sp)
    addi a2, sp, 16
    sub a2, a2, sp
    addi sp, sp, -16
    addi sp, sp, 16
    mv a3, a0
    srli a3, a3, 3
    mv a4, a1
    srai a4, a4, 2
    andi a3, a3, 7
    add a1, a1, a2
    add a1, a1, a3
    add a1, a1, a4
    addi a5, a5, -1
    bnez a5, stack_forms
    fold a0
    fold a1

    /* Constant and absolute-address forms the matcher fuses: LUI runs,
     * LUI + ADDI, LUI + ADD, and LUI-based loads and stores.
     */
    li a5, 1300
    li a0, 0
constants:
    lui a1, 0x12345
    lui a2, 0x6789a
    add a0, a0, a1
    lui a3, 0x11111
    addi a3, a3, 0x222
    xor a0, a0, a3
    lui a4, 0x22222
    add a4, a2, a4
    add a0, a0, a4
    lui t0, %hi(absolute_word)
    lw t1, %lo(absolute_word)(t0)
    add a0, a0, t1
    lui t2, %hi(absolute_word)
    sw a0, %lo(absolute_word)(t2)
    addi a5, a5, -1
    bnez a5, constants
    fold a0

    /* A 32-bit LW and SW (offset 128) between compressed copies and branch
     * when built for RVC: the fused reversal mixes instruction widths.
     */
    la a0, wide_nodes
    li a1, 0
    li a2, 300
wide_build:
    sw a1, 0(a0)
    addi a3, a0, 136
    sw a3, 128(a0)
    addi a1, a1, 1
    mv a0, a3
    bne a1, a2, wide_build
    sw zero, -8(a0)
    la a4, wide_nodes
    li a3, 0
wide_reverse:
    mv s0, a4
    lw a4, 128(s0)
    sw a3, 128(s0)
    mv a3, s0
    bnez a4, wide_reverse
    lw t0, 0(a3)
    fold t0

    /* A misaligned node part way along a list: the fused reversal traps on
     * it after several iterations and resumes through the default handler,
     * and a walk then reads its misaligned link.
     */
    la a0, odd_nodes
    addi t0, a0, 8
    sw t0, 4(a0)
    addi t1, a0, 16
    sw t1, 12(a0)
    addi t2, a0, 27
    sw t2, 20(a0)
    addi t3, a0, 40
    sw t3, 4(t2)
    sw zero, 44(a0)
    li t4, 1
    sw t4, 0(a0)
    li t4, 2
    sw t4, 8(a0)
    li t4, 3
    sw t4, 16(a0)
    li t4, 4
    sw t4, 0(t2)
    li t4, 5
    sw t4, 40(a0)
    mv a4, a0
    li a3, 0
odd_reverse:
    mv s0, a4
    lw a4, 4(s0)
    sw a3, 4(s0)
    mv a3, s0
    bnez a4, odd_reverse
    lw t0, 0(a3)
    fold t0
    mv a4, a3
odd_walk:
    lw a4, 4(a4)
    bnez a4, odd_walk
    fold a4

#ifdef USE_CSR
    /* Counter reads: each native iteration must charge the block's cost. */
    rdcycle t3
    li a5, 2000
    li a0, 0
timed:
    addi a0, a0, 7
    xor a0, a0, a5
    addi a5, a5, -1
    bnez a5, timed
    rdcycle t4
    sub t4, t4, t3
    fold t4
    fold a0
#endif

    addi s10, s10, -1
    bnez s10, repeat

    /* The same reversal from an unaligned base: the load traps, the default
     * handler performs it, and execution resumes at the store, which in the
     * compressed build is two bytes on.
     */
    la a4, unaligned_node
    addi a4, a4, 1
    li a3, 0x5a5a5a5a
    /* A new block hides the constant base from constant propagation. */
    j 1f
1:
    mv s0, a4
    lw a4, 4(s0)
    sw a3, 4(s0)
    mv a3, s0
    bnez a4, fail
    la t0, unaligned_node
    addi t0, t0, 1
    bne s0, t0, fail
    bne a3, t0, fail
    lw t1, 4(a3)
    li t2, 0x5a5a5a5a
    bne t1, t2, fail

#ifndef GOLDEN
    li t0, EXPECTED
    bne s11, t0, fail
#endif
    li a0, 1
    la a1, passed
    li a2, 15
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
    .ascii "loop shapes OK\n"
    .data
    .balign 4
unaligned_node:
    .word 0, 0, 0, 0
absolute_word:
    .word 0x13579bdf
    .bss
    .balign 4
buf:
    .space 1201 * 16
nodes:
    .space 3000 * 8
wide_nodes:
    .space 300 * 136
    .balign 16
odd_nodes:
    .space 64
    .balign 16
stack:
    .space 256
stack_top:
