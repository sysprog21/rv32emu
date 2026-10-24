/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* Flat S-mode kernel, run with -m, checking misaligned accesses that span two
 * virtual pages. The pages map to physical pages with a gap between them, so
 * each part must be translated on its own. A store whose second page is not
 * mapped faults without writing its first part. Each case loops 2000 times to
 * reach native code.
 */

.option norvc
.option norelax
.equ ITER, 2000
.equ PAGE_A, 0x400000
.equ PAGE_C, 0x401000

.section .text
.globl _start
_start:
    la t0, trap
    csrw stvec, t0
    la t0, root
    li t1, 0xcf /* VA 0-4 MiB: physical 0-4 MiB */
    sw t1, 0(t0)
    li t1, 0x3d0000cf /* the UART at 0xf4000000 */
    li t2, 976 * 4
    add t2, t2, t0
    sw t1, 0(t2)
    la t1, leaves
    srli t1, t1, 12
    slli t1, t1, 10
    ori t1, t1, 1
    sw t1, 4(t0)
    la t0, leaves
    la t1, page_a
    srli t1, t1, 12
    slli t1, t1, 10
    ori t1, t1, 0xc7 /* R W */
    sw t1, 0(t0)
    la t1, page_c
    srli t1, t1, 12
    slli t1, t1, 10
    ori t1, t1, 0xc7
    sw t1, 4(t0)
    la t0, root
    srli t0, t0, 12
    li t1, 0x80000000
    or t0, t0, t1
    csrw satp, t0
    sfence.vma

    li s1, 0x11223344
    li a2, ITER
    li a3, PAGE_A + 0xffe /* two bytes before the end of page_a */
    la a4, page_a + 0xffe
    la a5, page_c
cross_loop:
    addi s1, s1, 1
    sw s1, 0(a3) /* bytes 0-1 to page_a, 2-3 to page_c */
    lw t0, 0(a3)
    bne t0, s1, fail
    lhu t0, 0(a4) /* through the identity map */
    slli t1, s1, 16
    srli t1, t1, 16
    bne t0, t1, fail
    lhu t0, 0(a5)
    srli t1, s1, 16
    bne t0, t1, fail
    sh s1, 1(a3) /* one byte on each page */
    lh t0, 1(a3)
    slli t1, s1, 16
    srai t1, t1, 16
    bne t0, t1, fail
    addi a2, a2, -1
    bnez a2, cross_loop

    /* A store into an unmapped second page faults and writes nothing. */
    li s9, 0
    li a2, ITER
    li a3, PAGE_C + 0xffe
    la a6, page_c + 0xffc
    li t2, -1
    sw t2, 0(a6)
fault_loop:
    sw zero, 0(a3) /* faults; the handler skips it */
    lw t0, 0(a6)
    bne t0, t2, fail
    addi a2, a2, -1
    bnez a2, fault_loop
    li t0, ITER
    bne s9, t0, fail

    la a0, success
    j print

    .balign 4
trap:
    csrr t0, scause
    li t1, 15
    bne t0, t1, fail
    addi s9, s9, 1
    csrr t1, sepc
    addi t1, t1, 4
    csrw sepc, t1
    sret

#include "system-exit.inc"

.section .data
    .balign 4096
root:
    .space 4096
leaves:
    .space 4096
page_a:
    .space 4096
gap:
    .space 4096
page_c:
    .space 4096
success:
    .string "SYSTEM misaligned page crossing OK\n"
failure:
    .string "SYSTEM misaligned page crossing FAILED\n"
