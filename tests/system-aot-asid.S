/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* Two SATPs share a leaf table. A fence may target every ASID or an ASID
 * other than the currently loaded one; a later SATP switch must not resurrect
 * native code or T1 code translated before that fence.
 */
.option norvc
.option norelax
.section .text
.globl _start
_start:
    csrw sstatus, zero
    li s5, 0
    la t0, root_a
    la t1, shared
    srli t1, t1, 12
    slli t1, t1, 10
    ori t1, t1, 1
    li t2, 2
1:
    li t3, 0xcf
    sw t3, 0(t0)
    sw t1, 1024(t0)
    li t3, 0x3d0000cf
    li t5, 3904
    add t5, t5, t0
    sw t3, 0(t5)
    addi t2, t2, -1
    li t3, 4096
    add t0, t0, t3
    bnez t2, 1b
    la s4, shared
    li t0, 0x40cf
    sw t0, 0(s4)
    li t0, 0x48cf
    sw t0, 4(s4)
    la s0, root_a
    srli s0, s0, 12
    li t0, 0x80400000
    or s0, s0, t0
    la s1, root_b
    srli s1, s1, 12
    li t0, 0x80800000
    or s1, s1, t0
    li s2, 0x40000000
    li s3, 0x40001000
    li s7, 1
    csrw satp, s0
    sfence.vma
    jal t4, warm
    csrw satp, s1
    jal t4, warm

    /* rs2=x0 includes B even though the current SATP is A. */
    li s5, 1
    csrw satp, s0
    li t0, 0x44cf
    sw t0, 0(s4)
    sfence.vma s2, zero
    csrw satp, s1
    li s7, 2
    jal t4, warm

    /* A nonzero rs2 names ASID 2, not current ASID 1. */
    li s5, 2
    csrw satp, s0
    li t0, 0x40cf
    sw t0, 0(s4)
    li t6, 2
    sfence.vma s2, t6
    csrw satp, s1
    li s7, 1
    jal t4, warm

    /* The all-address variant must respect the same ASID selection. */
    li s5, 3
    csrw satp, s0
    li t0, 0x44cf
    sw t0, 0(s4)
    sfence.vma zero, t6
    csrw satp, s1
    li s7, 2
    jal t4, warm
    li s5, 4
    csrw satp, s0
    li t0, 0x40cf
    sw t0, 0(s4)
    sfence.vma
    csrw satp, s1
    li s7, 1
    jal t4, warm
    /* A nonzero register containing zero selects ASID 0. */
    li s5, 5
    li t0, 0x7fc00000
    not t0, t0
    and s1, s1, t0
    csrw satp, s1
    jal t4, warm
    csrw satp, s0
    li t0, 0x44cf
    sw t0, 0(s4)
    li t6, 0
    sfence.vma s2, t6
    csrw satp, s1
    li s7, 2
    jal t4, warm
    /* Repeated code remaps must retain the neighboring page's native
     * binding. Only the target page changes; its two versions are collected.
     */
    li s5, 6
    li s8, 1000
remap_loop:
    xori s7, s7, 3
    li t0, 0x40cf
    li t1, 1
    beq s7, t1, remap_ready
    li t0, 0x44cf
remap_ready:
    sw t0, 0(s4)
    sfence.vma s2, t6
    jalr ra, s2, 0
    bne a0, s7, fail
    jalr ra, s3, 0
    li t0, 3
    bne a0, t0, fail
    addi s8, s8, -1
    bnez s8, remap_loop
    la a0, ok
    j print
warm:
    li s6, 2000
2:
    jalr ra, s2, 0
    bne a0, s7, fail
    jalr ra, s3, 0
    li t0, 3
    bne a0, t0, fail
    addi s6, s6, -1
    bnez s6, 2b
    jr t4
fail:
    li t1, 0xf4000000
    addi t0, s5, '0'
    sb t0, 0(t1)
    la a0, bad
print:
    li t1, 0xf4000000
3:
    lbu t0, 0(a0)
    beqz t0, shutdown
    sb t0, 0(t1)
    addi a0, a0, 1
    j 3b
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
.org 0x12000
keeper:
    li a0, 3
    ret
.section .rodata
ok: .asciz "SYSTEM AOT ASID scope OK\n"
bad: .asciz "SYSTEM AOT ASID scope FAIL\n"
.section .bss
.balign 4096
shared: .space 4096
root_a: .space 4096
root_b: .space 4096
