/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* Evict the interrupted block from inside a trap, then run code first decoded
 * in that trap through the ordinary dispatcher until T1 heats it. This checks
 * deferred IR reclamation and fallthrough exits of single-instruction blocks.
 */
.option norvc
.option norelax
.section .text
.globl _start
_start:
    la t0, trap
    csrw stvec, t0
    li t2, 0x12345678
    lw t2, 2(zero)
    li t0, 0x12345678
    bne t2, t0, fail
    li a0, 0
    li a1, 0
    li s0, 10000
loop:
    jal ra, work
    addi s0, s0, -1
    bnez s0, loop
    li t0, 10000
    bne a0, t0, fail
    li t0, 30000
    bne a1, t0, fail
    xor t0, a0, a1
    bne a2, t0, fail
    la a0, ok
    j print
trap:
    mv s3, ra
    /* More than the 16384-entry AOT block cache. These CSR reads are deliberately
     * ineligible for AOT, so the stress does not create a giant artifact.
     */
    .rept 20000
    csrr zero, sscratch
    .endr
    jal ra, work
    mv ra, s3
    csrr t0, sepc
    addi t0, t0, 4
    csrw sepc, t0
    sret
work:
    addi a0, a0, 1
    addi a1, a1, 3
    xor a2, a0, a1
    ret
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
ok: .asciz "SYSTEM AOT trap cache OK\n"
bad: .asciz "SYSTEM AOT trap cache FAIL\n"
