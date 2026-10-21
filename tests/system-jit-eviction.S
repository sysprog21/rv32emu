/* Native metadata outlives decoded IR. Evict IR with 8193 unique blocks,
 * then verify native reuse, selective code remapping, and FENCE.I retirement. */
.option norvc
.option norelax
.section .text
.globl _start
_start:
    li s0, 0x10000
    li s1, 0x11000
    la t0, code_template
    mv t1, s0
    li t2, 4
1:
    lw t3, 0(t0)
    sw t3, 0(t1)
    li t4, 4096
    add t4, t1, t4
    sw t3, 0(t4)
    addi t0, t0, 4
    addi t1, t1, 4
    addi t2, t2, -1
    bnez t2, 1b
    li t0, 0x00968693       /* addi a3, a3, 9 */
    sw t0, 0(s1)
    li t0, 0x20000
    li t1, 8193
    li t2, 0x0040006f       /* jal zero, +4: distinct basic blocks */
2:
    sw t2, 0(t0)
    addi t0, t0, 4
    addi t1, t1, -1
    bnez t1, 2b
    li t2, 0x00008067       /* ret */
    sw t2, 0(t0)
    la s2, root
    li t0, 0xcf
    sw t0, 0(s2)
    la s4, leaves
    srli t0, s4, 12
    slli t0, t0, 10
    ori t0, t0, 1
    sw t0, 4(s2)
    li t0, 0x40cf
    sw t0, 0(s4)
    li t0, 0x3d0000cf
    li t1, 3904
    add t1, s2, t1
    sw t0, 0(t1)
    srli t0, s2, 12
    li t1, 0x80000000
    or t0, t0, t1
    csrw satp, t0
    sfence.vma
    fence.i
    li s3, 0x400000
    li a3, 0
    li a4, 20000
    jalr ra, s3, 0
    li t0, 140000
    bne a3, t0, fail
    jal ra, evict
    li a3, 0
    li a4, 20000
    jalr ra, s3, 0
    li t0, 140000
    bne a3, t0, fail
    jal ra, evict
    li t0, 0x44cf
    sw t0, 0(s4)
    sfence.vma s3
    li a3, 0
    li a4, 20000
    jalr ra, s3, 0
    li t0, 180000
    bne a3, t0, fail
    jal ra, evict
    li t0, 0x00b68693       /* addi a3, a3, 11 */
    sw t0, 0(s1)
    fence.i
    li a3, 0
    li a4, 20000
    jalr ra, s3, 0
    li t0, 220000
    bne a3, t0, fail
    la a0, success
    j print
evict:
    li t0, 0x20000
    jr t0
#include "system-exit.inc"
.balign 4
code_template:
    addi a3, a3, 7
    addi a4, a4, -1
    bnez a4, code_template
    ret
success: .asciz "SYSTEM JIT eviction OK\n"
failure: .asciz "SYSTEM JIT eviction FAIL\n"
.balign 4096
root: .space 4096
leaves: .space 4096
