/* Keep the computed mask and count observable after repeated bit updates.
 * Runtime sources 1, 3, and 0 exercise the rotating path and its fallback.
 */
#ifndef BIT_OP
#define BIT_OP 0
#endif
    .option norvc
    .text
    .global _start
_start:
    li s7, 0
    la s8, values
    li s1, 20003
    la s2, scratch
round:
    lw s4, 0(s8)
    mv a6, s4
    li a5, 0
    li t0, 0x12345678
    sw t0, 0(s2)
#ifdef USE_CYCLES
    rdcycle s10
    rdinstret s11
#endif
    jal ra, update
#ifdef USE_CYCLES
    rdcycle t4
    rdinstret t5
    sub t4, t4, s10
    sub t5, t5, s11
#if BIT_OP == 2
    li t0, 7 * 20003 + 4
#else
    li t0, 6 * 20003 + 4
#endif
    bne t4, t0, fail
    bne t5, t0, fail
#endif
    /* Build an independent reference using only one-bit immediate shifts. */
    li s5, 0
    li s6, 0x12345678
reference:
    mv t1, s4
    andi t2, s5, 31
    beqz t2, apply
shift:
    slli t1, t1, 1
    addi t2, t2, -1
    bnez t2, shift
apply:
#if BIT_OP == 0
    or s6, s6, t1
#elif BIT_OP == 1
    xor s6, s6, t1
#else
    xori t1, t1, -1
    and s6, s6, t1
#endif
    addi s5, s5, 1
    bne s5, s1, reference
    lw t0, 0(s2)
    bne t0, s6, fail
    bne a2, t1, fail
    bne a5, s1, fail
    bne a6, s4, fail
    addi s8, s8, 4
    addi s7, s7, 1
    li t0, 3
    bne s7, t0, round
    li a0, 1
    la a1, passed
    li a2, 16
    li a7, 64
    ecall
    li a0, 0
    j exit
update:
loop:
    lw a3, 0(s2)
    sll a2, a6, a5
#if BIT_OP == 0
    addi a5, a5, 1
    or a3, a3, a2
#elif BIT_OP == 1
    addi a5, a5, 1
    xor a3, a3, a2
#else
    xori a2, a2, -1
    and a3, a3, a2
#endif
    sw a3, 0(s2)
#if BIT_OP == 2
    addi a5, a5, 1
#endif
    bne a5, s1, loop
    ret
fail:
    li a0, 1
exit:
    li a7, 93
    ecall
    .section .rodata
passed:
    .ascii "JIT bit loop OK\n"
    .balign 4
values:
    .word 1, 3, 0
    .section .bss
    .balign 4
scratch:
    .space 4
