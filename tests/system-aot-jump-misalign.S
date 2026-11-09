/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* Trap repeatedly on a taken branch, JAL and JALR to a halfword target without
 * C, in hot loops that an AOT artifact compiles. The jump does not retire: sepc
 * is the jump, stval the target and rd keeps its value. The phases are a branch
 * fused with the ADDI before it, a JAL, a JALR, a JALR whose rd aliases rs1,
 * and a JALR without a link register.
 */
.option norvc
.option norelax
.section .text
.globl _start
_start:
    la t0, trap
    csrw stvec, t0

    /* Phase 1: addi s1, s1, 0; bne s1, zero, +2 with s1 = 1. */
    la s5, branch_jump
    li s0, 6000
    li s1, 1
    li s2, 0
branch_loop:
    li t0, 0x1234
    jal ra, branch_work
    addi s0, s0, -1
    bnez s0, branch_loop
    li t0, 6000
    bne s2, t0, fail

    /* Phase 2: jal t0, +2. */
    la s5, jal_jump
    li s0, 6000
    li s2, 0
jal_loop:
    li t0, 0x1234
    jal ra, jal_work
    addi s0, s0, -1
    bnez s0, jal_loop
    li t0, 6000
    bne s2, t0, fail

    /* Phase 3: jalr t0, 0(t2). */
    la s5, jalr_jump
    li s0, 6000
    li s2, 0
jalr_loop:
    li t0, 0x1234
    addi t2, s5, 2
    jal ra, jalr_work
    addi s0, s0, -1
    bnez s0, jalr_loop
    li t0, 6000
    bne s2, t0, fail

    /* Phase 4: jalr t2, 0(t2), where rd aliases rs1. */
    la s5, alias_jump
    li s0, 6000
    li s2, 0
alias_loop:
    li t0, 0x1234
    addi t2, s5, 2
    jal ra, alias_work
    addi s0, s0, -1
    bnez s0, alias_loop
    li t0, 6000
    bne s2, t0, fail

    /* Phase 5: jalr zero, 0(t2). */
    la s5, nolink_jump
    li s0, 6000
    li s2, 0
nolink_loop:
    li t0, 0x1234
    addi t2, s5, 2
    jal ra, nolink_work
    addi s0, s0, -1
    bnez s0, nolink_loop
    li t0, 6000
    bne s2, t0, fail
    la a0, ok
    j print

branch_work:
    addi s1, s1, 0
branch_jump:
    .word 0x00049163 /* bne s1, zero, +2 */
    j fail
jal_work:
jal_jump:
    .word 0x002002ef /* jal t0, +2 */
    j fail
jalr_work:
jalr_jump:
    jalr t0, 0(t2)
    j fail
alias_work:
alias_jump:
    jalr t2, 0(t2)
    j fail
nolink_work:
nolink_jump:
    jalr zero, 0(t2)
    j fail

trap:
    csrr t3, scause
    bnez t3, fail
    csrr t3, sepc
    bne t3, s5, fail
    csrr t3, stval
    addi t4, s5, 2
    bne t3, t4, fail
    /* rd is unchanged: the link value or, when aliased, the target. */
    la t3, alias_jump
    bne s5, t3, 1f
    bne t2, t4, fail
    j 2f
1:
    li t3, 0x1234
    bne t0, t3, fail
2:
    addi s2, s2, 1
    csrw sepc, ra
    sret
fail:
    la a0, bad
print:
    li t1, 0xf4000000
1:
    lbu t0, 0(a0)
    beqz t0, shutdown
    sb t0, 0(t1)
    addi a0, a0, 1
    j 1b
shutdown:
    li a0, 0
    li a1, 0
    li a6, 0
    li a7, 0x53525354
    ecall
    j .
.section .rodata
ok: .asciz "SYSTEM AOT jump misalignment OK\n"
bad: .asciz "SYSTEM AOT jump misalignment FAIL\n"
