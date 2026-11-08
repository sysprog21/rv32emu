/* Install a privileged trap handler that was absent from the original ELF. */
    .option norvc
    .text
    .global _start
_start:
    la t0, generated_handler
    li t1, 0x00148493       /* addi s1, s1, 1 */
    sw t1, 0(t0)
    li t1, 0x341022f3       /* csrrs t0, mepc, zero */
    sw t1, 4(t0)
    li t1, 0x00428293       /* addi t0, t0, 4 */
    sw t1, 8(t0)
    li t1, 0x34129073       /* csrrw zero, mepc, t0 */
    sw t1, 12(t0)
    li t1, 0x30200073       /* mret */
    sw t1, 16(t0)
    la t2, generated_install
    li t1, 0x30529073       /* csrrw zero, mtvec, t0 */
    sw t1, 0(t2)
    fence.i
    la t0, generated_handler
    jal ra, generated_install
    la s0, data + 1
    li s1, 0
    li s2, 6000
loop:
    lw a0, 0(s0)
    addi s2, s2, -1
    bnez s2, loop
    li t1, 6000
    bne s1, t1, fail
    li a0, 1
    la a1, msg
    li a2, 26
    li a7, 64
    ecall
    li a0, 0
    li a7, 93
    ecall
fail:
    li a0, 1
    li a7, 93
    ecall
    .balign 4
generated_install:
    nop
    ret
generated_handler:
    nop
    nop
    nop
    nop
    nop
    .data
    .balign 4
data:
    .word 0x01020304
    .word 0x05060708
    .section .rodata
msg:
    .asciz "user FENCE.I alignment OK\n"
