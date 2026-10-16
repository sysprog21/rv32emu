    .text
    .global _start
_start:
    /* Schedule a distant timer event, then enable STIE and SSIE. */
    li   a0, 65536
    li   a1, 0
    li   a6, 0
    li   a7, 0x54494d45
    ecall

    li   t0, 0x22
    csrw sie, t0
    li   t0, 2
    csrs sip, t0
    csrc sstatus, t0

    /* Verify the pending interrupt is enabled while global SIE is clear. */
    csrr t1, sstatus
    andi t1, t1, 2
    bnez t1, fail
    csrr t1, sie
    andi t1, t1, 0x22
    li   t0, 0x22
    bne  t1, t0, fail
    csrr t1, sip
    andi t1, t1, 2
    beqz t1, fail
    wfi

    /* SSIP is locally enabled, so WFI must not jump to the timer compare. */
    rdcycle t1
    li   t0, 65536
    bgeu t1, t0, fail

    /* A disabled SSIP does not block the timer fast-forward path. */
    li   t0, 0x20
    csrw sie, t0
    li   a0, 131072
    li   a1, 0
    li   a6, 0
    li   a7, 0x54494d45
    ecall
    wfi
    rdcycle t1
    li   t0, 131072
    bltu t1, t0, fail

    /* With no pending interrupt, the enabled timer can still wake WFI. */
    li   t0, 2
    csrc sip, t0
    li   a0, 196608
    li   a1, 0
    li   a6, 0
    li   a7, 0x54494d45
    ecall
    wfi
    rdcycle t1
    li   t0, 196608
    bltu t1, t0, fail

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
    .ascii "WFI pending interrupt wakeup OK\n"
passed_end:
