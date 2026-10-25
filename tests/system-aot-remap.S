/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* Native code for a page must not run once its mapping loses execute
 * permission. The artifact still lists the address, so the dispatcher fetches
 * it; that fetch faults, and the handler remaps the address to other code and
 * retries. The dispatcher must bind the page now fetched, not the old one.
 */
.option norvc
.option norelax
.section .text
.globl _start
_start:
    csrw sstatus, zero
    la t0, trap
    csrw stvec, t0
    li s5, 0
    la t0, root
    la t1, leaf
    srli t1, t1, 12
    slli t1, t1, 10
    ori t1, t1, 1
    li t3, 0xcf
    sw t3, 0(t0)
    sw t1, 1024(t0)
    li t3, 0x3d0000cf
    li t5, 3904
    add t5, t5, t0
    sw t3, 0(t5)
    /* VA 0x40000000 maps "old" (PA 0x10000) with RWX. */
    la s4, leaf
    li t0, 0x40cf
    sw t0, 0(s4)
    la s0, root
    srli s0, s0, 12
    li t0, 0x80000000
    or s0, s0, t0
    li s2, 0x40000000
    csrw satp, s0
    sfence.vma
    li s6, 2000
1:
    jalr ra, s2, 0
    li t0, 1
    bne a0, t0, fail
    addi s6, s6, -1
    bnez s6, 1b

    /* Drop X but keep the old bytes mapped. The fault handler maps "new". */
    li s5, 1
    li t0, 0x40c7
    sw t0, 0(s4)
    sfence.vma s2, zero
    li s9, 0
    jalr ra, s2, 0
    li t0, 2
    bne a0, t0, fail
    li t0, 1
    bne s9, t0, fail
    la a0, ok
    j print
trap:
    csrr t0, scause
    li t1, 12
    bne t0, t1, fail
    csrr t0, stval
    bne t0, s2, fail
    addi s9, s9, 1
    li t0, 0x44cf
    sw t0, 0(s4)
    sfence.vma s2, zero
    sret
fail:
    li t1, 0xf4000000
    addi t0, s5, '0'
    sb t0, 0(t1)
    la a0, bad
print:
    li t1, 0xf4000000
2:
    lbu t0, 0(a0)
    beqz t0, shutdown
    sb t0, 0(t1)
    addi a0, a0, 1
    j 2b
shutdown:
    li a0, 0
    li a1, 0
    li a6, 0
    li a7, 0x53525354
    ecall
    j .
.org 0x10000
old:
    li a0, 1
    ret
.org 0x11000
new:
    li a0, 2
    ret
.section .rodata
ok: .asciz "SYSTEM AOT remap OK\n"
bad: .asciz "SYSTEM AOT remap FAIL\n"
.section .bss
.balign 4096
leaf: .space 4096
root: .space 4096
