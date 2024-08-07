fib:
    li a5, 1
    bleu a0, a5, .L3
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)
    sw s1, 4(sp)
    mv s0, a0
    addi a0, a0, -1
    la t0, fib
    jalr ra, 0(t0)
    mv s1, a0
    addi a0, s0, -2
    la t0, fib
    jalr ra, 0(t0)
    add a0, s1, a0
    lw ra, 12(sp)
    lw s0, 8(sp)
    lw s1, 4(sp)
    addi sp, sp, 16
    jr ra
.L3:
    li a0, 1
    ret
.LC0:
    .string "%d\n"
    .text
    .align 1
    .globl main
    .type main, @function
main:
    addi sp, sp, -16
    sw ra, 12(sp)
    li a0, 42
    call fib
    mv a1, a0
    lui a0, %hi(.LC0)
    addi a0, a0, %lo(.LC0)
    call printf
    li a0, 0
    lw ra, 12(sp)
    addi sp, sp, 16
    jr ra
