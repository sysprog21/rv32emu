/* Compressed FP memory operations must remain interpreted until every JIT
 * tier has emitters. Keep each form hot enough to trigger compilation if its
 * decoder flag is accidentally enabled, and compare the exact stored bits.
 */
.option rvc
.text
.globl _start
_start:
    addi sp, sp, -16
    sw zero, 0(sp)
    la s0, data
    li s1, 200000
loop:
    c.flw fs0, 0(s0)
    c.fsw fs0, 4(s0)
    c.flwsp fs1, 0(sp)
    c.fswsp fs0, 0(sp)
    c.flwsp fs1, 0(sp)
    c.fswsp fs1, 4(sp)
    addi s1, s1, -1
    bnez s1, loop
    lw t0, 0(s0)
    lw t1, 4(s0)
    bne t0, t1, fail
    lw t1, 0(sp)
    bne t0, t1, fail
    lw t1, 4(sp)
    bne t0, t1, fail
    li a0, 1
    la a1, passed
    li a2, 21
    li a7, 64
    ecall
    li a0, 0
    j exit
fail:
    li a0, 1
exit:
    li a7, 93
    ecall
.section .rodata
passed:
    .ascii "JIT compressed FP OK\n"
.data
.balign 4
data:
    .word 0xc0490fdb, 0
