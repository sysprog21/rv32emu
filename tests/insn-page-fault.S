/*
 * A system program loaded directly from an ELF file runs with no operating
 * system, so an exception it takes without a trap vector has no handler. Turn
 * on paging with an empty root page table: the next instruction fetch faults,
 * and the emulator must stop with a failure status rather than retry the fetch
 * forever.
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
    nop                     /* never fetched */
    li   a7, 93             /* exit(0) would mean the fault was skipped */
    li   a0, 0
    ecall

    /* In .data rather than .bss, so the table's bytes are in the file */
    .data
    .balign 4096
root:
    .space 4096
