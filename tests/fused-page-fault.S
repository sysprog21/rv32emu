/*
 * A system program loaded directly from an ELF file has no trap vector, so a
 * page fault is handled by skipping the faulting instruction. Fused records
 * run several instructions in one handler; a faulting member must end the
 * record there, writing nothing for it and leaving every later member to run
 * exactly once from the resumed PC. Map only the first 4 MiB, then fault the
 * first member of each fused load record that system builds form, and a later
 * member of an LW group.
 */
    .text
    .global _start
_start:
    la   t0, root
    srli t0, t0, 12
    li   t1, 0x80000000     /* Sv32 */
    or   t0, t0, t1
    csrw satp, t0
    sfence.vma

    li   s0, 0x400000       /* first unmapped address */
    la   s1, chase

    /* fuse4: LW; LW. The faulting load keeps a0. A second run of the
     * pointer-chasing load would leave a1 = *chase_target = 0.
     */
    li   a0, 0x5a5a5a5a
    mv   a1, s1
    j    1f
1:
    lw   a0, 0(s0)
    lw   a1, 0(a1)
    li   t0, 0x5a5a5a5a
    bne  a0, t0, fail
    la   t0, chase_target
    bne  a1, t0, fail

    /* fuse4 with a later member faulting: the first load retires and the
     * faulting one keeps a6.
     */
    li   a6, 0x2d2d2d2d
    li   a7, 0
    j    1f
1:
    lw   a7, 0(s1)
    lw   a6, 0(s0)
    la   t0, chase_target
    bne  a7, t0, fail
    li   t0, 0x2d2d2d2d
    bne  a6, t0, fail

    /* fuse9: LUI; LW. The LUI retires; the faulting load keeps a2. */
    li   a2, 0x3c3c3c3c
    j    1f
1:
    lui  a3, 0x400
    lw   a2, 0(a3)
    li   t0, 0x400000
    bne  a3, t0, fail
    li   t0, 0x3c3c3c3c
    bne  a2, t0, fail

    /* fuse11: LW; ADDI. Only the ADDI after the skipped load advances a4. */
    li   a4, 0x400000
    li   a5, 0x1e1e1e1e
    j    1f
1:
    lw   a5, 0(a4)
    addi a4, a4, 4
    li   t0, 0x400004
    bne  a4, t0, fail
    li   t0, 0x1e1e1e1e
    bne  a5, t0, fail

    li   a7, 64             /* write(1, msg, len) */
    li   a0, 1
    la   a1, msg
    la   a2, msg_end
    sub  a2, a2, a1
    ecall
    li   a7, 93             /* exit(0) */
    li   a0, 0
    ecall
fail:
    li   a7, 93             /* exit(1) */
    li   a0, 1
    ecall

    .data
msg:
    .ascii "fused page fault OK\n"
msg_end:
    .balign 4
chase:
    .word chase_target
chase_target:
    .word 0
    /* Root page table: entry 0 maps VA 0-4 MiB to PA 0-4 MiB as a readable,
     * writable, executable megapage; the rest stay invalid. In .data so that
     * its bytes are in the file.
     */
    .balign 4096
root:
    .word 0xcf              /* PPN 0 | D | A | X | W | R | V */
    .space 4092
