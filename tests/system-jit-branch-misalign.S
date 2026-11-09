/* Repeatedly trap on a taken conditional branch to a halfword target. */
.option norvc
.option norelax
.text
.globl _start
_start:
    la t0, trap
    csrw stvec, t0
    li s0, 6000
    li s1, 1
    li s2, 0
loop:
    rdcycle s3
    jal ra, work
    addi s0, s0, -1
    bnez s0, loop
    li t0, 6000
    bne s2, t0, fail
    la a0, ok
    j print
work:
    addi s1, s1, 0
fault_branch:
    .word 0x00049163 /* bne s1, zero, +2; also fuses with ADDI */
    j fail
trap:
    rdcycle s4
    sub s4, s4, s3
    li t0, 4
    bne s4, t0, fail
    csrr t1, scause
    bnez t1, fail
    csrr t1, sepc
    la t2, fault_branch
    bne t1, t2, fail
    csrr t1, stval
    addi t2, t2, 2
    bne t1, t2, fail
    li t0, 1
    bne s1, t0, fail
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
ok: .asciz "SYSTEM branch misalignment OK\n"
bad: .asciz "SYSTEM branch misalignment FAIL\n"
