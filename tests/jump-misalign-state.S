/*
 * Trap repeatedly on a JAL or JALR to a halfword target without C, and leave
 * what the handler observed in registers, so that engines can be compared:
 *   s1 trap count, s2 mcause, s3 mepc, s4 mtval, s5 rd after the trap,
 *   s6 instructions retired between the first and the last trap.
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
    la t2, trapping_jump
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
    jal ra, work
    addi s0, s0, -1
    bnez s0, loop
    sub s6, s6, s7
    li a0, 0
    li a7, 93
    ecall
work:
    /* Keep unrelated dirty values live across the trap, including host
     * registers that double as helper argument registers.
     */
    addi t3, s0, 11
    addi t4, s0, 22
    addi t5, s0, 33
#ifdef JALR_DIRTY
    li t0, 0x1234
#elif defined(JALR_ALIAS_DIRTY)
    addi t2, t2, -0x40
#endif
trapping_jump:
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
    mv s8, t3
    mv s9, t4
    mv s10, t5
    csrr s2, mcause
    csrr s3, mepc
    csrr s4, mtval
#if defined(JALR_ALIAS) || defined(JALR_ALIAS_DIRTY)
    mv s5, t2
#else
    mv s5, t0
#endif
    csrr s6, instret
    bnez s1, 1f
    mv s7, s6
1:
    addi s1, s1, 1
    csrw mepc, ra
    mret
