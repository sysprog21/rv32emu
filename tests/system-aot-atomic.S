/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* Native page graphs must preserve reservations, x0, and CSR helper effects. */
.option norvc
.option norelax
.section .text
.globl _start
_start:
    la s1, value
    li s0, 1000
    li s2, 0
    li s3, 1
loop:
    lr.w t0, (s1)
    bne t0, s2, fail
    sc.w t1, t0, (s1)
    bnez t1, fail
    /* Without a reservation, sc.w must fail and leave memory unchanged. */
    addi t3, s2, 0x55
    sc.w t1, t3, (s1)
    li t2, 1
    bne t1, t2, fail
    lw t3, (s1)
    bne t3, s2, fail
    amoadd.w t0, s3, (s1)
    bne t0, s2, fail
    addi s2, s2, 1
    amoswap.w zero, s2, (s1)
    bnez zero, fail
    lr.w zero, (s1)
    sc.w t1, s2, (s1)
    bnez t1, fail
    csrrci t0, sstatus, 2
    csrrsi t1, sstatus, 2
    andi t1, t1, 2
    bnez t1, fail
    csrr t1, sstatus
    andi t1, t1, 2
    beqz t1, fail
    csrw sstatus, t0
    addi s0, s0, -1
    bnez s0, loop
    lw t0, 0(s1)
    bne t0, s2, fail
    la a0, ok
    j print
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
ok: .asciz "SYSTEM AOT atomic OK\n"
bad: .asciz "SYSTEM AOT atomic FAIL\n"
.section .data
.balign 4
value: .word 0
