/*
 * A system program loaded directly from an ELF file has no trap vector, so a
 * page fault is handled by skipping the faulting instruction. An AMO whose
 * read succeeds but whose write faults, as on a copy-on-write page, must
 * commit nothing: neither rd nor memory may change. Map a read-only alias of
 * the first 4 MiB at 4 MiB and run AMOs through it.
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

    la   s0, word
    li   t0, 0x400000
    add  s1, s0, t0         /* read-only alias of word */

    /* rd distinct from rs2 */
    li   a0, 0x11111111
    li   a1, 5
    amoadd.w a0, a1, (s1)
    li   t0, 0x11111111
    bne  a0, t0, fail
    li   t0, 5
    bne  a1, t0, fail

    /* rd aliasing rs2, as compilers emit for fetch-and-add */
    li   a2, 7
    amoadd.w a2, a2, (s1)
    li   t0, 7
    bne  a2, t0, fail

    li   a3, 0x22222222
    amoswap.w a3, a3, (s1)
    li   t0, 0x22222222
    bne  a3, t0, fail

    li   a4, -1
    amomaxu.w a4, a4, (s1)
    li   t0, -1
    bne  a4, t0, fail

    /* memory is unchanged, and the writable mapping still works */
    lw   t1, 0(s0)
    li   t0, 1000
    bne  t1, t0, fail
    li   a5, 3
    amoadd.w a5, a5, (s0)
    li   t0, 1000
    bne  a5, t0, fail
    lw   t1, 0(s0)
    li   t0, 1003
    bne  t1, t0, fail

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
    .ascii "AMO page fault OK\n"
msg_end:
    .balign 4
word:
    .word 1000
    /* Root page table: entry 0 maps VA 0-4 MiB to PA 0-4 MiB as a readable,
     * writable, executable megapage; entry 1 maps VA 4-8 MiB to the same
     * physical range, readable only. In .data so that its bytes are in the
     * file.
     */
    .balign 4096
root:
    .word 0xcf              /* PPN 0 | D | A | X | W | R | V */
    .word 0x43              /* PPN 0 | A | R | V */
    .space 4088
