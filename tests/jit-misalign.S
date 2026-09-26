/*
 * Misaligned loads and stores under tier-1 code generation.
 *
 * Without -m, the interpreter raises an address-misaligned exception for every
 * halfword or word access that is not naturally aligned. With no trap vector
 * installed, the user-mode default handler emulates the access and resumes
 * after it; with one installed, the guest handler runs instead. Tier-1 code
 * used to perform such accesses directly, so a guest handler was never entered.
 * The program uses the machine-mode trap vector, so it runs only in user-mode
 * builds.
 *
 * Each phase runs its loop long enough to be compiled. Phase 1 checks the
 * values produced by default emulation; phase 2 installs mtvec and counts the
 * exceptions. The accesses cover plain, fused (consecutive LW and SW, LW with a
 * post-increment, LUI with LW) and, when assembled for rv32ic, compressed
 * forms, which is why the misaligned bases live in s0 and s1. Phase 2 also
 * checks that a base proven aligned stops counting as aligned once written.
 *
 * Built with NO_TRAP_VECTOR, the program is phase 1 alone and has no CSR
 * instruction, so the emulator knows no trap vector can be installed and JIT
 * code leaves the accesses to the host, which must produce the same values.
 */
    .text
    .global _start

    .equ ITER, 20000

_start:
    la   s0, buf + 1            /* odd: every halfword and word misaligned */
    la   s1, buf + 2            /* halfword aligned, word misaligned */

    /* Phase 1: no trap vector, so each access is emulated in place. */
    li   s2, ITER
1:
    lw   a0, 0(s0)              /* C.LW when compressed */
    li   t0, 0x55443322
    bne  a0, t0, fail
    lh   a1, 4(s0)
    li   t0, 0x7766
    bne  a1, t0, fail
    lhu  a2, 1(s0)
    li   t0, 0x4433
    bne  a2, t0, fail

    lw   a3, 0(s1)              /* fused pair: consecutive LW */
    lw   a4, 4(s1)
    li   t0, 0x66554433
    bne  a3, t0, fail
    li   t0, 0xaa998877
    bne  a4, t0, fail

    mv   t1, s0                 /* fused LW + ADDI post-increment */
    lw   a5, 0(t1)
    addi t1, t1, 4
    li   t0, 0x55443322
    bne  a5, t0, fail
    sub  t1, t1, s0
    li   t0, 4
    bne  t1, t0, fail

    lui  t2, %hi(buf + 3)       /* fused LUI + LW at a constant address */
    lw   a6, %lo(buf + 3)(t2)
    li   t0, 0x77665544
    bne  a6, t0, fail
    lui  t0, %hi(buf + 3)       /* the fused LUI still writes its rd */
    bne  t2, t0, fail

    la   t3, scratch + 1        /* fused pair: consecutive SW, then SH */
    li   t0, 0x04030201
    li   t4, 0x08070605
    sw   t0, 0(t3)
    sw   t4, 4(t3)
    li   t0, 0x0a09
    sh   t0, 8(t3)
    la   t3, scratch
    lw   t0, 0(t3)
    li   t4, 0x03020100
    bne  t0, t4, fail
    lw   t0, 4(t3)
    li   t4, 0x07060504
    bne  t0, t4, fail
    lw   t0, 8(t3)
    li   t4, 0x000a0908
    bne  t0, t4, fail
    sw   zero, 0(t3)
    sw   zero, 4(t3)
    sw   zero, 8(t3)

    addi s2, s2, -1
    bnez s2, 1b

#ifndef NO_TRAP_VECTOR
    /* Phase 2: a trap vector is installed, so each access must reach it. */
    la   t0, handler
    csrw mtvec, t0
    li   s10, 0                 /* exceptions taken */
    li   s11, 0                 /* causes seen, one bit per cause */
    la   s3, buf
    li   s2, ITER
2:
    li   a0, -1                 /* a trapped load must not write rd */
    lw   a0, 0(s0)
    li   t0, -1
    bne  a0, t0, fail
    lw   a3, 0(s1)
    lw   a4, 4(s1)
    lh   a1, 4(s0)
    lui  t2, %hi(buf + 3)
    lw   a6, %lo(buf + 3)(t2)
    la   t3, scratch + 1
    sw   t0, 0(t3)
    sh   t0, 8(t3)
    sw   a0, 4(s0)              /* C.SW when compressed */

    /* An aligned access proves its base aligned for the rest of the block,
     * until the base changes: both second accesses below must still trap.
     */
    lw   a5, 0(s3)
    addi s3, s3, 1
    lw   a5, 0(s3)
    addi s3, s3, -1
    lw   a5, 0(sp)              /* C.LWSP when compressed */
    addi sp, sp, 1              /* C.ADDI when compressed */
    lw   a5, 0(sp)
    addi sp, sp, -1

    addi s2, s2, -1
    bnez s2, 2b

    csrw mtvec, zero
    li   t0, ITER * 10
    bne  s10, t0, fail
    li   t0, (1 << 4) | (1 << 6) /* load and store address misaligned */
    bne  s11, t0, fail
    la   t3, scratch
    lw   t0, 0(t3)
    bnez t0, fail               /* a trapped store must not write memory */
#endif

    li   a0, 1
    la   a1, passed
    li   a2, 27
    li   a7, 64
    ecall
    li   a0, 0
    j    exit
fail:
    li   a0, 1
exit:
    li   a7, 93
    ecall

#ifndef NO_TRAP_VECTOR
    /* Count the exception, record its cause, and skip the instruction. */
    .balign 4
handler:
    addi s10, s10, 1
    csrr t5, mcause
    li   t6, 1
    sll  t6, t6, t5
    or   s11, s11, t6
    csrr t5, mepc
    lhu  t6, 0(t5)
    andi t6, t6, 3
    addi t5, t5, 2
    li   t4, 3
    bne  t6, t4, 3f
    addi t5, t5, 2
3:
    csrw mepc, t5
    mret
#endif

    .data
    .balign 4
buf:
    .byte 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
    .byte 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00
scratch:
    .word 0, 0, 0, 0

    .section .rodata
passed:
    .string "JIT misaligned accesses OK\n"
