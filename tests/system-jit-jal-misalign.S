/*
 * Repeatedly trap on a direct jump to a halfword-aligned target without C. The
 * jump does not retire: sepc is the jump, stval the target, rd unchanged.
 */
.option norvc
.option norelax
.section .text
.globl _start
_start:
    la t0, trap
    csrw stvec, t0
    li s0, 10000
    li s1, 0
loop:
    li t0, 0x1234
    jal ra, work
after:
    addi s0, s0, -1
    bnez s0, loop
    li t0, 10000
    bne s1, t0, fail
    la a0, ok
    j print
work:
    .word 0x002002ef       /* jal t0, +2 */
    j fail
trap:
    csrr t1, scause
    bnez t1, fail
    csrr t1, sepc
    la t2, work
    bne t1, t2, fail
    csrr t1, stval
    addi t2, t2, 2
    bne t1, t2, fail
    li t2, 0x1234
    bne t0, t2, fail
    addi s1, s1, 1
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
ok: .asciz "SYSTEM JAL misalignment OK\n"
bad: .asciz "SYSTEM JAL misalignment FAIL\n"
