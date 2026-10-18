/* A shared leaf reached from two call sites, with both standard and alternate
 * link registers. Trap snapshots must retain the caller's updated link and
 * the leaf's register writes even when they share one native register map.
 */
#ifdef USE_RVC
    .option rvc
#else
    .option norvc
#endif
    .text
    .global _start
_start:
#ifdef USE_CSR
    la t0, handler
    csrw mtvec, t0
#endif
    li s0, 20000
    li s1, 0
    li s2, 0
    la s3, data
#ifdef CHECK_CYCLES
    rdcycle s4
    rdinstret s5
#endif
loop:
    li a0, 3
#ifdef USE_RVC
    c.jal leaf
#else
    jal ra, leaf
#endif
return_one:
    add s1, s1, a0
    li a0, 7
#ifdef USE_RVC
    c.jal leaf
#else
    jal ra, leaf
#endif
return_two:
    add s1, s1, a0
    jal t0, alternate_leaf
return_three:
    add s1, s1, a0
    addi s0, s0, -1
    bnez s0, loop
#ifdef CHECK_CYCLES
    rdcycle t1
    rdinstret t2
    sub t1, t1, s4
    sub t2, t2, s5
    /* Each iteration executes 22 instructions, including calls and returns.
     * Each interval includes the other opening read and its closing read.
     */
    li t0, 22 * 20000 + 2
    bne t1, t0, fail
    bne t2, t0, fail
#endif
    li t0, 520000
    bne s1, t0, fail
#ifdef USE_CSR
    li t0, 40000
    bne s2, t0, fail
    csrw mtvec, zero
#endif
    li a0, 1
    la a1, passed
    li a2, 24
    li a7, 64
    ecall
    li a0, 0
    j exit
leaf:
    addi a0, a0, 1
#ifdef USE_CSR
faulting:
    lhu a1, 1(s3)
#else
    lhu a1, 0(s3)
#endif
    add a0, a0, a1
#ifdef USE_RVC
    c.jr ra
#else
    ret
#endif
alternate_leaf:
    /* Changing the link invalidates return specialization. Return through
     * another register to the continuation selected by the caller.
     */
    mv t1, t0
    li t0, 0
    li a0, 10
    jalr zero, t1, 0
#ifdef USE_CSR
    .balign 4
handler:
    csrr t1, mcause
    li t2, 4
    bne t1, t2, fail
    csrr t1, mepc
    la t2, faulting
    bne t1, t2, fail
    la t2, return_one
    beq ra, t2, 1f
    la t2, return_two
    bne ra, t2, fail
    li t2, 8
    bne a0, t2, fail
    j 2f
1:
    li t2, 4
    bne a0, t2, fail
2:
    addi s2, s2, 1
    li a1, 2
    addi t1, t1, 4
    csrw mepc, t1
    mret
#endif
fail:
    li a0, 1
exit:
#ifdef USE_CSR
    csrw mtvec, zero
#endif
    li a7, 93
    ecall
    .section .rodata
passed:
    .ascii "JIT direct successor OK\n"
    .balign 4
data:
    .half 2
    .byte 0
