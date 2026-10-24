/* SIE-only writes may resume with interrupts disabled. Pending interrupts
 * must stay masked until the enabling write reaches its dispatcher boundary.
 */
.option norvc
.option norelax
.section .text
.globl _start
_start:
    la t0, trap
    csrw stvec, t0
    csrw sstatus, zero
    csrsi sie, 2
    csrsi sip, 2
    li s3, 0
    li s1, 0
    li s0, 10000
loop:
    csrrci t0, sstatus, 2
    csrrci t3, sstatus, 2
    xor t3, t3, t0
    bnez t3, fail
    csrrw t0, sstatus, s1
    andi t0, t0, 2
    bnez t0, fail
    rdinstret t0
    addi t1, zero, 1
    rdinstret t2
    sub t2, t2, t0
    li t1, 2
    bne t2, t1, fail
    bnez s3, fail
    addi s0, s0, -1
    bnez s0, loop
    csrsi sstatus, 2
    li s0, 10000
wait:
    bnez s3, delivered
    addi s0, s0, -1
    bnez s0, wait
    j fail
delivered:
    li t0, 1
    bne s3, t0, fail
    csrci sstatus, 2
    la a0, ok
    j print
trap:
    csrr t0, scause
    li t1, 0x80000001
    bne t0, t1, fail
    csrci sip, 2
    addi s3, s3, 1
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
ok: .asciz "SYSTEM AOT status OK\n"
bad: .asciz "SYSTEM AOT status FAIL\n"
