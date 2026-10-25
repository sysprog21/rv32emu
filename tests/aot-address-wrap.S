/* Guarded AOT address folding must leave at a wrapping access with the exact
 * retired prefix and resume it in the interpreter.
 *
 * Each case is its own loop. The loop head block holds eight distinct stack
 * displacements, so its region folds displacements behind wrap guards: seven
 * stores that never wrap, then the access under test. Training runs on
 * ordinary RAM. With an argument, the access under test is the first in its
 * block to wrap: past the top of the RV32 address space from sp = -8, or below
 * zero from sp = 4. Both runs must print the same instruction-count delta.
 */
    .option norelax
    .option norvc
    .text
    .global _start

    .equ ITER, 20000

    /* Seven nonwrapping stores below sp. */
    .macro below
    sw s1, -4(sp)
    sw s1, -8(sp)
    sw s1, -12(sp)
    sw s1, -16(sp)
    sw s1, -20(sp)
    sw s1, -24(sp)
    sw s1, -28(sp)
    .endm

    /* Seven nonwrapping stores above sp. */
    .macro above
    sw s1, 4(sp)
    sw s1, 8(sp)
    sw s1, 12(sp)
    sw s1, 16(sp)
    sw s1, 20(sp)
    sw s1, 24(sp)
    sw s1, 28(sp)
    .endm

    .macro begin label, base
    li s2, ITER
\label:
    addi s1, s1, 1
    mv sp, \base
    .endm

    .macro end label
    addi s2, s2, -1
    bnez s2, \label
    .endm

_start:
    la s0, buffer + 32          /* stores below the top, accesses at +12.. */
    la s5, buffer + 96          /* stores above zero, accesses at -8 */
    lw t0, 0(sp)
    li t1, 1
    beq t0, t1, ready
    li s0, -8
    li s5, 4
ready:
    li s1, 0x01234567
    li s6, 0x89abcdef
    sw s6, 12(s0)               /* cold, so the interpreter wraps it */
    rdinstret s3

    /* Native code first enters through a region rooted in the code above,
     * which does not fold displacements, so warm up before the first case.
     */
    begin warm_loop, s0
    below
    end warm_loop

    /* The eighth member of a fused store group, after seven retired. */
    begin fuse3_loop, s0
    below
    sw s1, 16(sp)
    lw t0, 16(s0)
    bne t0, s1, fail
    lw t0, -28(s0)
    bne t0, s1, fail
    end fuse3_loop

    /* The eighth member of a fused load group. */
    begin fuse4_loop, s0
    below
    lw a0, -4(sp)
    lw a1, -8(sp)
    lw a2, -12(sp)
    lw a3, -16(sp)
    lw a4, -20(sp)
    lw a5, -24(sp)
    lw a6, -28(sp)
    lw a7, 12(sp)
    bne a6, s1, fail
    bne a7, s6, fail
    end fuse4_loop

    /* A load with its base increment. */
    begin fuse11_loop, s0
    below
    lw t0, 12(sp)
    addi sp, sp, 4
    bne t0, s6, fail
    addi t0, s0, 4
    bne sp, t0, fail
    end fuse11_loop

    /* A store with its base increment; the ADDI keeps it out of a group. */
    begin fuse13_loop, s0
    below
    addi t6, t6, 1
    sw s1, 20(sp)
    addi sp, sp, 4
    lw t0, 16(sp)
    bne t0, s1, fail
    end fuse13_loop

    /* A load whose destination is its base. Only sp displacements select
     * folding, so add an eighth.
     */
    begin alias_loop, s0
    below
    sw s1, -32(sp)
    mv t1, sp
    lw t1, 12(t1)
    bne t1, s6, fail
    end alias_loop

    /* A load into x0. */
    begin zero_loop, s0
    below
    lw zero, 12(sp)
    bnez zero, fail
    end zero_loop

    /* Narrow loads extend the bytes they read. */
    begin lb_loop, s0
    below
    lb t0, 12(sp)
    slli t2, s6, 24
    srai t2, t2, 24
    bne t0, t2, fail
    end lb_loop

    begin lbu_loop, s0
    below
    lbu t0, 12(sp)
    andi t2, s6, 255
    bne t0, t2, fail
    end lbu_loop

    begin lh_loop, s0
    below
    lh t0, 12(sp)
    slli t2, s6, 16
    srai t2, t2, 16
    bne t0, t2, fail
    end lh_loop

    begin lhu_loop, s0
    below
    lhu t0, 12(sp)
    slli t2, s6, 16
    srli t2, t2, 16
    bne t0, t2, fail
    end lhu_loop

    /* Narrow stores. */
    begin sb_loop, s0
    below
    sb s1, 24(sp)
    lbu t0, 24(s0)
    andi t2, s1, 255
    bne t0, t2, fail
    end sb_loop

    begin sh_loop, s0
    below
    sh s1, 28(sp)
    lhu t0, 28(s0)
    slli t2, s1, 16
    srli t2, t2, 16
    bne t0, t2, fail
    end sh_loop

#ifdef __riscv_compressed
    /* Compressed stack accesses. */
    begin cswsp_loop, s0
    below
    .option rvc
    c.swsp s1, 16(sp)
    .option norvc
    lw t0, 16(s0)
    bne t0, s1, fail
    end cswsp_loop

    begin clwsp_loop, s0
    below
    .option rvc
    c.lwsp a0, 12(sp)
    .option norvc
    bne a0, s6, fail
    end clwsp_loop
#endif

    /* A negative displacement underflows into the last word of RV32 RAM. */
    begin under_loop, s5
    above
    sw s1, -8(sp)
    lw t0, -8(s5)
    bne t0, s1, fail
    /* A negative displacement that does not wrap. */
    li t2, 8
    sw s1, -4(t2)
    lw t0, -4(t2)
    bne t0, s1, fail
    end under_loop

    rdinstret s4
    sub s4, s4, s3

    la t0, out
    li t1, 28
1:
    srl t2, s4, t1
    andi t2, t2, 15
    li t3, 10
    blt t2, t3, 2f
    addi t2, t2, 'a' - '0' - 10
2:
    addi t2, t2, '0'
    sb t2, 0(t0)
    addi t0, t0, 1
    addi t1, t1, -4
    bgez t1, 1b
    li t2, '\n'
    sb t2, 0(t0)
    li a0, 1
    la a1, out
    li a2, 9
    li a7, 64
    ecall
    li a0, 0
    j exit
fail:
    li a0, 1
exit:
    li a7, 93
    ecall
    .data
    .balign 4
buffer:
    .space 128
out:
    .space 9
