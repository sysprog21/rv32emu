/* The ELF contains no counter read; FENCE.I exposes the generated one. */
    .option norvc
    .text
    .global _start
_start:
    /* Run a native hot loop before the newly generated counter exists. */
    li s0, 6000
preloop:
    addi s0, s0, -1
    bnez s0, preloop
    la t0, generated
    li t1, 0xc00029f3       /* csrrs s3, cycle, zero */
    sw t1, 0(t0)
    fence.i
    jal ra, generated
    mv s4, s3
    li s0, 6000
loop:
    addi s0, s0, -1
    bnez s0, loop
    jal ra, generated
    sub s5, s3, s4
    li t0, 12000
    bltu s5, t0, fail
    li a0, 1
    la a1, msg
    li a2, 24
    li a7, 64
    ecall
    li a0, 0
    li a7, 93
    ecall
fail:
    li a0, 1
    li a7, 93
    ecall
    .balign 4
generated:
    addi s3, zero, 0
    ret
    .section .rodata
msg:
    .asciz "user FENCE.I counter OK\n"
