/* FENCE.I must not disqualify the supported prefix at 0x1000. Repeated
 * invalidation verifies that collection and replay preserve this boundary.
 */
.option norvc
.option norelax
.section .text
.globl _start
_start:
    csrw sstatus, zero
    li s0, 128
    li s1, 0
    j loop
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
.org 0x1000
loop:
#if AOT_RVC
    .option push
    .option rvc
    c.addi s1, 1
    .option pop
#else
    addi s1, s1, 1
#endif
    .option push
    .option arch, +zifencei
    fence.i
    .option pop
    addi s0, s0, -1
    bnez s0, loop
    li t0, 128
    bne s1, t0, fail
    la a0, ok
    j print
.section .rodata
ok: .asciz "SYSTEM AOT collection OK\n"
bad: .asciz "SYSTEM AOT collection FAIL\n"
