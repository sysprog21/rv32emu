/* An interrupt handler that takes a page fault before returning, and re-arms
 * the timer to fire at once, must still let the interrupted code advance
 * between interrupts: only the return to that code grants it a dispatch.
 * With INTERRUPT_SWITCH, the handler resumes a second task instead, as a
 * scheduler would, and that task must advance just the same.
 */
#ifndef INTERRUPT_COUNT
#define INTERRUPT_COUNT 1000
#endif
.option norvc
.option norelax
.section .text
.globl _start
_start:
    la t0, root
    li t1, 0xcf               /* VA 0-4 MiB: this kernel, identity */
    sw t1, 0(t0)
    li t1, 0x3d0000cf         /* the UART; VA 4-8 MiB stays unmapped */
    li t2, 3904
    add t2, t2, t0
    sw t1, 0(t2)
    srli t0, t0, 12
    li t1, 0x80000000
    or t0, t0, t1
    csrw satp, t0
    sfence.vma

    la t0, trap
    csrw stvec, t0
    li s2, 0                  /* interrupts taken */
    li s3, 0                  /* page faults taken */
    li s4, 0                  /* main-loop iterations */
    jal ra, arm_now
    li t0, 32
    csrs sie, t0
    li t0, 2
    csrs sstatus, t0
1:
    addi s4, s4, 1
    li t0, INTERRUPT_COUNT
    bltu s2, t0, 1b
done:
    li t0, 2
    csrc sstatus, t0
    bne s3, s2, fail          /* every handler took its page fault */
    li t0, INTERRUPT_COUNT
    bltu s4, t0, fail         /* the interrupted loop was starved */
    la a0, success
    j print

#ifdef INTERRUPT_SWITCH
task:
    addi s4, s4, 1
    li t0, INTERRUPT_COUNT
    bltu s2, t0, task
    j done
#endif

/* Arm the timer to fire as soon as possible. */
arm_now:
    li a0, 0
    li a1, 0
    li a6, 0
    li a7, 0x54494d45         /* SBI TIME set_timer */
    ecall
    ret

.balign 4
trap:
    csrr t0, scause
    li t1, 13
    beq t0, t1, fault
    li t1, 0x80000005
    bne t0, t1, fail
    addi s2, s2, 1
    csrr s8, sepc
    csrr s9, sstatus
    li t0, 0x400000
fault_access:
    lw t1, 0(t0)              /* faults; the fault handler skips it */
#ifdef INTERRUPT_SWITCH
    la s8, task               /* every return resumes the second task */
#endif
    csrw sepc, s8
    csrw sstatus, s9
    li t0, INTERRUPT_COUNT
    bgeu s2, t0, 2f
    jal ra, arm_now
    sret
2:
    li a0, -1                 /* done: disarm the timer */
    li a1, -1
    li a6, 0
    li a7, 0x54494d45
    ecall
    sret
fault:
    csrr t0, sepc
    la t1, fault_access
    bne t0, t1, fail
    addi s3, s3, 1
    addi t0, t0, 4
    csrw sepc, t0
    sret

#include "system-exit.inc"
success: .asciz "SYSTEM interrupt fault OK\n"
failure: .asciz "SYSTEM interrupt fault FAIL\n"
.balign 4096
root:
    .space 4096
