/* Decode faults must wait for the preceding instructions to retire. A
 * straddling instruction checks execute permission on both virtual pages.
 */
#ifndef PHYS_BASE
#define PHYS_BASE 0
#endif
#ifndef UART_PA
#define UART_PA 0xf4000000
#endif
.option norvc
.option norelax
.text
.globl _start
_start:
    la t0, handler
    csrw stvec, t0
    li s7, 0
    li s2, 0
    li s3, 2
    la s4, illegal_prefix
    la s5, after_prefix
    li s6, 1
    addi s2, s2, 1
illegal_prefix:
    .word 0xffffffff
after_prefix:
    bne s2, s6, fail
    la s4, illegal_first
    la s5, after_first
    j illegal_first
illegal_first:
    .word 0xffffffff
after_first:
    bne s2, s6, fail

    la t0, root
    li t1, (PHYS_BASE >> 2) | 0xcf
    sw t1, 0(t0)
    li t2, (PHYS_BASE >> 22) * 4
    add t2, t0, t2
    sw t1, 0(t2)
    la t1, leaves
    srli t1, t1, 2
    ori t1, t1, 1
    sw t1, 1024(t0)
    li t1, (UART_PA >> 2) | 0xcf
    li t2, 3904
    add t2, t0, t2
    sw t1, 0(t2)
    la t0, leaves
    li t1, (PHYS_BASE >> 2) | 0x5c4b          /* S executable at PA 0x17000 */
    sw t1, 28(t0)
    li t1, (PHYS_BASE >> 2) | 0x605b          /* U executable at PA 0x18000 */
    sw t1, 32(t0)
    la t0, root
    srli t0, t0, 12
    li t1, 0x80000000
    or t0, t0, t1
    csrw satp, t0
    sfence.vma
    li s2, 0
    li s3, 12
    li s4, 0x40007ffe
    la s5, after_straddle
    li s6, 1
    li t0, 0x40007ffc
    jr t0
after_straddle:
    bne s2, s6, fail
    /* Warm a straddling jump without ever starting a block in its upper
     * page, then revoke that page and fence only its VA.
     */
    la t0, leaves
    li t1, (PHYS_BASE >> 2) | 0x604b
    sw t1, 32(t0)
    li t0, 0x40008000
    sfence.vma t0
    li s1, 10000
    li s7, 0
    la s5, after_warm
warm:
    li s2, 0
    li t0, 0x40007ffc
    jr t0
after_warm:
    bne s2, s6, fail
    bnez s7, fail
    addi s1, s1, -1
    bnez s1, warm
    la t0, leaves
    li t1, (PHYS_BASE >> 2) | 0x605b
    sw t1, 32(t0)
    li t0, 0x40008000
    sfence.vma t0
    li s2, 0
    la s5, after_revoke
    li t0, 0x40007ffc
    jr t0
after_revoke:
    bne s2, s6, fail
    li t0, 1
    bne s7, t0, fail
    la a0, success
    j print
handler:
    addi s7, s7, 1
    csrr t0, scause
    bne t0, s3, fail
    csrr t0, sepc
    bne t0, s4, fail
    bne s2, s6, fail
    csrr t0, stval
    li t1, 2
    beq s3, t1, 1f
    li t1, 0x40008000
    bne t0, t1, fail
    j 2f
1:
    li t1, -1
    bne t0, t1, fail
2:
    csrw sepc, s5
    sret
#include "system-exit.inc"
success: .asciz "SYSTEM decode trap OK\n"
failure: .asciz "SYSTEM decode trap FAIL\n"
.set root, 0x4000
.set leaves, 0x5000
.org 0x17ffc, 0
.option rvc
    c.addi s2, 1
.option norvc
    jalr zero, s5, 0
