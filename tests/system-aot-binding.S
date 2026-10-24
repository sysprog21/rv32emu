/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* Cross-page native jumps must find bindings published by the C dispatcher.
 * A different slot calculation in LLVM would return on every loop iteration.
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
    csrw sstatus, zero
    li s0, 100000
    li s1, 0
    j first
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
.balign 4096
first:
    addi s1, s1, 1
    j second
.balign 4096
second:
    addi s0, s0, -1
    bnez s0, first
    li t0, 100000
    bne s1, t0, fail
    la a0, ok
    j print
.section .rodata
ok: .asciz "SYSTEM AOT binding OK\n"
bad: .asciz "SYSTEM AOT binding FAIL\n"
