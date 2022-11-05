.text
.align 4
.globl switch_context
.type switch_context, @function

/*
 * All we have to save is the callee saved registers. Everything is
 * dumped to the stack and the only thing stored in, and restored from,
 * the context is the stack pointer.
 */
 switch_context:
    /* FIXME: We assume no FP in 32-bit */
    sw ra,   0(a0)
    sw sp,   4(a0)
    sw s0,   8(a0)
    sw s1,  12(a0)
    sw s2,  16(a0)
    sw s3,  20(a0)
    sw s4,  24(a0)
    sw s5,  28(a0)
    sw s6,  32(a0)
    sw s7,  36(a0)
    sw s8,  40(a0)
    sw s9,  44(a0)
    sw s10, 48(a0)
    sw s11, 52(a0)

    lw ra,  0(a1)
    lw sp,  4(a1)
    lw s0,  8(a1)
    lw s1,  12(a1)
    lw s2,  16(a1)
    lw s3,  20(a1)
    lw s4,  24(a1)
    lw s5,  28(a1)
    lw s6,  32(a1)
    lw s7,  36(a1)
    lw s8,  40(a1)
    lw s9,  44(a1)
    lw s10, 48(a1)
    lw s11, 52(a1)
    ret  /* XXX replace with jalr t0, ra to get slightly better RAS behavior */

.size switch_context, .- switch_context

/*
 * The helper function is the first to be entered in a new context
 * and serves to call the user entry function with the correct
 * argument. The reason we need a helper is that user entry function
 * argument is not one of the saved registers.
 */
.align 4
.globl helper_context
.type helper_context, @function
helper_context:
    lw t0, 56(a1)
    lw a0, 60(a1)
    jr t0

.size helper_context, .- helper_context
