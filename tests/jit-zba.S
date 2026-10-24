/* Load operands at runtime and compare each Zba form with RV32I arithmetic.
 * Force dispatcher entry so the probe executes in actual tier 2 as well.
 * In system mode, an interpreted direct jump keeps chaining into a hot block,
 * checking only one transition in PROBE_INTERVAL; an indirect jump returns a
 * hot target to the dispatcher, which then enters its native code.
 */
#ifndef ZBA_ITERATIONS
#define ZBA_ITERATIONS 1000000
#endif
.option norvc
.option norelax
.text
.globl _start, zba_probe, zba_probe2, zba_probe3
.macro DISPATCH_TO target
#ifdef SYSTEM_MODE
    csrr t0, sscratch     /* CSR keeps this block interpreted */
    la t0, \target
    jr t0
#else
    li a0, 0
    li a7, 214
    ecall
#endif
.endm
/* Dispatch to target, then restore the operands the dispatch may clobber. */
.macro RESUME target
    DISPATCH_TO \target
\target:
    mv a0, s4
    mv a1, s5
.endm
.macro DISPATCH
    RESUME .Lzba_dispatch_\@
.endm
.macro REF shift, addend
    slli a2, a0, \shift
    xori t6, s1, 1          /* keep the reference out of shift/add fusion */
    add a2, a2, \addend
.endm
.macro CHECK op, shift
    mv s4, a0
    mv s5, a1
    REF \shift, a1
    \op a3, a0, a1
    bne a3, a2, fail
    DISPATCH
    mv a3, a0
    \op a3, a3, a1
    bne a3, a2, fail
    DISPATCH
    mv a3, a1
    \op a3, a0, a3
    bne a3, a2, fail
    REF \shift, a0
    DISPATCH
    mv a3, a0
    \op a3, a3, a3
    bne a3, a2, fail
    DISPATCH
    slli a2, a0, \shift
    \op a3, a0, zero
    bne a3, a2, fail
    DISPATCH
    \op a3, zero, a1
    bne a3, a1, fail
    DISPATCH
    \op a3, zero, zero
    bne a3, s3, fail
.endm
_start:
#ifdef SYSTEM_MODE
    la t0, root
    li t1, 0xcf
    sw t1, 0(t0)          /* identity-map low RAM */
    li t2, 2048
    add t2, t0, t2
    sw t1, 0(t2)          /* 0x80000000 maps the same RAM */
    li t1, 0x3d0000cf
    li t2, 3904
    add t2, t0, t2
    sw t1, 0(t2)          /* UART */
    srli t0, t0, 12
    li t1, 0x80000000
    or t0, t0, t1
    csrw satp, t0
    sfence.vma
#endif
    li s0, ZBA_ITERATIONS
    li s1, 0
    la s2, values
    li s3, 0
    mv s6, s2
#ifdef SYSTEM_MODE
    li t0, 0x80000000
    or s6, s6, t0
#endif
outer:
    DISPATCH_TO zba_probe
zba_probe:
    srli t0, s1, 4
    slli t0, t0, 2
    xori t6, s1, 1      /* independent address reference, no LEA peephole */
    add t0, s2, t0
    lw a0, 0(t0)
    andi t0, s1, 15
    slli t0, t0, 2
    xori t6, s1, 1      /* independent address reference, no LEA peephole */
    add t0, s2, t0
    lw a1, 0(t0)
    CHECK sh1add, 1
    RESUME zba_probe2
    CHECK sh2add, 2
    RESUME zba_probe3
    CHECK sh3add, 3
    DISPATCH
    srli t0, s1, 4
    li t2, 0x40000000
    or t0, t0, t2       /* shifting must discard high bits before addressing */
    sh2add t1, t0, s6    /* high-bit virtual base in system mode */
    lw t1, 0(t1)
    bne t1, a0, fail
    addi s1, s1, 1
    andi s1, s1, 255
    addi s0, s0, -1
    bnez s0, outer
#ifdef SYSTEM_MODE
    la a0, success
    j print
#include "system-exit.inc"
#else
    li a0, 1
    la a1, success
    la a2, success_end
    sub a2, a2, a1
    li a7, 64
    ecall
    li a0, 0
    j stop
fail:
    li a0, 1
stop:
    li a7, 93
    ecall
#endif
.section .rodata
.balign 4
values:
    .word 0, 1, -1, 2, -2, 0x80000000, 0x7fffffff, 0x80000001
    .word 0x40000000, 0x20000000, 0x10000000, 0x12345678
    .word 0xaaaaaaaa, 0x55555555, 0xffff0000, 0x0000ffff
#ifdef SYSTEM_MODE
success: .asciz "SYSTEM Zba arithmetic OK\n"
failure: .asciz "SYSTEM Zba arithmetic FAIL\n"
.balign 4096
root: .space 4096
#else
success: .ascii "Zba arithmetic OK\n"
success_end:
#endif
