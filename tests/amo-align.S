#ifdef SYSTEM_MODE
#define TVEC stvec
#define CAUSE scause
#define TVAL stval
#define EPC sepc
#define RET sret
#else
#define TVEC mtvec
#define CAUSE mcause
#define TVAL mtval
#define EPC mepc
#define RET mret
#endif

    .option norvc
    .text
    .global _start

    .macro check op
    li a0, 0x5a5a5a5a
    \op a0, s1, (s0)
    li t0, 0x5a5a5a5a
    bne a0, t0, fail
    \op zero, zero, (s0)
    .endm

_start:
    la t0, handler
    csrw TVEC, t0
    li s10, 0
    li s1, 0x12345678
    la s0, words
    addi s0, s0, 1
    li s2, 3
loop:
    check amoswap.w
    check amoadd.w
    check amoxor.w
    check amoand.w
    check amoor.w
    check amomin.w
    check amomax.w
    check amominu.w
    check amomaxu.w

    mv s4, s0
    amoadd.w s0, s1, (s0)
    bne s0, s4, fail
    li a0, 0x5a5a5a5a
    amoswap.w a0, a0, (s0)
    li t0, 0x5a5a5a5a
    bne a0, t0, fail

    addi s0, s0, 1
    addi s2, s2, -1
    bnez s2, loop
    li t0, 60
    bne s10, t0, fail
    la t0, words
    lw t1, 0(t0)
    li t2, 0x11223344
    bne t1, t2, fail
    lw t1, 4(t0)
    li t2, 0x55667788
    bne t1, t2, fail
    csrw TVEC, zero
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

    .balign 4
handler:
    csrr t5, CAUSE
    li t6, 6
    bne t5, t6, fail
    csrr t5, TVAL
    bne t5, s0, fail
    addi s10, s10, 1
    csrr t5, EPC
    addi t5, t5, 4
    csrw EPC, t5
    RET

    .section .rodata
passed:
    .ascii "AMO alignment OK\n"
    .data
    .balign 4
words:
    .word 0x11223344, 0x55667788
