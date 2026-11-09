/*
 * Trap repeatedly on a JAL or JALR to a halfword target without C. The jump
 * does not retire: mepc is the jump, mtval the target and rd is unchanged.
 * The _DIRTY variants write rd in the trapping block itself, so that its
 * value is still in a host register when the jump traps.
 */
.option norvc
.option norelax
.text
.globl _start
_start:
    la t0, handler
    csrw mtvec, t0
    li s0, 6000
    li s1, 0
loop:
    la t2, work
#ifdef JALR_ALIAS_DIRTY
    addi t2, t2, 0x42
#else
    addi t2, t2, 2
#endif
#ifdef JALR_DIRTY
    li t0, 0x1111
#else
    li t0, 0x1234
#endif
    jal ra, prework
    addi s0, s0, -1
    bnez s0, loop
    li t0, 6000
    bne s1, t0, fail
    li a0, 0
    j exit
prework:
#ifdef JALR_DIRTY
    li t0, 0x1234
#elif defined(JALR_ALIAS_DIRTY)
    addi t2, t2, -0x40
#endif
work:
#if defined(JALR_ALIAS) || defined(JALR_ALIAS_DIRTY)
    jalr t2, 0(t2) /* rd aliases rs1 */
#elif defined(JALR) || defined(JALR_DIRTY)
    jalr t0, 0(t2)
#elif defined(JALR_NOLINK)
    jalr zero, 0(t2) /* checked on the indirect jump's miss path */
#else
    .word 0x002002ef /* jal t0, +2 */
#endif
    ret
handler:
    csrr t4, mcause
    bnez t4, fail
    la t5, work
    csrr t4, mepc
    bne t4, t5, fail
    addi t5, t5, 2
    csrr t4, mtval
    bne t4, t5, fail
#if defined(JALR_ALIAS) || defined(JALR_ALIAS_DIRTY)
    bne t2, t5, fail
#else
    li t5, 0x1234
    bne t0, t5, fail
#endif
    addi s1, s1, 1
    csrw mepc, ra
    mret
fail:
    li a0, 1
exit:
    li a7, 93
    ecall
