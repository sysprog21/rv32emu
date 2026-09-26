/*
 * A system program loaded directly from an ELF file has no trap vector, so a
 * misaligned access is emulated with smaller accesses, and a page fault is
 * handled by skipping the faulting instruction. Map only the first 4 MiB, then
 * load a word from its last byte: the emulated access faults partway, which
 * must skip that load, and only that load, without writing its destination.
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

    li   s0, 0x3fffff       /* last byte of the mapped megapage */
    li   a0, 0x5a5a5a5a     /* must survive the skipped load */
    li   t2, 0
    lw   a0, 0(s0)          /* misaligned, crosses into an unmapped page */
    addi t2, t2, 1          /* each of these must run exactly once */
    addi t2, t2, 1
    addi t2, t2, 1

    li   t0, 3
    bne  t2, t0, fail
    li   t0, 0x5a5a5a5a
    bne  a0, t0, fail
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
    .ascii "misaligned page fault OK\n"
msg_end:
    /* Root page table: entry 0 maps VA 0-4 MiB to PA 0-4 MiB as a readable,
     * writable, executable megapage; the rest stay invalid. In .data so that
     * its bytes are in the file.
     */
    .balign 4096
root:
    .word 0xcf              /* PPN 0 | D | A | X | W | R | V */
    .space 4092
