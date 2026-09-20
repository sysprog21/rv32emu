/* Pinning the host memory base must preserve address offsets, load extension,
 * source/destination aliases, fused accesses, and compressed stack accesses.
 */
    .text
    .global _start
    .option norvc
_start:
    la s0, buffer
    addi s0, s0, 2047
    addi s0, s0, 1
    li s1, 20000
loop:
    li t1, 0x89abcdef
    /* AArch64 unscaled displacement limits and their fallback neighbors. */
    .irp offset, -257, -256, -255, 254, 255, 256
    sb t1, \offset(s0)
    lbu t2, \offset(s0)
    li t3, 0xef
    bne t2, t3, fail
    lb t2, \offset(s0)
    li t3, -17
    bne t2, t3, fail
    .endr
    sw t1, -2048(s0)
    lw t2, -2048(s0)
    bne t1, t2, fail
    sw t1, 2044(s0)
    lw t2, 2044(s0)
    bne t1, t2, fail
    lb t2, 2047(s0)
    li t3, -119
    bne t2, t3, fail
    lbu t2, 2047(s0)
    li t3, 137
    bne t2, t3, fail
    lh t2, 2046(s0)
    li t3, -30293
    bne t2, t3, fail
    lhu t2, 2046(s0)
    li t3, 0x89ab
    bne t2, t3, fail
    sb t1, -1(s0)
    lbu t2, -1(s0)
    li t3, 0xef
    bne t2, t3, fail
    sh t1, -2(s0)
    lhu t2, -2(s0)
    li t3, 0xcdef
    bne t2, t3, fail
    sw t1, 0(s0)
    mv t0, s0
    lw t0, 0(t0)
    bne t0, t1, fail
    sw t1, 4(s0)
    sw t1, 8(s0)
    lw t2, 4(s0)
    lw t3, 8(s0)
    bne t2, t1, fail
    bne t3, t1, fail
    /* A displaced guest store is not register-file writeback. Force a block
     * boundary before checking that its dirty source survived the spill.
     */
    lw t2, 0(s0)
    addi t2, t2, 1
    sw t2, 40(s0)
    j 4f
4:
    li t3, 0x89abcdf0
    bne t2, t3, fail
    /* Likewise, a displaced guest load creates a dirty result. */
    lw t2, 40(s0)
    j 5f
5:
    bne t2, t3, fail
    /* More live values than the x86 allocator has registers. Each store
     * must retain its address operand while allocating its source.
     */
    .set value_delta, 1
    .irp reg, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11
    lw \reg, 0(s0)
    addi \reg, \reg, value_delta
    .set value_delta, value_delta + 1
    .endr
    .set store_offset, 64
    .irp reg, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11
    sw \reg, store_offset(s0)
    .set store_offset, store_offset + 4
    .endr
    mv t3, t1
    .set load_offset, 64
    .rept 10
    addi t3, t3, 1
    lw t2, load_offset(s0)
    bne t2, t3, fail
    .set load_offset, load_offset + 4
    .endr
    sw zero, 12(s0)
    lw t2, 12(s0)
    bnez t2, fail
    /* Narrow x0 stores must not overwrite neighboring bytes. */
    sw t1, 24(s0)
    sb zero, 25(s0)
    lw t2, 24(s0)
    li t3, 0x89ab00ef
    bne t2, t3, fail
    sh zero, 26(s0)
    lw t2, 24(s0)
    li t3, 0xef
    bne t2, t3, fail
    /* The address and stored value may occupy the same host register. */
    sw s0, 28(s0)
    lw t2, 28(s0)
    bne t2, s0, fail
    /* Exercise post-increment store fusion and preserve the updated base. */
    mv t0, s0
    sw t1, 32(t0)
    addi t0, t0, 4
    sw t1, 32(t0)
    lw t2, 32(s0)
    lw t3, 36(s0)
    bne t2, t1, fail
    bne t3, t1, fail
#ifdef __riscv_compressed
    .option rvc
    mv sp, s0
    c.swsp t1, 16(sp)
    c.lwsp t2, 16(sp)
    bne t1, t2, fail
    mv a0, s0
    mv a1, t1
    c.sw a1, 20(a0)
    c.lw a0, 20(a0)
    bne a0, t1, fail
    /* Alternate dirty zero/nonzero operands through compressed branches. */
    andi a2, s1, 1
    c.beqz a2, 1f
    c.bnez a2, 2f
    j fail
1:
    bnez a2, fail
    j 3f
2:
    beqz a2, fail
3:
    .option norvc
#endif
    addi s1, s1, -1
    bnez s1, loop
    li a0, 1
    la a1, passed
    li a2, 22
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
    .ascii "JIT memory address OK\n"
    .bss
    .balign 16
buffer:
    .space 4096
