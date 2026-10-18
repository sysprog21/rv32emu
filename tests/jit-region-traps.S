/* Both children guard the same unknown base. Alignment learned while
 * emitting one child must not suppress the other child's misalignment trap.
 */
#ifdef USE_RVC
    .option rvc
#else
    .option norvc
#endif
    .text
    .global _start
_start:
    la t0, handler
    csrw mtvec, t0
    li s0, 20000
    li s1, 0
    li s2, 0
    la s3, data
loop:
    andi a2, s0, 1
    add a0, s3, a2
    bnez a2, odd
    lhu a1, 0(a0)
    j join
odd:
faulting:
    lhu a1, 0(a0)
    j join
join:
    add s1, s1, a1
    addi s0, s0, -1
    bnez s0, loop
    li t0, 20000 * 4369
    bne s1, t0, fail
    li t0, 10000
    bne s2, t0, fail
    csrw mtvec, zero
    li a0, 1
    la a1, passed
    li a2, 20
    li a7, 64
    ecall
    li a0, 0
    j exit
    .balign 4
handler:
    csrr t0, mcause
    li t1, 4
    bne t0, t1, fail
    csrr t0, mepc
    la t1, faulting
    bne t0, t1, fail
    andi t1, a0, 1
    beqz t1, fail
    andi t1, s0, 1
    beqz t1, fail
    /* The failed load has not changed its destination. Every odd iteration
     * follows an even one, so the previous value is already 0x1111.
     */
    li t1, 4369
    bne a1, t1, fail
    addi s2, s2, 1
    addi t0, t0, 4
    csrw mepc, t0
    mret
fail:
    li a0, 1
exit:
    csrw mtvec, zero
    li a7, 93
    ecall
    .section .rodata
passed:
    .ascii "JIT region traps OK\n"
    .balign 4
data:
    .byte 0x11, 0x11, 0x11
