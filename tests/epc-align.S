/* Exception PCs hold instruction addresses. Write mepc with both low bits set:
 * it reads back with bit 0 clear, and bit 1 clear as well without C, which
 * misa reports. Do the same for sepc, and through the set and clear forms,
 * which take a different write path. MRET then resumes at that address.
 */
.option norvc
.option norelax
.text
.globl _start
_start:
    la t0, back
    ori t1, t0, 3
    csrw mepc, t1
    csrr t2, mepc
    csrr t3, misa
    andi t3, t3, 1 << 2 /* C */
    andi t4, t1, ~1
    bnez t3, 1f
    andi t4, t1, ~3
1:
    bne t2, t4, fail
    /* sepc masks like mepc, whichever instruction writes it */
    csrw sepc, t1
    csrr t2, sepc
    bne t2, t4, fail
    csrw sepc, t0
    csrs sepc, t1 /* sets the low bits again */
    csrr t2, sepc
    bne t2, t4, fail
    csrw mepc, t0
    csrs mepc, t1
    csrr t2, mepc
    bne t2, t4, fail
    /* MRET returns to the privilege in mstatus.MPP: stay in machine mode.
     * Without C, return through the masked value itself.
     */
    li t5, 3 << 11
    csrs mstatus, t5
    bnez t3, 2f
    csrw mepc, t1
    j 3f
2:
    csrw mepc, t0
3:
    mret
    j fail
back:
    li a0, 1
    la a1, passed
    li a2, 17
    li a7, 64
    ecall
    li a0, 0
    j exit
fail:
    li a0, 1
exit:
    li a7, 93
    ecall

    .section .rodata
passed:
    .ascii "epc alignment OK\n"
