/* More temporaries than host registers, but only five loop inputs.
 * A misaligned load before any temporary definitions checks that a trap sees
 * their previous-iteration values while carried values stay current.
 */
#ifndef ITERATIONS
#define ITERATIONS 20000
#endif
#ifdef USE_MULDIV
#define LOAD_RESULT 1
#else
#define LOAD_RESULT 4369
#endif
    .option norvc
    .text
    .global _start
    .macro check_temporaries
    addi t0, s1, 0
    bne a0, t0, fail
    addi t0, s1, 1
    bne a1, t0, fail
    addi t0, s1, 2
    bne a2, t0, fail
    addi t0, s1, 3
    bne a3, t0, fail
    addi t0, s1, 4
    bne a4, t0, fail
    addi t0, s1, 5
    bne a5, t0, fail
    addi t0, s1, 6
    bne a6, t0, fail
    addi t0, s1, 7
    bne a7, t0, fail
    addi t0, s1, 8
    bne t1, t0, fail
    addi t0, s1, 9
    bne t2, t0, fail
    addi t0, s1, 10
    bne t3, t0, fail
    addi t0, s1, 11
    bne t4, t0, fail
    addi t0, s1, 12
    bne t5, t0, fail
    addi t0, s1, 13
    bne t6, t0, fail
    addi t0, s1, 14
    bne ra, t0, fail
    .endm
_start:
#ifdef USE_CSR
    la t0, handler
    csrw mtvec, t0
#endif
    li s0, ITERATIONS
    la t0, seed
    lw s1, 0(t0)
    li s2, 0
    la s3, data
    la s5, scratch
    li s4, 0
#ifdef REGION_BRANCH
    li s6, 0
#endif
loop:
#ifdef REGION_BRANCH
    /* Split the loop so region allocation handles the same register pressure
     * across two outcomes, including an early trap on the following load.
     */
    andi s7, s0, 1
    beqz s7, 1f
    addi s6, s6, 1
1:
#endif
    lhu t0, 0(s3)
    addi a0, s1, 1
    addi a1, a0, 1
    addi a2, a1, 1
    addi a3, a2, 1
    addi a4, a3, 1
    addi a5, a4, 1
    addi a6, a5, 1
    addi a7, a6, 1
    addi t1, a7, 1
    addi t2, t1, 1
    addi t3, t2, 1
    addi t4, t3, 1
    addi t5, t4, 1
    addi t6, t5, 1
    addi ra, t6, 1
    sw a7, 0(s5)
    lw a7, 0(s5)
#ifdef USE_MULDIV
    /* The x86 divide borrows RAX/RDX, which may hold carried values. */
    div t0, t0, t0
#endif
    add s2, s2, t0
    add s2, s2, a0
    add s2, s2, a1
    add s2, s2, a2
    add s2, s2, a3
    add s2, s2, a4
    add s2, s2, a5
    add s2, s2, a6
    add s2, s2, a7
    add s2, s2, t1
    add s2, s2, t2
    add s2, s2, t3
    add s2, s2, t4
    add s2, s2, t5
    add s2, s2, t6
    add s2, s2, ra
    addi s1, s1, 1
    xori s3, s3, 1
    addi s0, s0, -1
    bnez s0, loop
    li t0, ((ITERATIONS * (225 + LOAD_RESULT) + \
             15 * ITERATIONS * (ITERATIONS - 1) / 2) & 0xffffffff)
    bne s2, t0, fail
    check_temporaries
#ifdef REGION_BRANCH
    li t0, ITERATIONS / 2
    bne s6, t0, fail
#endif
    lw t0, 0(s5)
    bne t0, a7, fail
#ifdef USE_CSR
    li t0, ITERATIONS / 2
    bne s4, t0, fail
    csrw mtvec, zero
#endif
    li a0, 1
    la a1, passed
    li a2, 18
    li a7, 64
    ecall
    li a0, 0
    j exit
fail:
    li a0, 1
exit:
#ifdef USE_CSR
    csrw mtvec, zero
#endif
    li a7, 93
    ecall
#ifdef USE_CSR
    .balign 4
handler:
    addi s4, s4, 1
    /* t0 is the faulting load's destination; preserve every other value. */
    csrr t0, mcause
    addi t0, t0, -4
    bnez t0, fail
    andi t0, s0, 1
    beqz t0, fail
    check_temporaries
    csrr t0, mepc
    addi t0, t0, 4
    csrw mepc, t0
    li t0, 0x1111
    mret
#endif
    .section .rodata
passed:
    .ascii "JIT spill loop OK\n"
    .balign 4
seed:
    .word 7
    .balign 2
data:
    .byte 0x11, 0x11, 0x11
    .section .bss
    .balign 4
scratch:
    .space 4
