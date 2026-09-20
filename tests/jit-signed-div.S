/* RV32M signed division under tier-1 code generation.
 *
 * The x86-64 emitter selects the divide instruction from the opcode class and
 * used to pick the unsigned form for every caller, so DIV and REM returned the
 * unsigned quotient whenever an operand was negative. The operands arrive
 * sign-extended to 64 bits, which is what lets the signed divide run at that
 * width: INT_MIN / -1 stays representable there and cannot raise #DE.
 *
 * Load the operands from memory so constant folding cannot answer for the
 * generated code, and run the loop long enough to be compiled.
 */
    .option norvc
    .text
    .global _start

    /* \idx selects an operand pair; \want is the expected 32-bit result. */
    .macro chk op, idx, want
    la   t4, operands
    lw   t0, (\idx * 8)(t4)
    lw   t1, (\idx * 8 + 4)(t4)
    \op  t2, t0, t1
    li   t3, \want
    bne  t2, t3, fail
    .endm

_start:
    li s1, 0x5a5a1234
    sw s1, 0(sp)
    li s0, 20000
loop:
    /* Signed: every sign combination, plus the two architectural specials. */
    chk div, 0, -3              /* -7 /  2 */
    chk div, 1, -3              /*  7 / -2 */
    chk div, 2, 3               /* -7 / -2 */
    chk div, 3, -2147483648     /* INT_MIN / -1 overflows to INT_MIN */
    chk div, 4, -1              /* x / 0 is -1 */
    chk rem, 0, -1
    chk rem, 1, 1
    chk rem, 2, -1
    chk rem, 3, 0               /* INT_MIN % -1 is 0 */
    chk rem, 4, -1              /* x % 0 is x */

    /* Unsigned forms must keep treating the same bits as unsigned. */
    chk divu, 0, 2147483644
    chk remu, 0, 1
    chk mulh, 5, -1
    chk mulhsu, 6, -1
    chk mulhu, 6, 1
    chk mul, 0, -14

    /* A negative quotient used straight as a memory base, with no spill in
     * between to reload it. The stack sits above 0x80000000, so sp is
     * negative as int32 and sp*2 / 2 reproduces it through DIV: if the
     * result stays sign-extended in its host register the load addresses
     * outside the guest mapping.
     */
    slli t0, sp, 1
    li t1, 2
    div t2, t0, t1
    lw t3, 0(t2)
    bne t3, s1, fail

    addi s0, s0, -1
    bnez s0, loop

    li a0, 1
    la a1, passed
    li a2, 23
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
    .ascii "JIT signed division OK\n"
    .balign 4
operands:
    .word -7, 2
    .word 7, -2
    .word -7, -2
    .word -2147483648, -1
    .word -1, 0
    .word -65536, 65536
    .word -1, 2
