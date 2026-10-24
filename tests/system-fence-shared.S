/* FENCE.I keeps kernel code fetched through global mappings unless a store
 * reached one of its pages. Run a global code page until it is compiled, then
 * rewrite its first instruction and fence: through the global mapping from a
 * compiled loop, through an identity alias of the same physical page from a
 * compiled loop, once more through the alias from cold code, and with an
 * atomic swap. Each fence must expose the new instruction. A store to the
 * page before it ever ran leaves a write tag that the page turning into code
 * has to revoke, and a load from it once it holds code must not create one.
 */
#ifndef FENCE_CALLS
#define FENCE_CALLS 20000
#endif
#define CODE_VA 0x420000
#define LI_A0(imm) (((imm) << 20) | (10 << 7) | 0x13)
.option norvc
.option norelax
.section .text
.globl _start
_start:
    la a0, root
    la a1, leaves
    la a2, code_page
    jal ra, build_space
    la a0, root
    srli a0, a0, 12
    li t0, 0x80000000
    or a0, a0, t0
    csrw satp, a0
    sfence.vma

    /* Write to the page before it holds code, leaving a write tag. */
    li t3, CODE_VA
    sw zero, 256(t3)

    li a1, 1
    jal ra, run_code

    /* Rewrite through the global mapping from a compiled loop. */
    li a0, CODE_VA
    li a2, LI_A0(3)
    jal ra, store_hot
    fence.i
    li a1, 3
    jal ra, run_code

    /* Rewrite through the identity alias from a compiled loop. */
    la a0, code_page
    li a2, LI_A0(5)
    jal ra, store_hot
    fence.i
    li a1, 5
    jal ra, run_code

    /* Rewrite through the alias once, from cold code. */
    la t3, code_page
    li t4, LI_A0(7)
    sw t4, 0(t3)
    fence.i
    li a1, 7
    jal ra, run_code

    /* Rewrite through the global mapping with an atomic swap. */
    li t3, CODE_VA
    li t4, LI_A0(9)
    amoswap.w zero, t4, (t3)
    fence.i
    li a1, 9
    jal ra, run_code

    la a0, success
    j print

/* Call CODE_VA FENCE_CALLS times; each call must return a1. */
run_code:
    mv s0, ra
    li s1, FENCE_CALLS
1:
    li t3, CODE_VA
    jalr ra, 0(t3)
    bne a0, a1, fail
    addi s1, s1, -1
    bnez s1, 1b
    mv ra, s0
    ret

/* Store a2 to 0(a0) from compiled code. Warm store_loop up on a scratch
 * page first: FENCE.I retires this code, which no global mapping reaches.
 */
store_hot:
    mv s0, ra
    mv s2, a0
    mv s3, a2
    li s1, 64
1:
    la a0, scratch
    li a3, 4
    jal ra, store_loop
    addi s1, s1, -1
    bnez s1, 1b
    /* Drop the TLB entries, so that the compiled load refills them. */
    sfence.vma
    mv a0, s2
    mv a2, s3
    li a3, FENCE_CALLS
    jal ra, store_loop
    mv ra, s0
    ret

/* Store a2 to 0(a0) a3 times. The load first fills the dTLB entry from a
 * read, through a dirty mapping, which must not hand out a write tag either.
 */
store_loop:
    lw t0, 0(a0)
1:
    sw a2, 0(a0)
    addi a3, a3, -1
    bnez a3, 1b
    ret

/* Root a0 maps this kernel and the UART by identity, and CODE_VA globally to
 * page a2 through the leaf table a1.
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
success: .asciz "SYSTEM fence shared OK\n"
failure: .asciz "SYSTEM fence shared FAIL\n"

.balign 4096
code_page:
    li a0, 1
    ret
.balign 4096
scratch:
    .space 4096
leaves:
    .space 4096
root:
    .space 4096
