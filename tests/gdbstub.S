/* Guest for .ci/gdbstub-test.sh. A run of nops takes the instruction
 * breakpoints and a single step, a copy loop lets the debugger's register and
 * memory writes show up in guest memory, then one access per path that a data
 * watchpoint must observe. Every stop has a label, so the test checks where the
 * debugger stopped by name rather than by address.
 * Build with -DWITH_RVV for the vector accesses (needs an EXT_V emulator).
 */
    .option norvc
    .text
    .global _start
_start:
    /* The linker relaxes la into gp-relative addressing: set gp up first */
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    /* Register read: the debugger expects this value in t4 at bp_1 */
    li t4, 0x600d
    /* Instruction breakpoints, then a stepi off the last one */
bp_1:
    nop
bp_2:
    nop
bp_3:
    nop
after_stepi:
    /* Register and memory writes: at after_stepi the debugger sets t3 and
     * mem_in; the guest stores both so they can be read back at after_copy */
    la t0, reg_out
    sw t3, 0(t0)
    la t0, mem_in
    lw t5, 0(t0)
    la t0, mem_out
    sw t5, 0(t0)
after_copy:
    /* 1. Scalar store: watch w_scalar */
    la t0, w_scalar
    li t1, 1
    sw t1, 0(t0)
after_sw:
    /* 2. Scalar load: rwatch r_scalar */
    la t0, r_scalar
    lw t1, 0(t0)
after_lw:
    /* 3. Misaligned store spanning ms_lo+2..ms_lo+5: watch ms_hi */
    la t0, ms_lo
    li t1, 0x11223344
    sw t1, 2(t0)
after_misaligned_sw:
    /* 4. Misaligned load spanning ml_lo+2..ml_lo+5: rwatch ml_hi */
    la t0, ml_lo
    lw t1, 2(t0)
after_misaligned_lw:
    /* 5. AMO, a read then a write of one word: awatch amo_var */
    la t0, amo_var
    li t1, 5
    amoadd.w t2, t1, (t0)
after_amo:
#ifdef WITH_RVV
    /* 6. Vector load of vsrc[0..3]: rwatch vsrc1 */
    li a0, 4
    vsetvli t0, a0, e32, m1, ta, ma
    la t0, vsrc
    vle32.v v1, (t0)
after_vle:
    /* 7. Vector store to vdst[0..3]: watch vdst1 */
    la t0, vdst
    vse32.v v1, (t0)
after_vse:
#endif
    li a0, 0
    li a7, 93
    ecall

    .data
    .balign 8
reg_out:
    .word 0
mem_in:
    .word 0
mem_out:
    .word 0
w_scalar:
    .word 0
r_scalar:
    .word 0x12345678
ms_lo:
    .word 0
ms_hi:
    .word 0
ml_lo:
    .word 0
ml_hi:
    .word 0x5a5a5a5a
amo_var:
    .word 0
    .balign 16
vsrc:
    .word 1
vsrc1:
    .word 2, 3, 4
vdst:
    .word 0
vdst1:
    .word 0, 0, 0
