/* Counter-free self-loops exercise T1 register retention and interpreter
 * pointer-reversal fusion. More than 1024 nodes forces native polling exits.
 */
    .option norvc
    .text
    .global _start
_start:
    la s1, nodes
    mv t0, s1
    li t1, 4096
    li t2, 0
init:
    addi t3, t0, 8
    sw t3, 0(t0)
    sw t2, 4(t0)
    addi t2, t2, 1
    mv t0, t3
    bne t2, t1, init
    sw zero, -8(t0)
    li s2, 50
again:
    mv a4, s1
    li a3, 0
reverse:
    mv s0, a4
    lw a4, 0(s0)
    sw a3, 0(s0)
    mv a3, s0
    bne a4, zero, reverse
    mv s3, a3
    li t2, 4095
verify_reverse:
    lw t0, 4(a3)
    bne t0, t2, fail
    lw a3, 0(a3)
    addi t2, t2, -1
    bne a3, zero, verify_reverse
    li t0, -1
    bne t2, t0, fail

    mv a4, s3
    li a3, 0
restore:
    mv s0, a4
    lw a4, 0(s0)
    sw a3, 0(s0)
    mv a3, s0
    bne a4, zero, restore
    bne a3, s1, fail
    li t2, 0
verify_forward:
    lw t0, 4(a3)
    bne t0, t2, fail
    lw a3, 0(a3)
    addi t2, t2, 1
    bne a3, zero, verify_forward
    li t0, 4096
    bne t2, t0, fail
    addi s2, s2, -1
    bne s2, zero, again

    /* A signed load and identity move keep the 32-bit value normalized.
     * Distinct branch operands prevent ADDI/BNE fusion from hiding the loop.
     */
    la a1, signed_value
    li t0, 0
    li t1, 20000
signed_loop:
    lh t3, 0(a1)
    addi t3, t3, 0
    sw t3, 4(a1)
    addi t0, t0, 1
    bne t0, t1, signed_loop
    lw t3, 4(a1)
    li t4, -1
    bne t3, t4, fail
    bne zero, zero, fail
    /* The load overwrites the first copy's source, and the store consumes
     * that new value before the last copy restores the original pointer.
     */
    la s0, signed_value
    mv a4, s0
    lw s0, 4(a4)
    sw s0, 4(a4)
    mv s0, a4
    la t0, signed_value
    bne s0, t0, fail
    lw t3, 4(s0)
    bne t3, t4, fail
    /* A trap after the first copy must retain that write. The default
     * misalignment handler resumes in the middle of the original sequence.
     */
    la a4, unaligned_value
    li a3, 0x76543210
    /* A new block hides the constant base, so the copy is not folded into an
     * immediate load and the five instructions still fuse.
     */
    j 1f
1:
    mv s0, a4
    lw a4, 1(s0)
    sw a3, 1(s0)
    mv a3, s0
    la t0, unaligned_value
    bne a3, t0, fail
    li t0, 0x44332211
    bne a4, t0, fail
    lw t0, 1(a3)
    li t1, 0x76543210
    bne t0, t1, fail
    /* This exact reversal shape faults on an unaligned base. The trap must
     * publish the copied base before resuming the original load/store pair.
     */
    la a4, unaligned_null
    addi a4, a4, 1
    li a3, 0x12345678
    j 1f
1:
    mv s0, a4
    lw a4, 0(s0)
    sw a3, 0(s0)
    mv a3, s0
    bne a4, zero, fail
    la t0, unaligned_null
    addi t0, t0, 1
    bne s0, t0, fail
    bne a3, t0, fail
    lw t0, 0(a3)
    li t1, 0x12345678
    bne t0, t1, fail
#ifdef CHECK_CYCLES
    /* The fused fallthrough still accounts for all five instructions. */
    la a4, null_node
    li a3, 0
    j 1f
1:
    rdcycle t5
    mv s0, a4
    lw a4, 0(s0)
    sw a3, 0(s0)
    mv a3, s0
    bne a4, zero, fail
    rdcycle t6
    sub t6, t6, t5
    li t5, 6
    bne t6, t5, fail
#endif
    li a0, 1
    la a1, passed
    li a2, 17
    li a7, 64
    ecall
    li a0, 0
    j exit
fail:
    li a0, 1
exit:
    li a7, 93
    ecall

    .section .rodata
passed:
    .ascii "register loop OK\n"
    .data
    .balign 4
signed_value:
    .word 0xffff, 0
unaligned_value:
    .byte 0, 0x11, 0x22, 0x33, 0x44, 0, 0, 0
unaligned_null:
    .byte 0, 0, 0, 0, 0, 0, 0, 0
    .balign 4
null_node:
    .word 0
    .bss
    .balign 4
nodes:
    .space 32768
