/*
 * A fault on the following page must not run before this page's instructions
 * retire. Otherwise a handler can observe stale register state and the
 * translated instructions can run after the handler returns.
 */
    .option norvc
    .text
    .globl _start
_start:
    .option push
    .option norelax
    la   gp, __global_pointer$
    .option pop
    la   t0, root
    srli t0, t0, 12
    li   t1, 0x80000000
    or   t0, t0, t1
    csrw satp, t0
    sfence.vma
    la   t0, trap_handler
    csrw stvec, t0
    li   s0, 0
    li   s1, 0
    li   s2, 0
    la   t0, boundary
    jr   t0

trap_handler:
    csrr t0, scause
    li   t1, 12
    bne  t0, t1, fail
    mv   s2, s0
    addi s1, s1, 1
    la   t0, recover
    csrw sepc, t0
    sret

recover:
    /*
     * A partial translated block may return here after an inline page fault.
     * Keep this target distinct from the checks so any resumed block is seen.
     */
    nop
    nop
    j    verify

verify:
    li   t0, 2
    bne  s2, t0, fail
    bne  s0, t0, fail
    li   t0, 1
    bne  s1, t0, fail
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

    .section .data,"aw"
    .balign 4096
root:
    .word 0xcf
    .space 4092
success:
    .ascii "fetch fault ordering OK\n"
success_end:

    /*
     * The test target places this section at the last eight bytes of the
     * 4 MiB mapping with --section-start.
     */
    .section .faultpage,"ax"
boundary:
    addi s0, s0, 1
    addi s0, s0, 1
