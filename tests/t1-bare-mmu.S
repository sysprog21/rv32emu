/* Exercise the SYSTEM_MMIO T1 load/store path from a raw supervisor image. */
#ifndef TEST_RAM_END
#error "TEST_RAM_END must match the configured guest RAM end address"
#endif
#define TEST_LAST_RAM_WORD (TEST_RAM_END - 4)
#define TEST_LAST_RAM_LOAD_OFFSET (TEST_RAM_END - 0x8000 - 20)
    .option norvc
    .text
    .global _start
_start:
    li   s0, 0x8000
    li   s1, 0x89abcdef
    li   t0, 0x13579bdf
    sw   t0, 16(s0)
    li   t0, 0x13579bdf
    li   t1, TEST_LAST_RAM_WORD
    sw   t0, 0(t1)

    li   s5, 0xf4000000
    li   t0, 2
    sb   t0, 1(s5)  /* enable the UART THRE interrupt */

    la   t0, trap_handler
    csrw stvec, t0
    li   s9, 0x8020
    sw   zero, 0(s9)

    li   s10, 4
    li   s11, 0x8001
    la   s8, misaligned_load
    /* Keep the access block hot enough to run in native T1. */
    li   s2, 100000

access_loop:
    addi s2, s2, -1
    sb   s1, 0(s0)
    lbu  t0, 0(s0)
    li   t1, 0xef
    bne  t0, t1, fail
    lb   t0, 0(s0)
    li   t1, -17
    bne  t0, t1, fail

    sh   s1, 2(s0)
    lhu  t0, 2(s0)
    li   t1, 0xcdef
    bne  t0, t1, fail
    lh   t0, 2(s0)
    li   t1, -12817
    bne  t0, t1, fail

    sw   s1, 4(s0)
    lw   t0, 4(s0)
    bne  t0, s1, fail
    lw   x0, 4(s0)

    /* The final aligned word in RAM remains accessible. */
    seqz t0, s2
    neg  t0, t0
    li   t1, TEST_LAST_RAM_LOAD_OFFSET
    and  t0, t0, t1
    add  t2, s0, t0
    addi t2, t2, 16
    lw   t3, 0(t2)
    li   t4, 0x13579bdf
    bne  t3, t4, fail

    /* This one store reaches UART THR only on the last loop iteration. */
    seqz t0, s2
    neg  t0, t0
    li   t1, 0xf3ff8000
    and  t0, t0, t1
    add  t2, s0, t0
    li   t3, '!'
    sb   t3, 0(t2)
    addi t2, t2, 2
    lbu  x0, 0(t2)

    /* Only the last iteration makes this word load misaligned. */
    seqz t0, s2
    add  t2, s0, t0
misaligned_load:
    lw   t3, 0(t2)
    bnez s2, access_loop

    lw   t0, 0(s9)
    li   t1, 1
    bne  t0, t1, fail
    lbu  t0, 2(s5)
    andi t0, t0, 1
    beqz t0, fail

    li   s10, 0
    li   s0, 100000
    li   s1, 0x8000
    li   s2, 0
    j    mode_loop

/* Map code at VA 0 and data at VA 0x8000; leave VA 0x6000 unmapped. */
page_setup:
    li   t0, 0x10000
    li   t1, 0x4401
    sw   t1, 0(t0)
    li   t0, 0x11000
    li   t1, 0x4b
    sw   t1, 0(t0)
    li   t1, 0x20c7
    sw   t1, 32(t0)

    sw   zero, 0(s9)
    li   s10, 13
    li   s11, 0x6000
    la   s8, paged_load_fault
    li   s0, 100000
    li   s1, 0x8000
    li   s2, 0xffffe000
    li   t0, 0x80000010
    csrw satp, t0
    sfence.vma x0, x0
    j    paged_mode_loop

/* Keep the bare and paged loops at distinct PCs for T1 hotness checks. */
mode_loop:
    addi s0, s0, -1
    seqz t0, s0
    neg  t0, t0
    and  t0, t0, s2
    add  t1, s1, t0
    lbu  x0, 0(t1)
    bnez s0, mode_loop

mode_loop_done:
    beqz s10, page_setup
    j    finished

/* Warm this new PC under Sv32 before the final unmapped x0 load. */
paged_mode_loop:
    addi s0, s0, -1
    seqz t0, s0
    neg  t0, t0
    and  t0, t0, s2
    add  t1, s1, t0
paged_load_fault:
    lbu  x0, 0(t1)
    bnez s0, paged_mode_loop

paged_mode_loop_done:
    lw   t0, 0(s9)
    li   t1, 1
    bne  t0, t1, fail
    csrr t0, satp
    li   t1, 0x80000010
    bne  t0, t1, fail
    csrw satp, zero
    sfence.vma x0, x0
    li   s0, 100000
    li   s2, 0
    j    mode_loop

finished:
    lw   t0, 0(s9)
    li   t1, 1
    bne  t0, t1, fail
    li   a0, 1
    la   a1, passed
    li   a2, 20
    li   a7, 64
    ecall
    li   a0, 0
    j    exit

trap_handler:
    csrr t0, scause
    bne  t0, s10, fail
    csrr t0, stval
    bne  t0, s11, fail
    csrr t0, sepc
    bne  t0, s8, fail
    lw   t0, 0(s9)
    addi t0, t0, 1
    sw   t0, 0(s9)
    csrr t0, sepc
    addi t0, t0, 4
    csrw sepc, t0
    sret

fail:
    li   a0, 1
    j    exit
exit:
    li   a7, 93
    ecall
1:
    j    1b

    .section .rodata
passed:
    .ascii "T1 bare MMU path OK\n"
passed_end:
