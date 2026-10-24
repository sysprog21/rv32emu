/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* Revalidate unchanged instruction pages and both versions of modified code.
 * The separate work page must be rebound after each instruction fence.
 */
.option norvc
.option norelax
.section .text
.globl _start
_start:
    li s1, 0
stage:
    li s0, 1000
    li a0, 0
loop:
    jal ra, work
    addi s0, s0, -1
    bnez s0, loop
    li t0, 1000
    li t1, 2
    bne s1, t1, check
    li t0, 2000
check:
    bne a0, t0, fail
    li t0, 3
    beq s1, t0, done
    beqz s1, unchanged
    la t0, work
    li t1, 0x00250513 /* addi a0, a0, 2 */
    li t2, 1
    beq s1, t2, patch
    li t1, 0x00150513 /* addi a0, a0, 1 */
patch:
    sw t1, 0(t0)
unchanged:
    .word 0x0000100f /* fence.i */
    addi s1, s1, 1
    j stage
done:
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
ok: .asciz "SYSTEM AOT fence OK\n"
bad: .asciz "SYSTEM AOT fence FAIL\n"
.section .text
.balign 4096
work:
    addi a0, a0, 1
    ret
