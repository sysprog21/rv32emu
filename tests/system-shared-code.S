/* Kernel code fetched through global mappings is shared by every address
 * space, which is valid only while each space maps it alike. Map one global
 * code page to different physical pages in two address spaces and switch
 * between them: rv32emu flushes translations whenever SATP changes, so the
 * code each space runs, hot and compiled, must be its own.
 */
#ifndef SHARED_CALLS
#define SHARED_CALLS 100000
#endif
#define CODE_VA 0x420000
.option norvc
.option norelax
.section .text
.globl _start
_start:
    la a0, root_a
    la a1, leaves_a
    la a2, page_a
    jal ra, build_space
    la a0, root_b
    la a1, leaves_b
    la a2, page_b
    jal ra, build_space

    li s2, 4                  /* alternate the spaces four times */
    li s3, 0                  /* 0: space A, 1: space B */
1:
    la a0, root_a
    beqz s3, 2f
    la a0, root_b
2:
    srli a0, a0, 12
    li t0, 0x80000000
    or a0, a0, t0
    csrw satp, a0
    /* Space A runs page A (1); space B runs page B (2). */
    addi s4, s3, 1
    li s1, SHARED_CALLS
3:
    li t3, CODE_VA
    jalr ra, 0(t3)
    bne a0, s4, fail
    addi s1, s1, -1
    bnez s1, 3b
    xori s3, s3, 1
    addi s2, s2, -1
    bnez s2, 1b
    la a0, success
    j print

/* Root a0 maps this kernel, the UART, and CODE_VA globally to page a2
 * through the leaf table a1.
 */
build_space:
    li t1, 0xcf               /* VA 0-4 MiB: this kernel, identity */
    sw t1, 0(a0)
    srli t1, a1, 12
    slli t1, t1, 10
    ori t1, t1, 1             /* VA 4-8 MiB: the leaf table */
    sw t1, 4(a0)
    li t1, 0x3d0000cf         /* the UART */
    li t2, 3904
    add t2, t2, a0
    sw t1, 0(t2)
    srli t1, a2, 12
    slli t1, t1, 10
    ori t1, t1, 0xef          /* global RWX */
    li t2, ((CODE_VA >> 12) & 1023) * 4
    add t2, t2, a1
    sw t1, 0(t2)
    ret

#include "system-exit.inc"
success: .asciz "SYSTEM shared code OK\n"
failure: .asciz "SYSTEM shared code FAIL\n"

.balign 4096
page_a:
    li a0, 1
    ret
.balign 4096
page_b:
    li a0, 2
    ret
.balign 4096
leaves_a:
    .space 4096
leaves_b:
    .space 4096
root_a:
    .space 4096
root_b:
    .space 4096
