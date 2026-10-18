/* Flat S-mode kernel, loaded at address zero with -k. A trap in the middle of
 * a block retires only the instructions before it, so the instructions
 * counted between two traps must not change once T1 compiles the block that
 * traps. Run loops whose first access traps: a misaligned load, then a load, a
 * LUI and LW pair that may be fused, and an AMO on an unmapped page, whose
 * handler runs inside the access. A fault on the fused LW must name the LW, not
 * the LUI. Each loop runs long enough to reach T1, and the loops differ only in
 * their first two instructions, so every interval between consecutive traps
 * must be equal.
 */
.option norvc
.option norelax
.section .text
.globl _start
_start:
    la t0, trap
    csrw stvec, t0
    li s6, 0                /* the interval every trap must match */
    la s0, data
    addi s0, s0, 1          /* misaligned word address */
    jal ra, run_load
    /* Map the low 4 MiB and the UART only: VA 0x400000 has no leaf. */
    la t0, root
    li t1, 0xcf
    sw t1, 0(t0)
    li t1, 0x3d0000cf
    li t2, 3904
    add t2, t2, t0
    sw t1, 0(t2)
    srli t0, t0, 12
    li t1, 0x80000000
    or t0, t0, t1
    csrw satp, t0
    sfence.vma
    li s0, 0x400000
    jal ra, run_load
    jal ra, run_fused
    jal ra, run_amo
    la a0, success
    bnez s7, fail
    j print

/* Trap 30000 times on the access at s0, each at the start of a block. */
.macro trap_loop pre, access
    li s1, 30000
    li s5, 0
    li s9, 1
1:
    j 2f
2:
    \pre
    \access
    addi s2, s2, 1
    addi s2, s2, 1
    addi s2, s2, 1
    addi s2, s2, 1
    addi s2, s2, 1
    addi s2, s2, 1
    addi s2, s2, 1
    addi s1, s1, -1
    bnez s1, 1b
    ret
.endm
run_load:
    trap_loop "lui t3, 0", "lw t2, 0(s0)"
run_fused:
    trap_loop "lui t3, 0x400", "lw t2, 0(t3)"
run_amo:
    trap_loop "lui t3, 0", "amoadd.w t2, t2, (s0)"

/* Count the instructions retired from the previous trap's return to this
 * trap: the loop's path, independent of how long this handler runs. Compare
 * it with the first such count, then skip the trapping access.
 */
trap:
    rdinstret t0
    beqz s9, 3f
    addi s9, s9, -1         /* the first returns precede a measured one */
    j 2f
3:
    sub t1, t0, s5
    bnez s6, 1f
    mv s6, t1
    j 2f
1:
    beq t1, s6, 2f
    li s7, 1
2:
    csrr t0, sepc
    addi t0, t0, 4
    csrw sepc, t0
    rdinstret s5
    sret

fail:
    la a0, failure
print:
    li t1, 0xf4000000
1:
    lbu t0, 0(a0)
    beqz t0, stop
    sb t0, 0(t1)
    addi a0, a0, 1
    j 1b
stop:
    li a7, 0x53525354
    li a6, 0
    li a0, 0
    li a1, 0
    ecall
    j stop
success: .asciz "SYSTEM trap cycles OK\n"
failure: .asciz "SYSTEM trap cycles FAIL\n"
.balign 4
data: .word 0, 0
.set root, 0x4000
.org 0x5000, 0
