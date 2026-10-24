/* Code decoded through a translation the page tables no longer hold must not
 * survive the fence that publishes the new one. Map a virtual code page to
 * page A and run code there, so the iTLB holds that translation. Retire the
 * address space's code by remapping another code page and fencing only that
 * page, which leaves A's translation cached. Point the first entry at page B
 * without a fence, then call code on that page not yet decoded: its fetch may
 * still use A. After SFENCE.VMA, that code must come from B, in every engine.
 */
#ifndef STALE_FETCH_CALLS
#define STALE_FETCH_CALLS 100000
#endif
/* Clear of this kernel's iTLB slots, which would otherwise evict the stale
 * translation between calls.
 */
#define CODE_VA 0x420000
#define OTHER_VA (CODE_VA + 0x1000)
.option norvc
.option norelax
.section .text
.globl _start
_start:
    la t0, root
    li t1, 0xcf               /* VA 0-4 MiB: this kernel, identity */
    sw t1, 0(t0)
    la t1, leaves
    srli t1, t1, 12
    slli t1, t1, 10
    ori t1, t1, 1             /* VA 4-8 MiB: the leaf table */
    sw t1, 4(t0)
    li t1, 0x3d0000cf         /* the UART */
    li t2, 3904
    add t2, t2, t0
    sw t1, 0(t2)
    la t0, page_a
    jal ra, map_code
    la t0, page_c
    li t2, OTHER_VA
    jal ra, map_page
    la t0, root
    srli t0, t0, 12
    li t1, 0x80000000
    or t0, t0, t1
    csrw satp, t0
    sfence.vma

    /* Fill the iTLB with A's translation. */
    li t3, CODE_VA
    jalr ra, 0(t3)
    li t0, 1
    bne a0, t0, fail

    /* Retire this space's code, keeping A's translation in the iTLB. */
    li t3, OTHER_VA
    jalr ra, 0(t3)
    li t0, 3
    bne a0, t0, fail
    la t0, page_b
    li t2, OTHER_VA
    jal ra, map_page
    li t2, OTHER_VA
    sfence.vma t2

    /* Remap to B without a fence, and decode code on the page meanwhile.
     * Before the fence either translation may be used, so do not check it.
     */
    la t0, page_b
    jal ra, map_code
    li s1, STALE_FETCH_CALLS
1:
    li t3, CODE_VA + 0x100
    jalr ra, 0(t3)
    addi s1, s1, -1
    bnez s1, 1b

    sfence.vma
    li s1, STALE_FETCH_CALLS
2:
    li t3, CODE_VA + 0x100
    jalr ra, 0(t3)
    li t0, 2
    bne a0, t0, fail
    addi s1, s1, -1
    bnez s1, 2b
    la a0, success
    j print

/* Map the code page to the physical page at t0. */
map_code:
    li t2, CODE_VA
/* Map the page at t2 to the physical page at t0. */
map_page:
    srli t0, t0, 12
    slli t0, t0, 10
    ori t0, t0, 0xcf
    srli t2, t2, 12
    andi t2, t2, 1023
    slli t2, t2, 2
    la t1, leaves
    add t1, t1, t2
    sw t0, 0(t1)
    ret

#include "system-exit.inc"
success: .asciz "SYSTEM fence stale fetch OK\n"
failure: .asciz "SYSTEM fence stale fetch FAIL\n"

.balign 4096
page_a:
    li a0, 1
    ret
.org page_a + 0x100
    li a0, 1
    ret
.balign 4096
page_b:
    li a0, 2
    ret
.org page_b + 0x100
    li a0, 2
    ret
.balign 4096
page_c:
    li a0, 3
    ret
.balign 4096
leaves:
    .space 4096
root:
    .space 4096
