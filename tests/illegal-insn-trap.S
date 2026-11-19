/* An instruction that does not decode raises a precise illegal-instruction
 * trap, whether it starts its block or follows other instructions in it. The
 * user-mode emulator used to halt when it started the block (#811). Each case
 * jumps to a fresh block, checks that the instructions before the illegal one
 * ran, and that exactly one trap was taken with mepc naming that instruction.
 * The first case runs without a trap vector, where the default handler skips
 * the instruction.
 *
 * A failing case exits with its own status and prints "illegal insn trap FAIL".
 */
    .option norvc
    .option norelax

    .equ SYSWRITE, 64
    .equ SYSEXIT, 93
    .equ ILLEGAL_INSN, 2

    .data
ok_msg:   .ascii "illegal insn trap OK\n"
    .equ ok_len, . - ok_msg
fail_msg: .ascii "illegal insn trap FAIL\n"
    .equ fail_len, . - fail_msg

    .text
    .global _start
_start:
    li   s10, 0                 /* traps taken */

    /* No trap vector: the default handler skips the instruction. */
    li   s1, 1
    csrw mtvec, zero
    j    1f
1:
    .word 0xffffffff
    li   t2, 1
    bnez s10, fail
    beqz t2, fail

    la   t0, handler
    csrw mtvec, t0

    /* The illegal instruction starts its block. */
    li   s1, 2
    j    1f
1:
    .word 0xffffffff
    li   t0, 1
    bne  s10, t0, fail
    li   t0, ILLEGAL_INSN
    bne  s11, t0, fail
    la   t0, 1b
    bne  s8, t0, fail

    /* The illegal instruction follows another one in its block. */
    li   s1, 3
    li   t2, 0
    j    1f
1:
    li   t2, 7
2:
    .word 0xffffffff
    li   t0, 7
    bne  t2, t0, fail
    li   t0, 2
    bne  s10, t0, fail
    la   t0, 2b
    bne  s8, t0, fail

    /* The trap vector is set in the same block as the illegal instruction. */
    li   s1, 4
    csrw mtvec, zero
    j    1f
1:
    la   t0, handler
    csrw mtvec, t0
2:
    .word 0xffffffff
    li   t0, 3
    bne  s10, t0, fail
    la   t0, 2b
    bne  s8, t0, fail

    li   a0, 1
    la   a1, ok_msg
    li   a2, ok_len
    li   a7, SYSWRITE
    ecall
    li   a0, 0
    j    exit

fail:
    li   a0, 1
    la   a1, fail_msg
    li   a2, fail_len
    li   a7, SYSWRITE
    ecall
    mv   a0, s1
exit:
    li   a7, SYSEXIT
    ecall

    /* Record the exception and resume after the faulting instruction. */
    .balign 4
handler:
    addi s10, s10, 1
    csrr s11, mcause
    csrr s8, mepc
    addi t6, s8, 4
    csrw mepc, t6
    mret
