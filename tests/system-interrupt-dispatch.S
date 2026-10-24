/* Repeated timer delivery must interrupt learned/compiled chains without a
 * CSR or helper forcing dispatch. Check live registers and nested traps.
 */
#ifndef INTERRUPT_COUNT
#define INTERRUPT_COUNT 128
#endif
#ifndef INTERRUPT_INTERVAL
#define INTERRUPT_INTERVAL 16384
#endif
.option norvc
.option norelax
.section .text
.globl _start
_start:
    la t0, handler
    csrw stvec, t0
    li s1, INTERRUPT_COUNT * 256 /* outer pass limit scales with long runs */
    li s2, 0                  /* timer entries */
    li s3, 0                  /* nested exceptions */
    li s4, 0                  /* interrupted loop iterations */
    li s5, 0x12345678         /* a live value across native exits */
    la s6, data
    li a0, 0
    li a1, 0
    li a6, 0
    li a7, 0x54494d45
    ecall
    li t0, 32
    csrs sie, t0
    li t0, 2
    csrs sstatus, t0
    li s7, 0                  /* outer passes */
main_loop:
    li t2, 64
inner_loop:
#ifdef INTERRUPT_INDIRECT
    jal ra, increment
#else
    addi s4, s4, 1
#endif
    addi t2, t2, -1
    bnez t2, inner_loop
    addi s7, s7, 1
    nop                       /* no forced dispatch boundary */
    li t3, INTERRUPT_COUNT
    bgeu s2, t3, loop_done
    addi s1, s1, -1
    bnez s1, main_loop
    j fail                    /* interrupts never reached the loop */
loop_done:
    li t3, 2
    csrc sstatus, t3
    slli t0, s7, 6
    bne s4, t0, fail          /* every inner pass ran exactly 64 times */
    li t0, 0x12345678
    bne s5, t0, fail
    bne s2, s3, fail
    la a0, success
    j print
#ifdef INTERRUPT_INDIRECT
increment:
    addi s4, s4, 1
    ret
#endif
.balign 4
handler:
    csrr t0, scause
    li t1, 4
    beq t0, t1, nested
    li t1, 0x80000005
    bne t0, t1, fail
    addi s2, s2, 1
    /* A resumed-code starvation regression must fail, not trap forever. */
    li t0, INTERRUPT_COUNT * 2 + 256
    bgtu s2, t0, fail
    csrr s8, sepc
    csrr s9, sstatus
    andi t0, s9, 0x100
    beqz t0, fail             /* a trap from S-mode must record SPP = S */
    addi t0, s6, 1
nested_access:
    lw t1, 0(t0)
    csrr t0, sstatus
    andi t0, t0, 2
    bnez t0, fail             /* nested SRET must leave SIE clear */
#ifdef INTERRUPT_LONG_HANDLER
    /* Exceed a dispatch slice, then re-arm so the next poll traps again.
     * The resumed guest must still retire a block between handler entries.
     */
    .rept 256
    nop
    .endr
#endif
    csrw sepc, s8
    csrw sstatus, s9
    rdtime a0
    rdtimeh a1
    li t0, INTERRUPT_INTERVAL
    add t0, a0, t0
    sltu t1, t0, a0
    add a1, a1, t1
    mv a0, t0
    li a6, 0
    li a7, 0x54494d45
    ecall
    sret
nested:
    csrr t0, sepc
    la t1, nested_access
    bne t0, t1, fail
    addi s3, s3, 1
    addi t0, t0, 4
    csrw sepc, t0
    sret
#include "system-exit.inc"
success: .asciz "SYSTEM interrupt dispatch OK\n"
failure: .asciz "SYSTEM interrupt dispatch FAIL\n"
.balign 4
data: .word 0
