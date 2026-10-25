/* RV32M boundaries must execute through native AOT without host faults.
 * RAM operands prevent the collector from folding away exceptional cases.
 */
    .option norvc
    .text
    .global _start
_start:
    li s0, 2000
outer:
    la s1, cases
    li s2, 16
loop:
    lw t0, 0(s1)
    lw t1, 4(s1)
    div t2, t0, t1
    lw t3, 8(s1)
    bne t2, t3, fail
    divu t2, t0, t1
    lw t3, 12(s1)
    bne t2, t3, fail
    rem t2, t0, t1
    lw t3, 16(s1)
    bne t2, t3, fail
    remu t2, t0, t1
    lw t3, 20(s1)
    bne t2, t3, fail
    /* Destination aliases either input, and x0 discards a result. */
    div t0, t0, t1
    lw t3, 8(s1)
    bne t0, t3, fail
    lw t0, 0(s1)
    rem t1, t0, t1
    lw t3, 16(s1)
    bne t1, t3, fail
    divu t0, t0, t0
    lw t3, 24(s1)
    bne t0, t3, fail
    mul zero, t0, t1
    mulh zero, t0, t1
    mulhsu zero, t0, t1
    mulhu zero, t0, t1
    div zero, zero, zero
    divu zero, t0, t1
    rem zero, t0, t1
    remu zero, zero, zero
    bnez zero, fail
    addi s1, s1, 28
    addi s2, s2, -1
    bnez s2, loop
    addi s0, s0, -1
    bnez s0, outer
    li a0, 0
    j exit
fail:
    li a0, 1
exit:
    li a7, 93
    ecall
    .data
    .balign 4
cases:
    .word 0xf8a432eb, 0x00000000, 0xffffffff, 0xffffffff, 0xf8a432eb, 0xf8a432eb, 0x00000001
    .word 0x80000000, 0xffffffff, 0x80000000, 0x00000000, 0x00000000, 0x80000000, 0x00000001
    .word 0x80000000, 0x00000001, 0x80000000, 0x80000000, 0x00000000, 0x00000000, 0x00000001
    .word 0x80000000, 0x80000000, 0x00000001, 0x00000001, 0x00000000, 0x00000000, 0x00000001
    .word 0x7fffffff, 0xffffffff, 0x80000001, 0x00000000, 0x00000000, 0x7fffffff, 0x00000001
    .word 0xfffffff9, 0x00000003, 0xfffffffe, 0x55555553, 0xffffffff, 0x00000000, 0x00000001
    .word 0x00000007, 0xfffffffd, 0xfffffffe, 0x00000000, 0x00000001, 0x00000007, 0x00000001
    .word 0xfffffff9, 0xfffffffd, 0x00000002, 0x00000000, 0xffffffff, 0xfffffff9, 0x00000001
    .word 0x00000000, 0x00000000, 0xffffffff, 0xffffffff, 0x00000000, 0x00000000, 0xffffffff
    .word 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffffffff
    .word 0xffffffff, 0xffffffff, 0x00000001, 0x00000001, 0x00000000, 0x00000000, 0x00000001
    .word 0x00000001, 0xffffffff, 0xffffffff, 0x00000000, 0x00000000, 0x00000001, 0x00000001
    .word 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000000, 0x00000000, 0x00000001
    .word 0x7fffffff, 0x00000002, 0x3fffffff, 0x3fffffff, 0x00000001, 0x00000001, 0x00000001
    .word 0xffffffff, 0x00000002, 0x00000000, 0x7fffffff, 0xffffffff, 0x00000001, 0x00000001
    .word 0x80000000, 0x00000003, 0xd5555556, 0x2aaaaaaa, 0xfffffffe, 0x00000002, 0x00000001
