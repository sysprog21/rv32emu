/* Zbb rotations mask counts, including the complementary shift at zero. */
.option norvc
.option norelax
.text
.globl _start
_start:
    la s0, values
    li s1, 7
value_loop:
    lw a0, 0(s0)
    la s2, counts
    li s3, 10
count_loop:
    lw a1, 0(s2)
    neg t0, a1
    sll t1, a0, a1
    srl t2, a0, t0
    or a2, t1, t2
    rol a3, a0, a1
    bne a3, a2, fail
    mv a3, a0
    rol a3, a3, a1
    bne a3, a2, fail
    mv a3, a1
    rol a3, a0, a3
    bne a3, a2, fail
    srl t1, a0, a1
    sll t2, a0, t0
    or a2, t1, t2
    ror a3, a0, a1
    bne a3, a2, fail
    mv a3, a0
    ror a3, a3, a1
    bne a3, a2, fail
    mv a3, a1
    ror a3, a0, a3
    bne a3, a2, fail
    addi s2, s2, 4
    addi s3, s3, -1
    bnez s3, count_loop
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    .if \n == 0
    mv a2, a0
    .else
    srli t1, a0, \n
    slli t2, a0, (32-\n)
    or a2, t1, t2
    .endif
    rori a3, a0, \n
    bne a3, a2, fail
    mv a3, a0
    rori a3, a3, \n
    bne a3, a2, fail
    .endr
    addi s0, s0, 4
    addi s1, s1, -1
    bnez s1, value_loop
    li a0, 1
    la a1, success
    la a2, success_end
    sub a2, a2, a1
    li a7, 64
    ecall
    li a0, 0
    j stop
fail:
    li a0, 1
stop:
    li a7, 93
    ecall
.section .rodata
.balign 4
values: .word 0, 1, -1, 0x80000000, 0x7fffffff, 0x12345678, 0xaaaaaaaa
counts: .word 0, 1, 2, 31, 32, 33, 63, 64, 0xffffffff, 0x80000000
success: .ascii "Zbb rotations OK\n"
success_end:
