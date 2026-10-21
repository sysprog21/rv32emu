/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* Flat S-mode kernel, loaded at address zero with -k. Exercise 4 KiB hits,
 * colliding VPNs, load/store page faults, load/store alignment faults, and
 * nonzero slices of a 4 MiB superpage. Code also crosses a superpage's 4 KiB
 * slice boundary. Twelve live values and the old load destination must survive
 * both successful accesses and precise trap exits. Each case runs 2000 times
 * to reach T1 under the system-mode hotness threshold. The final checks
 * require exactly 8000 counted loop faults, the write PTE's dirty bit, and an
 * unchanged read-only page. A selective SFENCE.VMA must redirect instruction
 * fetches to a new physical page, and flushing one address of a superpage must
 * drop its other cached slices and any code decoded through it. Switching satp
 * with CSRS and CSRC must not reuse code decoded under the old address space,
 * a faulting AMO takes a store/AMO page fault, a load into x0 from an unmapped
 * page faults, and a device load into x0 keeps x0 zero, also in a trap handler
 * that runs inside a faulting access. UART output and SBI shutdown exercise
 * MMIO/helper exits as well.
 */

.option norvc
# Fixed physical target addresses below must survive the final link.
.option norelax
.section .text
.globl _start
_start:
    la t0, trap
    csrw stvec, t0
    la t0, root
    li t1, 0xcf
    sw t1, 0(t0)
    la t1, leaves
    srli t1, t1, 12
    slli t1, t1, 10
    ori t1, t1, 1
    sw t1, 1024(t0)
    li t1, 0x3d0000cf
    li t2, 3904
    add t2, t2, t0
    sw t1, 0(t2)
    la t0, code_leaves
    li t1, 0x804b
    sw t1, 0(t0)
    srli t1, t0, 12
    slli t1, t1, 10
    ori t1, t1, 1
    la t0, root
    sw t1, 8(t0)
    la t0, leaves
    li t1, 0x404f
    sw t1, 0(t0)
    sw t1, 256(t0)
    li t1, 0x4443
    sw t1, 4(t0)
    li t0, 0x10000
    sw zero, 0(t0)
    li t0, 0x11000
    sw zero, 0(t0)
    li t0, 0x10ffc
    sw zero, 0(t0)
    li t0, 0x3ffffc
    sw zero, 0(t0)
    la t0, root
    srli t0, t0, 12
    li t1, 0x80000000
    or t0, t0, t1
    csrw satp, t0
    sfence.vma
    li a5, 0
mode:
    li a3, 0
    li t3, 0
    li t4, 0x40000000
    li t5, 0
    li t6, 0
    mv a6, t4
    beqz a5, loop
    li t0, 1
    bne a5, t0, mode_fault
    li t5, 0x40000
    li a3, 2099
    j loop
mode_fault:
    li t0, 2
    bne a5, t0, mode_store
    li t4, 0x40002000
    li t6, 13
    li a3, 100
    j loop
mode_store:
    li t0, 3
    bne a5, t0, mode_misaligned_load
    li t4, 0x40001000
    mv a6, t4
    li t6, 15
    j loop
mode_misaligned_load:
    li t0, 4
    bne a5, t0, mode_misaligned_store
    addi t4, t4, 1
    li t6, 4
    li a3, 100
    j loop
mode_misaligned_store:
    li t0, 5
    bne a5, t0, mode_superpage
    addi a6, a6, 1
    li t6, 6
    li a3, 2099
    j loop
mode_superpage:
    li t4, 0x10ffc
    li t0, 6
    beq a5, t0, super_ready
    li t4, 0x3ffffc
super_ready:
    mv a6, t4
    j loop
    .balign 4096
loop:
    addi s0, t3, 1
    addi s1, t3, 2
    addi s2, t3, 3
    addi s3, t3, 4
    addi s4, t3, 5
    addi s5, t3, 6
    addi s6, t3, 7
    addi s7, t3, 8
    addi s8, t3, 9
    addi s9, t3, 10
    addi s10, t3, 11
    addi s11, t3, 12
    addi a0, t3, 100
    lw a0, 0(t4)
    bne a0, a3, fail
    add t0, s0, s1
    add t0, t0, s2
    add t0, t0, s3
    add t0, t0, s4
    add t0, t0, s5
    add t0, t0, s6
    add t0, t0, s7
    add t0, t0, s8
    add t0, t0, s9
    add t0, t0, s10
    add t0, t0, s11
    slli t1, t3, 3
    slli t2, t3, 2
    add t1, t1, t2
    addi t1, t1, 78
    bne t0, t1, fail
    addi s0, t3, 1
    addi s1, t3, 2
    addi s2, t3, 3
    addi s3, t3, 4
    addi s4, t3, 5
    addi s5, t3, 6
    addi s6, t3, 7
    addi s7, t3, 8
    addi s8, t3, 9
    addi s9, t3, 10
    addi s10, t3, 11
    addi s11, t3, 12
    addi a0, t3, 100
    sw a0, 0(a6)
    add t0, s0, s1
    add t0, t0, s2
    add t0, t0, s3
    add t0, t0, s4
    add t0, t0, s5
    add t0, t0, s6
    add t0, t0, s7
    add t0, t0, s8
    add t0, t0, s9
    add t0, t0, s10
    add t0, t0, s11
    slli t1, t3, 3
    slli t2, t3, 2
    add t1, t1, t2
    addi t1, t1, 78
    bne t0, t1, fail
    # Successful stores determine the next load, while faulting stores
    # leave the previous value intact. Faulting loads preserve their old rd.
    li t0, 13
    beq t6, t0, next_fault_load
    li t0, 4
    beq t6, t0, next_fault_load
    li t0, 15
    beq t6, t0, next_iteration
    li t0, 6
    beq t6, t0, next_iteration
    addi a3, t3, 100
    j next_iteration
next_fault_load:
    addi a3, t3, 101
next_iteration:
    xor t4, t4, t5
    xor a6, a6, t5
    addi t3, t3, 1
    li t0, 2000
    bne t3, t0, loop
    addi a5, a5, 1
    li t0, 8
    bne a5, t0, mode
    la t0, faults
    lw t0, 0(t0)
    li t1, 8000
    bne t0, t1, fail
    la t0, leaves
    lw t0, 0(t0)
    andi t0, t0, 0x80
    beqz t0, fail
    li t0, 0x11000
    lw t0, 0(t0)
    bnez t0, fail
    # Keep the target iTLB slot live across a selective mapping flush.
    # The caller runs on VPN 1; the target uses a different index (VPN 0x800).
    li t0, 0x800000
    jalr ra, t0, 0
    li t1, 7
    bne a0, t1, fail
    la t0, code_leaves
    li t1, 0x844b
    sw t1, 0(t0)
    li t0, 0x800000
    sfence.vma t0, zero
    jalr ra, t0, 0
    li t1, 9
    bne a0, t1, fail
    # Repeat the remap in an inline page-fault handler. Its return skips the
    # faulting load and immediately branches to the previously cached code.
    la t0, remap_request
    li t1, 1
    sw t1, 0(t0)
    li s2, 0x800000
    li t0, 0xc00000
    lw a0, 0(t0)
    jalr ra, s2, 0
    li t1, 7
    bne a0, t1, fail
    # A selective SFENCE.VMA of one address covers its whole superpage leaf.
    # Cache two 4 KiB slices of a superpage, invalidate the leaf, and flush
    # only the first address: the second slice must fault.
    la t0, root
    li t1, 0xcf
    sw t1, 4(t0)
    sfence.vma
    li s3, 0x410000
    li s4, 0x411000
    lw t1, 0(s3)
    lw t1, 0(s4)
    la t0, root
    sw zero, 4(t0)
    sfence.vma s3, zero
    la t0, load_request
    li t1, 1
    sw t1, 0(t0)
    lw t1, 0(s4)
    la t0, load_request
    lw t0, 0(t0)
    bnez t0, fail
    # Code fetched through a superpage: flushing another page of the same
    # leaf must retire it too. Through a superpage at physical 4 MiB, copy the
    # 9-returning routine to the slot that holds the 7-returning one at
    # physical 0x20000. Run the original until it is hot, remap the leaf, and
    # flush only the superpage's first page.
    la t0, root
    li t1, 0x1000cf
    sw t1, 4(t0)
    sfence.vma
    li t0, 0x21000
    li t1, 0x420000
    lw t2, 0(t0)
    sw t2, 0(t1)
    lw t2, 4(t0)
    sw t2, 4(t1)
    la t0, root
    li t1, 0xcf
    sw t1, 4(t0)
    sfence.vma
    li s5, 0x420000
    li s6, 3000
1:
    jalr ra, s5, 0
    li t1, 7
    bne a0, t1, fail
    addi s6, s6, -1
    bnez s6, 1b
    la t0, root
    li t1, 0x1000cf
    sw t1, 4(t0)
    li t0, 0x400000
    sfence.vma t0, zero
    jalr ra, s5, 0
    li t1, 9
    bne a0, t1, fail
    # Switching address space by setting and clearing satp bits, with a new
    # ASID and no SFENCE.VMA, must not reuse code decoded under the old one.
    # The second root differs from the first in PPN bit 3 and maps the routine
    # page to the 9-returning code.
    la t0, root_b
    li t1, 0xcf
    sw t1, 0(t0)
    li t1, 0x3401
    sw t1, 8(t0)
    la t0, leaves_b
    li t1, 0x844b
    sw t1, 0(t0)
    li s5, 0x800000
    li s6, 3000
1:
    jalr ra, s5, 0
    li t1, 7
    bne a0, t1, fail
    addi s6, s6, -1
    bnez s6, 1b
    li s7, 0x400008
    csrs satp, s7
    jalr ra, s5, 0
    li t1, 9
    bne a0, t1, fail
    csrc satp, s7
    jalr ra, s5, 0
    li t1, 7
    bne a0, t1, fail
    # An AMO takes a store/AMO page fault, never the load fault of its read,
    # and commits nothing.
    la t0, amo_request
    li t1, 1
    sw t1, 0(t0)
    li s3, 0x40002000
    li a1, 5
    li a2, 0x55
    amoadd.w a2, a1, (s3)
    li t1, 0x55
    bne a2, t1, fail
    la t0, amo_request
    lw t0, 0(t0)
    bnez t0, fail
    # A load into x0 still performs its access, so an unmapped address takes
    # a load page fault, interpreted and under T1.
    li s4, 0x40002000
    li s6, 3000
1:
    la t0, load_request
    li t1, 1
    sw t1, 0(t0)
    lw zero, 0(s4)
    la t0, load_request
    lw t0, 0(t0)
    bnez t0, fail
    addi s6, s6, -1
    bnez s6, 1b
    # A device load into x0 still leaves x0 zero, interpreted and under T1.
    li s3, 0xf4000005
    li s6, 3000
1:
    lb zero, 0(s3)
    addi t1, zero, 0
    bnez t1, fail
    addi s6, s6, -1
    bnez s6, 1b
    la a0, success
    j print
trap:
    la t0, amo_request
    lw t0, 0(t0)
    bnez t0, amo_trap
    la t0, load_request
    lw t0, 0(t0)
    bnez t0, load_trap
    la t0, remap_request
    lw t0, 0(t0)
    bnez t0, remap_trap
    csrr t0, scause
    bne t0, t6, fail
    andi t0, t0, 2
    csrr t1, stval
    bnez t0, trap_store
    bne t1, t4, fail
    j trap_regs
trap_store:
    bne t1, a6, fail
trap_regs:
    addi t0, t3, 100
    bne a0, t0, fail
    add t0, s0, s1
    add t0, t0, s2
    add t0, t0, s3
    add t0, t0, s4
    add t0, t0, s5
    add t0, t0, s6
    add t0, t0, s7
    add t0, t0, s8
    add t0, t0, s9
    add t0, t0, s10
    add t0, t0, s11
    slli t1, t3, 3
    slli t2, t3, 2
    add t1, t1, t2
    addi t1, t1, 78
    bne t0, t1, fail
    la t0, faults
    lw t1, 0(t0)
    addi t1, t1, 1
    sw t1, 0(t0)
    # Invalidate once inside a synchronous trap. The interrupted load still
    # owns its decoded instruction until this handler returns.
    li t0, 1
    bne t1, t0, 2f
    li t0, 0x800000
    sfence.vma t0, zero
2:
    csrr t0, sepc
    addi t0, t0, 4
    csrw sepc, t0
    sret
remap_trap:
    csrr t0, scause
    li t1, 13
    bne t0, t1, fail
    la t0, code_leaves
    li t1, 0x804b
    sw t1, 0(t0)
    li t0, 0x800000
    sfence.vma t0, zero
    la t0, remap_request
    sw zero, 0(t0)
    csrr t0, sepc
    addi t0, t0, 4
    csrw sepc, t0
    sret
amo_trap:
    csrr t0, scause
    li t1, 15
    bne t0, t1, fail
    csrr t0, stval
    bne t0, s3, fail
    # This handler runs inside the faulting access, one instruction at a
    # time; a device load into x0 there leaves x0 zero as well.
    li t0, 0xf4000005
    lb zero, 0(t0)
    addi t1, zero, 0
    bnez t1, fail
    la t0, amo_request
    sw zero, 0(t0)
    csrr t0, sepc
    addi t0, t0, 4
    csrw sepc, t0
    sret
load_trap:
    csrr t0, scause
    li t1, 13
    bne t0, t1, fail
    csrr t0, stval
    bne t0, s4, fail
    la t0, load_request
    sw zero, 0(t0)
    csrr t0, sepc
    addi t0, t0, 4
    csrw sepc, t0
    sret
#include "system-exit.inc"
success: .asciz "SYSTEM MMU cache OK\n"
failure: .asciz "SYSTEM MMU cache FAIL\n"
.balign 4
faults: .word 0
remap_request: .word 0
load_request: .word 0
amo_request: .word 0
# Keep page tables in the zero-filled gap, independent of code placement.
.set root, 0x4000
.set leaves, 0x5000
.set code_leaves, 0x6000
.set root_b, 0xc000
.set leaves_b, 0xd000
.org 0x20000, 0
    addi a0, zero, 7
    jalr zero, ra, 0
.org 0x21000, 0
    addi a0, zero, 9
    jalr zero, ra, 0
