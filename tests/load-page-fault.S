/*
 * Inline page-fault handling can resume elsewhere, including another task.
 * A canceled load must leave the resumed register state intact, rather than
 * commit the memory helper's zero placeholder. With no trap vector, a directly
 * loaded system ELF skips each faulting load, exercising that same path.
 */
    .text
    .global _start
_start:
    la   t0, root
    srli t0, t0, 12
    li   t1, 0x80000000
    or   t0, t0, t1
    csrw satp, t0
    sfence.vma

    li   s0, 0x400000       /* first byte of an unmapped megapage */
    li   s1, 0x5a5a5a5a
    li   t2, 0

    mv   a0, s1
    lb   a0, 0(s0)
    bne  a0, s1, fail
    addi t2, t2, 1
    lbu  a0, 0(s0)
    bne  a0, s1, fail
    addi t2, t2, 1
    lh   a0, 0(s0)
    bne  a0, s1, fail
    addi t2, t2, 1
    lhu  a0, 0(s0)
    bne  a0, s1, fail
    addi t2, t2, 1
    lw   a0, 0(s0)
    bne  a0, s1, fail
    addi t2, t2, 1

#ifdef __riscv_compressed
    c.lw a0, 0(s0)
    bne  a0, s1, fail
    mv   s2, sp
    mv   sp, s0
    c.lwsp a0, 0(sp)
    mv   sp, s2
    bne  a0, s1, fail
#endif

#ifdef __riscv_flen
    fmv.w.x fa0, s1
    flw  fa0, 0(s0)
    fmv.x.w a0, fa0
    bne  a0, s1, fail
#ifdef __riscv_compressed
    c.flw fa0, 0(s0)
    fmv.x.w a0, fa0
    bne  a0, s1, fail
    mv   s2, sp
    mv   sp, s0
    c.flwsp fa0, 0(sp)
    mv   sp, s2
    fmv.x.w a0, fa0
    bne  a0, s1, fail
#endif
#endif

    /* Base/destination aliasing must also preserve the canceled load. */
    mv   a0, s0
    lw   a0, 0(a0)
    bne  a0, s0, fail
    li   t0, 5
    bne  t2, t0, fail

    li   a7, 64
    li   a0, 1
    la   a1, msg
    la   a2, msg_end
    sub  a2, a2, a1
    ecall
    li   a7, 93
    li   a0, 0
    ecall
fail:
    li   a7, 93
    li   a0, 1
    ecall

    .data
msg:
    .ascii "canceled loads preserve registers OK\n"
msg_end:
    .balign 4096
root:
    .word 0xcf              /* identity-map only the first 4 MiB */
    .space 4092
