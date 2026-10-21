/* Resolve faults and retry the exact instruction through interpreted warmup
 * and T1 execution. Stores and AMOs must commit once; loads must not expose
 * placeholder values. An unmapped code page must resume via its new mapping.
 * First, SRET must restore SIE from SPIE, and a trap from U-mode must record
 * SPP = U so the handler's SRET returns there. */
.option norvc
.option norelax
.section .text
.globl _start
_start:
    li t0, 0x100              /* SPP = S: the SRET below stays in S-mode */
    csrs sstatus, t0
    li t0, 0x20               /* SPIE = 0 */
    csrc sstatus, t0
    li t0, 2                  /* SIE = 1 */
    csrs sstatus, t0
    la t0, sret_done
    csrw sepc, t0
    sret
sret_done:
    csrr t0, sstatus
    andi t0, t0, 2
    bnez t0, fail             /* SIE must take SPIE = 0 */
    la t0, user_trap
    csrw stvec, t0
    li t0, 0x100              /* SPP = U */
    csrc sstatus, t0
    la t0, user_code
    csrw sepc, t0
    li a7, 0                  /* not a host-serviced call */
    sret
user_code:
    ecall                     /* traps to S-mode with scause 8 */
    j fail
.balign 4
user_trap:
    csrr t0, scause
    li t1, 8
    bne t0, t1, fail
    csrr t0, sepc
    la t1, user_code
    bne t0, t1, fail
    csrr t0, sstatus
    andi t1, t0, 0x100
    bnez t1, fail             /* SPP must record U-mode */
    andi t1, t0, 0x22
    li t2, 0x20
    bne t1, t2, fail          /* SPIE records U-mode SIE = 1; SIE is clear */
    /* Every handler ends with SRET; return to S-mode to continue. */
    li t0, 0x100
    csrs sstatus, t0
    la t0, user_done
    csrw sepc, t0
    sret
user_done:
    csrr t0, sstatus
    andi t0, t0, 0x100
    bnez t0, fail             /* SRET must clear SPP */
    la t0, handler
    csrw stvec, t0
    la s0, root
    li t0, 0xcf
    sw t0, 0(s0)
    la s1, leaves
    srli t0, s1, 12
    slli t0, t0, 10
    ori t0, t0, 1
    sw t0, 4(s0)
    li t0, 0x3d0000cf
    li t1, 3904
    add t1, s0, t1
    sw t0, 0(t1)
    li s2, 0x10000
    li s3, 0x400000
    li s4, 3000
    li s5, 0
    li s6, 0x12345678
    sw s6, 0(s2)
    srli t0, s0, 12
    li t1, 0x80000000
    or t0, t0, t1
    csrw satp, t0
    sfence.vma
loop:
    sw zero, 0(s1)
    sfence.vma s3
    la s10, retry_load
    li s11, 13
retry_load:
    lw a3, 0(s3)
    bne a3, s6, fail
    /* Readable but not writable: a write must fault before committing. */
    li t0, 0x4043
    sw t0, 0(s1)
    sfence.vma s3
    la s10, retry_store
    li s11, 15
    lw s9, 0(s2)              /* the faulting store must leave this */
    addi s6, s6, 1
retry_store:
    sw s6, 0(s3)
    lw t0, 0(s2)
    bne t0, s6, fail
    li t0, 0x4043
    sw t0, 0(s1)
    sfence.vma s3
    la s10, retry_amo
    li a4, 1
    lw s9, 0(s2)              /* the faulting AMO must leave this */
retry_amo:
    amoadd.w a3, a4, (s3)
    bne a3, s6, fail
    addi s6, s6, 1
    lw t0, 0(s2)
    bne t0, s6, fail
    addi s4, s4, -1
    bnez s4, loop
    li t0, 9000
    bne s5, t0, fail
    /* addi a3, zero, 17; jalr zero, ra, 0 at physical 0x11000. */
    li t0, 0x11000
    li t1, 0x01100693
    sw t1, 0(t0)
    li t1, 0x00008067
    sw t1, 4(t0)
    li s11, 12
    li s3, 0x401000
    li t0, 0x401000
    jalr ra, t0, 0
    li t0, 17
    bne a3, t0, fail
    li t0, 9001
    bne s5, t0, fail
    la a0, success
    j print
.balign 4
handler:
    csrr t0, scause
    bne t0, s11, fail
    csrr t0, stval
    bne t0, s3, fail
    li t0, 12
    beq s11, t0, map_code
    csrr t0, sepc
    bne t0, s10, fail
    li t1, 15
    bne s11, t1, 1f
    lw t0, 0(s2)
    bne t0, s9, fail          /* nothing committed before the retry */
1:
    li t0, 0x40cf
    sw t0, 0(s1)
    j mapped
map_code:
    csrr t0, sepc
    bne t0, s3, fail
    li t0, 0x44cb
    sw t0, 4(s1)
mapped:
    sfence.vma s3
    addi s5, s5, 1
    sret
#include "system-exit.inc"
success: .asciz "SYSTEM trap resume OK\n"
failure: .asciz "SYSTEM trap resume FAIL\n"
.balign 4096
root: .space 4096
leaves: .space 4096
