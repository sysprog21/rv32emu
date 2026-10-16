    .option norvc
    .text
    .global _start
_start:
    .macro check_load operation, second
    li   s0, 0x8000
    sw   zero, 0(s0)
#if LOAD_X0_MODE == 0
    li   t0, 0x80
    sw   t0, 0(s0)
    \operation zero, 0(s0)
    j    verify\@
#else
    li   s1, 100000
    li   s2, 0
loop\@:
    \operation zero, 0(s0)
    addi s1, s1, -1
    bnez s1, loop\@
    bnez s2, verify\@
    li   s2, 1
    li   s1, 1
#if LOAD_X0_MODE == 1
    /* Change the data only after this load block has become hot. */
    li   t0, 0x80
    sw   t0, 0(s0)
#else
    /* Reuse the hot load for one destructive UART receive operation. */
    li   s0, 0xf4000000
#endif
    j    loop\@
#endif

    /* A new translation must observe the hardwired zero-register invariant. */
    .balign 4096
verify\@:
#if LOAD_X0_MODE == 2
    lbu  t0, 0(s0)
    li   t1, \second
    bne  t0, t1, fail
#endif
    .endm

    check_load lb, 'B'
    check_load lh, 'D'
    check_load lw, 'F'
    check_load lbu, 'H'
    check_load lhu, 'J'

    li   a0, 1
    la   a1, passed
    la   a2, passed_end
    sub  a2, a2, a1
    li   a7, 64
    ecall
    li   a0, 0
    j    exit
fail:
    li   a0, 1
exit:
    li   a7, 93
    ecall

    .section .rodata
passed:
    .ascii "system loads preserve x0 OK\n"
passed_end:
