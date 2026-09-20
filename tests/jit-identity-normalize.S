/* Signed multiply/divide extends mapped operands on 64-bit hosts. Two sinks
 * observe the stale upper half, and both must be normalized:
 *
 *   - an identity operation feeding unsigned MULHU, and
 *   - a later use of the same register as a memory-address base, which the
 *     host adds in full 64-bit width. User mode spans 4 GiB and seats the
 *     stack above 0x80000000, so such a base is ordinary.
 *
 * Keep each sequence in one block; writeback would hide stale upper bits.
 */
    .option norvc
    .text
    .global _start
    .macro identity op, imm, dst, producer
    la t4, operands
    lw s1, 0(t4)
    lw s4, 4(t4)
    \producer t0, s1, s4
    \op \dst, s1, \imm
    mulhu t1, \dst, s4
    bnez t1, fail
    li t2, 0x80000000
    bne \dst, t2, fail
    .endm

    /* Sign-extended base register: the reload must hit the same slot. */
    .macro base_after producer
    addi s3, sp, -32
    li s5, 0x5a5a1234
    sw s5, 0(s3)
    la t4, operands
    lw t3, 4(t4)
    \producer t5, s3, t3
    lw t6, 0(s3)
    bne t6, s5, fail
    .endm
_start:
    li s0, 20000
loop:
    .irp producer, mulh, mulhsu, div, rem
    identity addi, 0, s2, \producer
    identity addi, 0, s1, \producer
    identity ori, 0, s2, \producer
    identity ori, 0, s1, \producer
    identity xori, 0, s2, \producer
    identity xori, 0, s1, \producer
    identity andi, -1, s2, \producer
    identity andi, -1, s1, \producer
    identity slli, 0, s2, \producer
    identity slli, 0, s1, \producer
    identity srli, 0, s2, \producer
    identity srli, 0, s1, \producer
    identity srai, 0, s2, \producer
    identity srai, 0, s1, \producer
    base_after \producer
    .endr
    addi s0, s0, -1
    bnez s0, loop
    li a0, 0
    j exit
fail:
    li a0, 1
exit:
    li a7, 93
    ecall
    .section .rodata
    .balign 4
operands:
    .word 0x80000000, 1
