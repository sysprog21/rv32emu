/* A fault handler may replace code and fence while a native helper is live.
 * Warm the probe and its indirect return, fault its load, then require the
 * replacement instruction rather than the already-emitted native suffix.
 */
#ifndef RETIRE_WARM_ITERATIONS
#define RETIRE_WARM_ITERATIONS 100000
#endif
.option norvc
.option norelax
.text
.globl _start
_start:
    la t0, handler
    csrw stvec, t0
    la s0, root
    li t0, 0xcf
    sw t0, 0(s0)
    la s1, leaves
    srli t0, s1, 12
    slli t0, t0, 10
    ori t0, t0, 1
    sw t0, 4(s0)
    li t0, 0x3d0000cf
    li t1, 3904
    add t1, s0, t1
    sw t0, 0(t1)
    li t0, 0x40cf
    sw t0, 0(s1)
    li s2, 0x10000
    li s3, 0x400000
    li s4, 0
#ifdef RETIRE_INNER
    /* Re-enter the hot loop itself after unmapping. A hot caller alone does
     * not ensure the faulting probe runs in an asynchronously compiled region.
     */
    li s5, 1
    li s7, 20000000
#else
    li s5, RETIRE_WARM_ITERATIONS
#endif
    li s6, 0x12345678
#ifdef RETIRE_MMIO
    li s6, 'Z'
#endif
    sw s6, 0(s2)
    li t0, 1
    sw t0, 4(s2)
#ifdef RETIRE_SFENCE
    /* A second physical code page and a 4 KiB identity leaf table. */
    li t0, 0
    li t1, 0x20000
    li t2, 4096
1:
    lw t3, 0(t0)
    sw t3, 0(t1)
    addi t0, t0, 4
    addi t1, t1, 4
    addi t2, t2, -4
    bnez t2, 1b
    la t0, code_leaves
    li t1, 0xcf
    li t2, 64
2:
    sw t1, 0(t0)
    addi t0, t0, 4
    addi t1, t1, 1024
    addi t2, t2, -1
    bnez t2, 2b
    la t0, code_leaves
    li t1, 0x80cf
    sw t1, 0(t0)             /* VA zero now names PA 0x20000 */
#endif
    srli t0, s0, 12
    li t1, 0x80000000
    or t0, t0, t1
    csrw satp, t0
    sfence.vma
warm:
    jal ra, probe
#ifdef RETIRE_MULTI_LOAD
    mv a5, a4
#endif
#ifdef RETIRE_FUSED
    li t0, 0x400004
    bne s3, t0, fail
    addi s3, s3, -4
#endif
#ifdef RETIRE_STORE
    lw a3, 0(s2)
#endif
    li t0, 1
    bne a5, t0, fail
    bne a3, s6, fail
#ifdef RETIRE_AMO
    addi s6, s6, 1
#endif
    addi s5, s5, -1
    bnez s5, warm
    bnez s4, fail
    sw zero, 0(s1)
#if defined(RETIRE_STORE) && !defined(RETIRE_MMIO)
    /* A dropped faulting store must not pass using the warmup value. */
    sw zero, 0(s2)
#endif
#ifdef RETIRE_MULTI_STORE
    sw zero, 4(s2)
#endif
    sfence.vma s3             /* data-only fence retains the compiled probe */
#ifdef RETIRE_INNER
    li s7, 1
    la ra, validate_replacement
    j fault_access
#else
    jal ra, probe
#endif
validate_replacement:
#ifdef RETIRE_MULTI_LOAD
    mv a5, a4
#endif
#ifdef RETIRE_MULTI_STORE
    lw t0, 4(s2)
    bnez t0, fail
#endif
#ifdef RETIRE_FUSED
    li t0, 0x400008
    bne s3, t0, fail
    li a5, 2
#endif
#ifdef RETIRE_STORE
    lw a3, 0(s2)
#endif
    li t0, 2
    bne a5, t0, fail
    bne a3, s6, fail
#ifdef RETIRE_AMO
    addi s6, s6, 1
    lw t0, 0(s2)
    bne t0, s6, fail
#endif
    li t0, 1
    bne s4, t0, fail
#ifdef RETIRE_CYCLES
    rdcycle s7
#if RETIRE_WARM_ITERATIONS == 100000
    /* Exact retired prefix, shared by interpreter, T1C and T2C. */
    li t0, 0x000f428b
    bne s7, t0, fail
#endif
    li s8, 28
cycle_digit:
    srl t0, s7, s8
    andi t0, t0, 15
    li t1, 10
    bltu t0, t1, decimal_digit
    addi t0, t0, 'a' - 10
    j emit_digit
decimal_digit:
    addi t0, t0, '0'
emit_digit:
    li t1, 0xf4000000
    sb t0, 0(t1)
    addi s8, s8, -4
    bgez s8, cycle_digit
    li t0, 10
    sb t0, 0(t1)
#endif
    la a0, success
    j print
probe:
#if defined(RETIRE_FUSED) || defined(RETIRE_MULTI_LOAD) || defined(RETIRE_MULTI_STORE)
    li a5, 1
#endif
    mv a0, s3
#if defined(RETIRE_STORE) && defined(RETIRE_RVC)
    mv a4, s6
#endif
#ifdef RETIRE_AMO
    li a4, 1
#endif
fault_access:
#ifdef RETIRE_AMO
    amoadd.w a3, a4, (s3)
#elif defined(RETIRE_STORE)
#ifdef RETIRE_RVC
    .option push
    .option rvc
    c.sw a4, 0(a0)
    .option pop
#elif defined(RETIRE_MMIO)
    sb s6, 0(s3)
#else
    sw s6, 0(s3)
#endif
#elif defined(RETIRE_RVC)
    .option push
    .option rvc
    c.lw a3, 0(a0)
    .option pop
#else
    lw a3, 0(s3)
#endif
replacement:
#ifdef RETIRE_MULTI_LOAD
    lw a4, 4(s3)
#elif defined(RETIRE_MULTI_STORE)
    sw s6, 4(s3)
#elif defined(RETIRE_FUSED)
    addi s3, s3, 4
#else
    addi a5, zero, 1
#endif
#ifdef RETIRE_INNER
    addi s7, s7, -1
    bnez s7, fault_access
#endif
    ret
.balign 4, 0
handler:
    csrr t0, scause
#if defined(RETIRE_STORE) || defined(RETIRE_AMO)
    li t1, 15
#else
    li t1, 13
#endif
    bne t0, t1, fail
    csrr t0, stval
    bne t0, s3, fail
    csrr t0, sepc
    la t1, fault_access
    bne t0, t1, fail
    addi s4, s4, 1
    li t0, 1
    bne s4, t0, fail
    li t0, 0x40cf
#ifdef RETIRE_MMIO
    li t0, 0x3d0000c7
#endif
    sw t0, 0(s1)
    la t0, replacement
#ifdef RETIRE_SFENCE
    li t1, 0x20000
    add t0, t0, t1
#endif
    li t1, 0x00200793         /* addi a5, zero, 2 */
#ifdef RETIRE_MULTI_LOAD
    li t1, 0x00200713         /* addi a4, zero, 2 */
#endif
#ifdef RETIRE_FUSED
    li t1, 0x00898993         /* addi s3, s3, 8 */
#endif
    sh t1, 0(t0)             /* the RVC variant places this at PC % 4 == 2 */
    srli t1, t1, 16
    sh t1, 2(t0)
#ifdef RETIRE_SFENCE
    la t0, code_leaves
    srli t0, t0, 12
    slli t0, t0, 10
    ori t0, t0, 1
    sw t0, 0(s0)
    sfence.vma              /* same SATP, new code mapping */
#else
    fence.i
    sfence.vma s3
#endif
    sret
#include "system-exit.inc"
success: .asciz "SYSTEM native retire OK\n"
failure: .asciz "SYSTEM native retire FAIL\n"
.balign 4096
root: .space 4096
leaves: .space 4096

#ifdef RETIRE_SFENCE
code_leaves: .space 4096
#endif
