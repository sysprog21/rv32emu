/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* Retire a learned loop inside a trap. Offline direct edges must survive the
 * interpreter's unlinking, without a native function entry on every iteration.
 */
#if AOT_RVC
.option rvc
#else
.option norvc
#endif
.option norelax
.section .text
.globl _start
_start:
    la t0, trap
    csrw stvec, t0
    li s0, 100000
    li s1, 0
loop:
    jal ra, work
    addi s0, s0, -1
    bnez s0, loop
    li t0, 100000
    bne s1, t0, fail
    lw t0, 2(zero)
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
work:
    addi s1, s1, 1
    ret
.balign 4096
trap:
    .rept 20000
    csrr zero, sscratch
    .endr
    csrr t0, sepc
    addi t0, t0, 4
    csrw sepc, t0
    sret
.section .rodata
ok: .asciz "SYSTEM AOT static edges OK\n"
bad: .asciz "SYSTEM AOT static edges FAIL\n"
