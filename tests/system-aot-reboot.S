/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* The host requests one warm reboot, then shutdown on the second boot. */
.option norvc
.option norelax
.section .text
.globl _start
_start:
    la a0, ready
    li s0, 0xf4000000
1:
    lbu t0, 0(a0)
    beqz t0, wait
    sb t0, 0(s0)
    addi a0, a0, 1
    j 1b
wait:
    wfi
    lbu t0, 5(s0)
    andi t0, t0, 1
    beqz t0, wait
    lbu t0, 0(s0)
    li t1, 114
    li a0, 0
    bne t0, t1, reset
    li a0, 2
reset:
    li a1, 0
    li a6, 0
    li a7, 0x53525354
    ecall
    j .
.section .rodata
ready: .asciz "AOT_REBOOT_READY\n"
