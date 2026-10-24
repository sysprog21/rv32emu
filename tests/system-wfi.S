/* WFI must return once the timer comes due, whether or not interrupts are
 * globally enabled, and an idle hart must not spin through the wait: the loop
 * below may run only a small fraction of the cycles it waits for.
 */
#ifndef WFI_WAIT
#define WFI_WAIT 200000000
#endif
.option norvc
.option norelax
.section .text
.globl _start
_start:
    la t0, handler
    csrw stvec, t0
    li s2, 0                  /* timer interrupts taken */
    li t0, 32
    csrs sie, t0              /* STIE; SIE stays clear for the first wait */

    /* Wait with interrupts globally disabled: WFI still wakes. */
    jal ra, arm_timer
    li s3, 0                  /* WFI loop iterations */
1:
    wfi
    addi s3, s3, 1
    csrr t0, sip
    andi t0, t0, 32
    beqz t0, 1b
    jal ra, check_deadline
    li t0, WFI_WAIT / 100
    bgeu s3, t0, fail         /* the hart spun instead of skipping */
    bnez s2, fail

    /* Wait with interrupts enabled: the handler takes the interrupt. */
    jal ra, arm_timer
    li s3, 0
    li t0, 2
    csrs sstatus, t0
2:
    wfi
    addi s3, s3, 1
    beqz s2, 2b
    li t0, 2
    csrc sstatus, t0
    jal ra, check_deadline
    li t0, WFI_WAIT / 100
    bgeu s3, t0, fail
    li t0, 1
    bne s2, t0, fail
    la a0, success
    j print

/* Arm the timer WFI_WAIT cycles ahead and keep the deadline in s4:s5. */
arm_timer:
    rdtimeh t1
    rdtime t0
    rdtimeh t2
    bne t1, t2, arm_timer
    li t2, WFI_WAIT
    add a0, t0, t2
    sltu t3, a0, t0
    add a1, t1, t3
    mv s4, a0
    mv s5, a1
    li a6, 0
    li a7, 0x54494d45         /* SBI TIME set_timer */
    ecall
    ret

/* Fail unless time has reached the deadline in s4:s5. */
check_deadline:
    rdtimeh t1
    rdtime t0
    rdtimeh t2
    bne t1, t2, check_deadline
    bltu t1, s5, fail
    bne t1, s5, 3f
    bltu t0, s4, fail
3:
    ret

.balign 4
handler:
    csrr t0, scause
    li t1, 0x80000005
    bne t0, t1, fail
    addi s2, s2, 1
    li a0, -1                 /* disarm the timer */
    li a1, -1
    li a6, 0
    li a7, 0x54494d45
    ecall
    sret

#include "system-exit.inc"
success: .asciz "SYSTEM WFI OK\n"
failure: .asciz "SYSTEM WFI FAIL\n"
