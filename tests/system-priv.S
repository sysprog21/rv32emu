/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* Flat S-mode kernel, loaded at address zero with -k, checking that cached
 * translations and translated code keep privilege checks. Every case loops
 * 2000 times so the code under test is hot and native, while the trap handler
 * keeps the kernel's own translations and blocks warm in S-mode:
 *
 * - U-mode loads from a kernel page fault, and U-mode jumps into kernel code
 *   fault on the fetch, while user loads from a user page succeed.
 * - S-mode loads from a user page fault unless SSTATUS.SUM is set.
 * - S-mode loads from an execute-only page fault unless SSTATUS.MXR is set.
 * - S-mode jumps into a user page fault on the fetch.
 * - A leaf beyond the 32-bit physical space takes a load access fault.
 *
 * s11 holds the expected cause and s9 counts the faults taken.
 */

.option norvc
.option norelax
.equ ITER, 2000
.equ SUM, 1 << 18
.equ MXR, 1 << 19
.equ SPP, 1 << 8
.equ USER_CODE, 0x400000
.equ USER_DATA, 0x401000
.equ XONLY, 0x402000
.equ HIGH_PA, 0x403000

.section .text
.globl _start
_start:
    la t0, trap
    csrw stvec, t0
    /* VA 0-4 MiB maps physical 0-4 MiB as a supervisor superpage. */
    la t0, root
    li t1, 0xcf
    sw t1, 0(t0)
    /* The UART at 0xf4000000 is a supervisor superpage too. */
    li t1, 0x3d0000cf
    li t2, 976 * 4
    add t2, t2, t0
    sw t1, 0(t2)
    /* VA 4-8 MiB goes through a leaf table. */
    la t1, leaves
    srli t1, t1, 12
    slli t1, t1, 10
    ori t1, t1, 1
    sw t1, 4(t0)
    la t0, leaves
    la t1, user_page /* user code: U R X */
    srli t1, t1, 12
    slli t1, t1, 10
    ori t1, t1, 0x5b
    sw t1, 0(t0)
    la t1, user_data /* user data: U R W */
    srli t1, t1, 12
    slli t1, t1, 10
    ori t1, t1, 0xd7
    sw t1, 4(t0)
    la t1, xonly_data /* supervisor execute-only */
    srli t1, t1, 12
    slli t1, t1, 10
    ori t1, t1, 0x49
    sw t1, 8(t0)
    li t1, 0x40000043 /* PPN 1 << 20: physical 4 GiB */
    sw t1, 12(t0)
    la t0, root
    srli t0, t0, 12
    li t1, 0x80000000
    or t0, t0, t1
    csrw satp, t0
    sfence.vma

    /* U-mode: two faults per pass, then an ECALL back to the kernel. */
    li s7, 0
    li s9, 0
    la a0, kernel_data
    la a1, kernel_func
    li a2, ITER
    li a3, USER_DATA
    la s8, user_done
    li t0, SPP
    csrc sstatus, t0
    li t0, USER_CODE
    csrw sepc, t0
    sret
user_done:
    li t0, 2 * ITER
    bne s9, t0, fail
    li t0, 2 * ITER
    bne s7, t0, fail /* the handler's kernel calls ran */

    /* S-mode loads from a user page: SUM off faults, SUM on succeeds. */
    li s9, 0
    li s11, 13
    li a2, ITER
    li a3, USER_DATA
    li t2, SUM
sum_loop:
    csrs sstatus, t2
    lw t1, 0(a3)
    li t0, 0x5a5a5a5a
    bne t1, t0, fail
    csrc sstatus, t2
    lw t1, 0(a3) /* faults; the handler skips it */
    addi a2, a2, -1
    bnez a2, sum_loop
    li t0, ITER
    bne s9, t0, fail

    /* S-mode loads from an execute-only page need MXR. */
    li s9, 0
    li a2, ITER
    li a3, XONLY
    li t2, MXR
mxr_loop:
    csrs sstatus, t2
    lw t1, 0(a3)
    li t0, 0x13579bdf
    bne t1, t0, fail
    csrc sstatus, t2
    lw t1, 0(a3) /* faults */
    addi a2, a2, -1
    bnez a2, mxr_loop
    li t0, ITER
    bne s9, t0, fail

    /* S-mode never executes a user page, even with SUM set. */
    li s9, 0
    li s11, 12
    li a2, ITER
    li a3, USER_CODE
    li t0, SUM
    csrs sstatus, t0
sexec_loop:
    jalr ra, 0(a3) /* faults; the handler resumes at ra */
    addi a2, a2, -1
    bnez a2, sexec_loop
    li t0, SUM
    csrc sstatus, t0
    li t0, ITER
    bne s9, t0, fail

    /* A leaf beyond 4 GiB of physical space takes an access fault. */
    li s9, 0
    li s11, 5
    li a2, ITER
    li a3, HIGH_PA
high_loop:
    lw t1, 0(a3) /* faults */
    addi a2, a2, -1
    bnez a2, high_loop
    li t0, ITER
    bne s9, t0, fail

    /* Warm the same U-mode load/store loop before and after a remap. The
     * S-mode collision replaces its shared dTLB slot before the scoped fence.
     */
    sfence.vma
    li a3, USER_DATA
    li a4, 0x5a5a5a5a
    la s8, remap_user_data
    j enter_remap_user
remap_user_data:
    li t0, 0x1000 /* VPN 1 shares USER_DATA's slot up to 1024 TLB entries */
    lw t1, 0(t0)
    la t0, leaves
    la t1, remapped_data
    srli t1, t1, 12
    slli t1, t1, 10
    ori t1, t1, 0xd7
    sw t1, 4(t0)
    sfence.vma a3, zero
    li a4, 0x2468ace0
    la s8, remap_done
enter_remap_user:
    li a2, ITER
    li t0, SPP
    csrc sstatus, t0
    la t0, user_remap_loop
    la t1, user_page
    sub t0, t0, t1
    li t1, USER_CODE
    add t0, t0, t1
    csrw sepc, t0
    sret
remap_done:
    la t0, remapped_data
    lw t1, 0(t0)
    bne t1, a4, fail
    la t0, user_data
    lw t1, 0(t0)
    li t2, 0x5a5a5a5a
    bne t1, t2, fail

    la a0, success
    j print

/* Called by the trap handler, so its block runs in S-mode while the user
 * code keeps jumping to it.
 */
kernel_func:
    addi s7, s7, 1
    jr t2

    .balign 4
trap:
    csrr t0, scause
    li t1, 8
    beq t0, t1, trap_ecall
    csrr t1, sstatus
    andi t1, t1, SPP
    bnez t1, trap_supervisor
    /* From U-mode: a load fault on kernel data, or a fetch fault on kernel
     * code. Touch both from S-mode first, so their translations and blocks
     * are warm in the other privilege.
     */
    lw t1, 0(a0)
    jal t2, kernel_func
    li t1, 13
    beq t0, t1, trap_skip
    li t1, 12
    bne t0, t1, fail
    csrr t1, stval
    bne t1, a1, fail
    addi s9, s9, 1
    csrw sepc, ra
    sret
trap_supervisor:
    bne t0, s11, fail
    li t1, 12
    beq t0, t1, trap_resume_ra
trap_skip:
    addi s9, s9, 1
    csrr t1, sepc
    addi t1, t1, 4
    csrw sepc, t1
    sret
trap_resume_ra:
    addi s9, s9, 1
    csrw sepc, ra
    sret
trap_ecall:
    csrr t1, sstatus
    andi t1, t1, SPP
    bnez t1, fail
    jr s8

#include "system-exit.inc"

/* Mapped at USER_CODE. Kernel addresses arrive in registers. */
    .balign 4096
user_page:
user_loop:
    lw t1, 0(a0) /* kernel data: faults */
    jalr ra, 0(a1) /* kernel code: faults on the fetch */
    lw t1, 0(a3) /* user data: succeeds */
    li t0, 0x5a5a5a5a
    bne t1, t0, user_fail
    addi a2, a2, -1
    bnez a2, user_loop
    ecall
user_remap_loop:
    lw t1, 0(a3)
    bne t1, a4, user_fail
    sw a4, 0(a3)
    addi a2, a2, -1
    bnez a2, user_remap_loop
    ecall
user_fail:
    unimp /* an illegal instruction reaches "fail" */

.section .data
    .balign 4096
root:
    .space 4096
leaves:
    .space 4096
user_data:
    .word 0x5a5a5a5a
    .balign 4096
xonly_data:
    .word 0x13579bdf
    .balign 4096
remapped_data:
    .word 0x2468ace0
    .balign 4096
kernel_data:
    .word 0
success:
    .string "SYSTEM privilege OK\n"
failure:
    .string "SYSTEM privilege FAILED\n"
