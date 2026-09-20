/* Warm two indirect targets, then exercise a previously unseen third target.
 * All calls use an odd base plus an offset and overwrite their base register
 * with the return address. Targets occupy different direct-mapped BHT slots.
 *
 * The result is checked in every configuration, but only jit_defconfig reaches
 * the tier-1 indirect guards: bht_should_translate() declines unconditionally
 * once T2C is configured, so a tiered build answers this program from the
 * dispatcher instead. Reproducing a guard regression therefore requires a
 * non-T2C JIT build.
 */
    .text
    .global _start
    .option norvc
_start:
    li s0, 20000
    li s1, 0
    la s2, targets
loop:
    li t1, 4000
    bleu s0, t1, late_target
    andi t1, s0, 1
    slli t1, t1, 2
    add t1, s2, t1
    lw t0, 0(t1)
    j call
late_target:
    lw t0, 8(s2)
call:
    addi t0, t0, -3
    jalr t0, 4(t0)
    addi s0, s0, -1
    bnez s0, loop
    li t1, 52000
    bne s1, t1, fail
    li a0, 1
    la a1, passed
    li a2, 24
    li a7, 64
    ecall
    li a0, 0
    j exit
fail:
    li a0, 1
exit:
    li a7, 93
    ecall
    .balign 64
target_one:
    addi s1, s1, 1
    jalr zero, 0(t0)
target_two:
    addi s1, s1, 3
    jalr zero, 0(t0)
target_three:
    addi s1, s1, 5
    jalr zero, 0(t0)
    .section .rodata
    .balign 4
targets:
    .word target_one, target_two, target_three
passed:
    .ascii "JIT indirect targets OK\n"
