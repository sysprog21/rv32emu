/*
 * Take a misaligned branch at the tail of a loop-fed T1C region. The
 * fall-through block is warm and cached, but the taken target is not. CALLER: a
 * loop calls a body ending in "addi; bne" (a fused tail). Otherwise a single
 * loop has "sltiu; bne" followed by "addi; addi; bnez".
 */
.option norvc
.option norelax
.text
.globl _start
.globl fault_branch
.globl link_point
_start:
    la t0, handler
    csrw mtvec, t0
    li ra, 0x1234
    li s0, 6000
    li s7, 0
    rdcycle s6
#ifdef CALLER
again:
    sltiu s1, s0, 2
    jal ra, branch_body
link_point:
    addi s0, s0, -1
    bnez s0, again
    j .
branch_body:
    addi s1, s1, 0
fault_branch:
    /* bne s1, x0, +2 */
    .word ((0 << 20) | (9 << 15) | (1 << 12) | (1 << 8) | 0x63)
    ret
#else
again:
    sltiu s1, s0, 2
fault_branch:
    /* bne s1, x0, +2 */
    .word ((0 << 20) | (9 << 15) | (1 << 12) | (1 << 8) | 0x63)
    addi s0, s0, -1
    addi s7, s7, 1
    bnez s0, again
link_point:
    j .
#endif
handler:
    csrr s2, mepc
    csrr s3, mtval
    csrr s4, mcause
    rdcycle s5
    csrw mtvec, zero
    li a0, 0
    li a7, 93
    ecall
