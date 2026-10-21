/* Timer handlers may run through the dispatcher and T1. Verify repeated
 * returns to a compiled loop, live registers, and a synchronous nested trap.
 * Interrupts are polled only between dispatches, and neither the interpreter
 * nor T1 leaves a chained loop to poll them, so each outer pass reads a CSR,
 * which ends the native run and lets a pending timer interrupt in. The loop
 * stops after enough interrupts, or fails once its pass limit is reached.
 * This flat supervisor kernel needs no guest libc or firmware. */
.option norvc
.option norelax
.section .text
.globl _start
_start:
    la t0, handler
    csrw stvec, t0
    li s1, 100000             /* outer pass limit */
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
    addi s4, s4, 1
    addi t2, t2, -1
    bnez t2, inner_loop
    addi s7, s7, 1
    csrr t0, sscratch         /* return to the dispatcher */
    li t0, 6
    bgeu s2, t0, loop_done
    addi s1, s1, -1
    bnez s1, main_loop
    j fail                    /* interrupts never reached the loop */
loop_done:
    li t0, 2
    csrc sstatus, t0
    slli t0, s7, 6
    bne s4, t0, fail          /* every inner pass ran exactly 64 times */
    li t0, 0x12345678
    bne s5, t0, fail
    bne s2, s3, fail
    la a0, success
    j print
.balign 4
handler:
    csrr t0, scause
    li t1, 4
    beq t0, t1, nested
    li t1, 0x80000005
    bne t0, t1, fail
    addi s2, s2, 1
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
    csrw sepc, s8
    csrw sstatus, s9
    rdtime a0
    rdtimeh a1
    addi t0, a0, 512
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
