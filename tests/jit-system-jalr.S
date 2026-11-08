/*
 * Exercise hot indirect jumps and alignment traps without the C extension. A
 * trapping jump reports itself as sepc, the target as stval and does not write
 * rd.
 */
.option norvc
.option norelax
.section .text
.globl _start
_start:
    la t0, trap
    csrw stvec, t0
    li s0, 6000
    li s1, 0
aligned_loop:
    la t0, aligned_target + 1
    j aligned_jump
aligned_return:
    addi s0, s0, -1
    bnez s0, aligned_loop
    li t0, 6000
    bne s1, t0, fail
    li s0, 6000
    li s1, 0
    li s2, 0
    li s9, 1
    li s7, 0
    la s4, alias_jump
    la s5, alias_target
alias_loop:
    la t0, alias_target + 3
    j alias_entry
alias_return:
    addi s0, s0, -1
    bnez s0, alias_loop
    li t0, 6000
    bne s1, t0, fail
    bne s7, t0, fail
    mv a1, s2
    jal ra, print_cycles
    li s0, 6000
    li s1, 0
    li s2, 0
    li s9, 1
    li s7, 0
    la s4, zero_jump
    la s5, zero_target
zero_loop:
    la t0, zero_target + 3
    j zero_entry
zero_return:
    addi s0, s0, -1
    bnez s0, zero_loop
    li t0, 6000
    bne s1, t0, fail
    bne s7, t0, fail
    mv a1, s2
    jal ra, print_cycles
    la a0, ok
    j print
aligned_jump:
    jalr t0, 0(t0)
aligned_target:
    la t1, aligned_jump + 4
    bne t0, t1, fail
    addi s1, s1, 1
    j aligned_return
alias_entry:
    addi s7, s7, 1
alias_jump:
    jalr t0, 0(t0)
alias_target:
    j fail
zero_entry:
    addi s7, s7, 1
zero_jump:
    jalr zero, 0(t0)
zero_target:
    j fail
trap:
    rdinstret t3
    csrr t1, scause
    bnez t1, fail
    csrr t1, sepc
    bne t1, s4, fail
    csrr t1, stval
    addi t2, s5, 2
    bne t1, t2, fail
    /* The trapping jump does not write rd: t0 still holds target + 3. */
    addi t1, s5, 3
    bne t0, t1, fail
    la t1, alias_jump
    bne s4, t1, 1f
    la t2, alias_return
    j 2f
1:
    la t2, zero_return
2:
    beqz s9, 3f
    li s9, 0
    j 5f
3:
    sub t1, t3, s3
    bnez s2, 4f
    mv s2, t1
    j 5f
4:
    bne t1, s2, fail
5:
    addi s1, s1, 1
    csrw sepc, t2
    rdinstret s3
    sret
/* Emit the interval so a host can compare it across engines. */
print_cycles:
    li t1, 0xf4000000
    li t2, 28
1:
    srl t0, a1, t2
    andi t0, t0, 15
    addi t0, t0, 65
    sb t0, 0(t1)
    addi t2, t2, -4
    bgez t2, 1b
    li t0, 10
    sb t0, 0(t1)
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
ok: .asciz "SYSTEM JALR misalignment OK\n"
bad: .asciz "SYSTEM JALR misalignment FAIL\n"
