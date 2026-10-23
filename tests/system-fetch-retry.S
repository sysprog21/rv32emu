/* Return from the first instruction page fault without repairing its PTE;
 * the second fault repairs it. Entered through the dispatcher, each fault is
 * retried from the trap vector, and afterwards a compatible loop must still
 * compile while a CSR-containing loop must not. With FETCH_NESTED, a handler
 * the emulator runs nested calls the loops, so the faults nest too and the
 * fetch retries in place after each one.
 */
#ifdef FETCH_NESTED
#define LOOP_COUNT 16
/* Run each loop from an illegal-instruction handler, which runs nested. */
#define CALL_LOOP .word 0xffffffff
#else
#define LOOP_COUNT 65536
#define CALL_LOOP jalr ra, s8, 0
#endif
.option norvc
.option norelax
.text
.globl _start
_start:
    la t0, handler
    csrw stvec, t0
    la t0, root
    li t1, 0xcf
    sw t1, 0(t0)              /* identity-map low RAM */
    la t1, leaves
    srli t1, t1, 2
    ori t1, t1, 1
    sw t1, 4(t0)
    li t1, 0x3d0000cf        /* UART megapage */
    li t2, 3904
    add t2, t0, t2
    sw t1, 0(t2)
    srli t0, t0, 12
    li t1, 0x80000000
    or t0, t0, t1
    csrw satp, t0
    sfence.vma

    li s8, 0x401000
    la s9, leaves
    addi s9, s9, 4
    li s10, 0x44cb          /* physical 0x11000, RX, A/D, V */
    li s11, 0
    li s4, LOOP_COUNT
    li a0, 0
    CALL_LOOP
    li t0, LOOP_COUNT
    bne a0, t0, fail
    li t0, 2
    bne s11, t0, fail

    li s8, 0x402000
    addi s9, s9, 4
    li s10, 0x48cb          /* physical 0x12000 */
    li s11, 0
    li s4, LOOP_COUNT
    li a0, 0
    CALL_LOOP
    li t0, LOOP_COUNT
    bne a0, t0, fail
    li t0, 2
    bne s11, t0, fail
    la a0, success
    j print

.balign 4
handler:
    csrr t0, scause
#ifdef FETCH_NESTED
    li t1, 2
    bne t0, t1, 2f
    csrr s7, sepc             /* the faults below overwrite both */
    csrr s5, sstatus
    mv s6, ra
    jalr ra, s8, 0
    mv ra, s6
    addi s7, s7, 4
    csrw sepc, s7
    csrw sstatus, s5
    sret
2:
#endif
    li t1, 12
    bne t0, t1, fail
    csrr t0, sepc
    bne t0, s8, fail
    csrr t0, stval
    bne t0, s8, fail
    addi s11, s11, 1
    li t0, 1
    beq s11, t0, 1f
    li t0, 2
    bne s11, t0, fail
    sw s10, 0(s9)
    sfence.vma s8
1:
    sret
#include "system-exit.inc"
success: .asciz "SYSTEM fetch retry OK\n"
failure: .asciz "SYSTEM fetch retry FAIL\n"
.set root, 0x4000
.set leaves, 0x5000
.org 0x11000, 0
compatible:
    addi a0, a0, 1
    addi s4, s4, -1
    bnez s4, compatible
    ret
.org 0x12000, 0
blocked:
    csrr s2, sscratch
    addi a0, a0, 1
    addi s4, s4, -1
    bnez s4, blocked
    ret
