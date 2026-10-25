/* AOT replay must relocate RAM, retain XLEN wrapping, and leave an unobserved
 * argument-dependent path to the interpreter or tier-1 compiler.
 */
    .text
    .global _start
_start:
    lw s0, 0(sp)
    li t0, 3
    beq s0, t0, fenced
    addi sp, sp, -16
    li s1, 37
    c.swsp s1, 0(sp)
    c.lwsp s2, 0(sp)
    bne s1, s2, fail
    la s3, value
    sw s2, 0(s3)
    lw s4, 0(s3)
    bne s4, s1, fail
    /* -4 + 4 wraps to guest address zero, not host RAM + 4 GiB. */
    li t0, -4
    lw t1, 4(t0)
    li t0, 0
    lw t2, 0(t0)
    bne t1, t2, fail
    li t0, 1
    bne s0, t0, cold
    li s5, 20000
loop:
    addi s4, s4, 1
    addi s5, s5, -1
    bnez s5, loop
    li t0, 20037
    bne s4, t0, fail
    j done
cold:
    li s6, 123
    li s5, 100
cold_loop:
    addi s6, s6, 3
    addi s5, s5, -1
    bnez s5, cold_loop
    li t0, 423
    bne s6, t0, fail
    j done
fenced:
    .option push
    .option arch, +zifencei
    fence.i
    .option pop
done:
    li a0, 0
    j exit
fail:
    li a0, 1
exit:
    li a7, 93
    ecall
    .bss
    .balign 4
value:
    .space 4
