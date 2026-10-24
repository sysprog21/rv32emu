/* SFENCE.VMA must retire retained code outside the current SATP. Exercise
 * all four rs1/rs2 forms, ASID reuse with a new root and a global mapping.
 * Target code uses 4 KiB leaves so selective fences exercise cross-space
 * page indexes. Low superpages identity-map the kernel and test data.
 */
#ifndef START_CASE
#define START_CASE 0
#endif
.option norvc
.option norelax
.text
.globl _start
_start:
    la t0, root_a
    la t1, root_b
    la t2, root_c
    li t3, 0xcf
    sw t3, 0(t0)
    sw t3, 0(t1)
    sw t3, 0(t2)
    li t3, 0x1000cf
    sw t3, 4(t0)
    sw t3, 4(t1)
    sw t3, 4(t2)
    li t3, 0x2000cf
    sw t3, 8(t0)
    sw t3, 8(t1)
    sw t3, 8(t2)
    li t3, 0x3d0000cf
    li t4, 3904
    add t5, t0, t4
    sw t3, 0(t5)
    add t5, t1, t4
    sw t3, 0(t5)
    add t5, t2, t4
    sw t3, 0(t5)
    /* Generated code: li a0, 1/2; ret. */
    li t3, 0x100513
    li t4, 0x400000
    sw t3, 0(t4)
    li t3, 0x200513
    li t4, 0x800000
    sw t3, 0(t4)
    li t3, 0x8067
    sw t3, 4(t4)
    li t4, 0x400000
    sw t3, 4(t4)
    /* All roots share a global code mapping at 0x40400000. */
    li t3, 0x1000ef
    sw t3, 1028(t0)
    sw t3, 1028(t1)
    sw t3, 1028(t2)
    srli s0, t0, 12
    li t3, 0x80400000          /* mode Sv32, ASID 1 */
    or s0, s0, t3
    srli s1, t1, 12
    li t3, 0x80800000          /* ASID 2 */
    or s1, s1, t3
    srli s2, t2, 12
    li t3, 0x80400000          /* reuse ASID 1 with another root */
    or s2, s2, t3
    la t3, leaves_a
    srli t3, t3, 12
    slli t3, t3, 10
    ori t3, t3, 1
    sw t3, 1024(t0)
    la t3, leaves_c
    srli t3, t3, 12
    slli t3, t3, 10
    ori t3, t3, 1
    sw t3, 1024(t2)
    li s3, START_CASE
case:
    /* Warm A's old mapping, including native code where enabled. */
    la t0, root_a
    la t1, root_b
    la t2, root_c
    li t3, 0x1000ef
    sw t3, 1028(t0)
    sw t3, 1028(t1)
    sw t3, 1028(t2)
    la t0, leaves_a
    li t1, 0x1000cf
    sw t1, 0(t0)
    csrw satp, s0
    sfence.vma
    li s4, 5000
    li s5, 1
    li s6, 0x40000000
    jal ra, check_code
    li s6, 0x40400000
    li s4, 5000
    jal ra, check_code
    csrw satp, s1
    li s6, 0x40400000
    li s4, 5000
    jal ra, check_code
    /* Edit A while executing B, then fence from B. */
    la t0, leaves_a
    li t1, 0x2000cf
    sw t1, 0(t0)
    li t0, 0x40000000
    li t1, 1
    beqz s3, all_all
    li t2, 1
    beq s3, t2, va_all
    li t2, 2
    beq s3, t2, all_asid
    sfence.vma t0, t1
    j fenced
all_all:
    sfence.vma zero, zero
    j fenced
va_all:
    sfence.vma t0, zero
    j fenced
all_asid:
    sfence.vma zero, t1
fenced:
    csrw satp, s0
    li s4, 5000
    li s5, 2
    li s6, 0x40000000
    jal ra, check_code
    /* Same ASID, new root: its different mapping must take effect. */
    la t0, leaves_c
    li t1, 0x1000cf
    sw t1, 0(t0)
    csrw satp, s2
    sfence.vma
    li s4, 5000
    li s5, 1
    jal ra, check_code
    li s6, 0x40400000
    li s4, 5000
    jal ra, check_code
    /* Recycle A's exact root and ASID while B is current. */
    csrw satp, s1
    la t0, leaves_a
    li t1, 0x1000cf
    sw t1, 0(t0)
    li t1, 1
    sfence.vma zero, t1
    csrw satp, s0
    li s6, 0x40000000
    li s4, 5000
    jal ra, check_code
    /* Replace the shared global mapping, fencing from another space. */
    csrw satp, s1
    la t0, root_a
    la t1, root_b
    la t2, root_c
    li t3, 0x2000ef
    sw t3, 1028(t0)
    sw t3, 1028(t1)
    sw t3, 1028(t2)
    sfence.vma zero, zero
    csrw satp, s0
    li s6, 0x40400000
    li s4, 5000
    li s5, 2
    jal ra, check_code
    addi s3, s3, 1
    li t0, 4
    blt s3, t0, case
    la a0, success
    j print
check_code:
    mv s7, ra
1:
    jalr ra, s6, 0
    bne a0, s5, sfence_fail
    addi s4, s4, -1
    bnez s4, 1b
    jr s7
sfence_fail:
    li t0, 0xf4000000
    addi t1, s3, 48
    sb t1, 0(t0)
    li t1, 10
    sb t1, 0(t0)
    j fail
#include "system-exit.inc"
success: .asciz "SYSTEM SFENCE ASID OK\n"
failure: .asciz "SYSTEM SFENCE ASID FAIL\n"
.set root_a, 0x4000
.set root_b, 0x5000
.set root_c, 0x6000
.set leaves_a, 0x7000
.set leaves_c, 0x8000
