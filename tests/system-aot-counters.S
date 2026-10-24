/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* Pure counter reads must remain precise inside native loops. This loop is
 * ineligible for T1, so an AOT binding exercises the counter lowering itself.
 */
.option norvc
.option norelax
.section .text
.globl _start
_start:
    li s0, 100000
loop:
    rdinstret t0
    addi t3, zero, 1
    rdinstret t1
    sub t1, t1, t0
    li t2, 2
    bne t1, t2, fail
    rdcycle t0
    rdtime t1
    sub t1, t1, t0
    li t2, 1
    bne t1, t2, fail
    rdcycleh t0
    rdtimeh t1
    bne t0, t1, fail
    csrrsi t0, instret, 0
    csrrci t1, instret, 0
    sub t1, t1, t0
    bne t1, t2, fail
    csrrc zero, instret, zero
    bnez zero, fail
    addi s0, s0, -1
    bnez s0, loop
    la s0, ok
    j print
fail:
    la s0, bad
print:
    li s1, 0xf4000000
1:
    lbu t0, 0(s0)
    beqz t0, shutdown
    sb t0, 0(s1)
    addi s0, s0, 1
    j 1b
shutdown:
    li a0, 0
    li a1, 0
    li a6, 0
    li a7, 0x53525354
    ecall
    j .
.section .rodata
ok: .asciz "SYSTEM AOT counters OK\n"
bad: .asciz "SYSTEM AOT counters FAIL\n"
