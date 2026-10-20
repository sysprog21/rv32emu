/*
 * A decoded prefix before an illegal word must retire before the trap, even
 * after enough visits to trigger T1 promotion.
 */
    .option norvc
    .text
    .globl _start
_start:
    la   t0, trap_handler
    csrw stvec, t0
    li   s0, 0
    li   s1, 0
    li   s2, 100000
    j    prefix

trap_handler:
    csrr t0, scause
    li   t1, 2
    bne  t0, t1, fail
    addi s1, s1, 1
    bne  s0, s1, fail
    la   t0, recover
    csrw sepc, t0
    sret

recover:
    bne  s0, s1, fail
    blt  s1, s2, prefix
    li   a7, 64
    li   a0, 1
    la   a1, success
    la   a2, success_end
    sub  a2, a2, a1
    ecall
    li   a7, 93
    li   a0, 0
    ecall
fail:
    li   a7, 93
    li   a0, 1
    ecall

    .balign 16
prefix:
    addi s0, s0, 1
    .word 0x00200073

    .section .rodata
success:
    .ascii "illegal prefix ordering OK\n"
success_end:
