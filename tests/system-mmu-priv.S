/* Permission changes must protect walks, TLB hits and cached/compiled blocks.
 * All faulting accesses check their exact cause and stval, skip the operation,
 * and repeat enough times to make both successful and faulting loops hot.
 */
/* Defaults target rv32emu's flat RAM/UART. QEMU virt can run the same
 * architectural checks with PHYS_BASE=0x80400000 and UART_PA=0x10000000.
 * Link at zero for the fixed .org labels; load the raw binary at 0x80400000.
 */
#ifndef PHYS_BASE
#define PHYS_BASE 0
#endif
#ifndef UART_PA
#define UART_PA 0xf4000000
#endif
.option norvc
.option norelax
.equ USER_DELTA, 0x3fff0000 - PHYS_BASE
#ifndef MMU_PRIV_ITERATIONS
#define MMU_PRIV_ITERATIONS 10000
#endif
.equ N, MMU_PRIV_ITERATIONS
.macro USER_ADDR reg, symbol
    la \reg, \symbol
    li t0, USER_DELTA
    add \reg, \reg, t0
.endm
.macro S_LOAD addr, cause
    addi s5, s5, 1
    li s0, \addr
    li s8, \cause
    mv s9, s0
    la s10, s_load_done
    li s1, N
    li s7, 0
    jal ra, s_load
.if \cause == 0
.if (\addr == 0x40004000) || (\addr == 0x40005000)
    li t0, 0x12345678
    bne a0, t0, fail_value
.else
    bnez a0, fail_value
.endif
.endif
    jal ra, check_count
.endm
.macro S_STORE addr, cause
    addi s5, s5, 1
    li s0, \addr
    li s8, \cause
    mv s9, s0
    la s10, s_store_done
    li s1, N
    li s7, 0
    jal ra, s_store
    jal ra, check_count
.endm
.macro S_FETCH addr, cause
    addi s5, s5, 1
    li s0, \addr
    li s8, \cause
    mv s9, s0
    la s10, s_fetch_done
    li s1, N
    li s7, 0
    jal ra, s_fetch
    jal ra, check_count
.endm
.macro U_RUN entry, resume, addr, cause
    addi s5, s5, 1
    li s0, \addr
    li s8, \cause
    mv s9, s0
    USER_ADDR s10, \resume
    USER_ADDR t1, \entry
    la s11, 9f
    li s1, N
    li s7, 0
    csrw sepc, t1
    li t0, 0x100
    csrc sstatus, t0
    sret
9:
    jal ra, check_count
.endm
.text
.globl _start
_start:
    li s4, 0x4242
    la t0, handler
    csrw stvec, t0
    la t0, root
    li t1, (PHYS_BASE >> 2) | 0xcf
    sw t1, 0(t0)
    li t2, (PHYS_BASE >> 22) * 4
    add t2, t0, t2
    sw t1, 0(t2)             /* supervisor identity superpage */
    la t1, leaves
    srli t1, t1, 12
    slli t1, t1, 10
    ori t1, t1, 1
    sw t1, 1024(t0)
    li t1, (UART_PA >> 2) | 0xcf
    li t2, 3904
    add t2, t0, t2
    sw t1, 0(t2)            /* UART identity superpage */
    la t0, leaves
    li t1, (PHYS_BASE >> 2) | 0x405b
    sw t1, 0(t0)            /* U executable entry at PA 0x10000 */
    li t1, (PHYS_BASE >> 2) | 0x44d7
    sw t1, 4(t0)            /* U read/write data at PA 0x11000 */
    li t1, (PHYS_BASE >> 2) | 0x48c7
    sw t1, 8(t0)            /* S read/write data at PA 0x12000 */
    li t1, (PHYS_BASE >> 2) | 0x4c4b
    sw t1, 12(t0)           /* S executable function at PA 0x13000 */
    li t1, (PHYS_BASE >> 2) | 0x5049
    sw t1, 16(t0)           /* S execute-only data at PA 0x14000 */
    li t1, (PHYS_BASE >> 2) | 0x5459
    sw t1, 20(t0)           /* U execute-only data at PA 0x15000 */
    li t1, (PHYS_BASE >> 2) | 0x585b
    sw t1, 24(t0)           /* U executable function at PA 0x16000 */
    li t0, PHYS_BASE + 0x14000
    li t1, 0x12345678
    sw t1, 0(t0)
    li t0, PHYS_BASE + 0x15000
    sw t1, 0(t0)
    la t0, root
    srli t0, t0, 12
    li t1, 0x80000000
    or t0, t0, t1
    csrw satp, t0
    sfence.vma

    /* SUM=0: cold U data must fault. SUM=1 fills the same entries; clearing
     * SUM without a fence must revoke access on both loads and stores.
     */
    li s6, 1
    li s5, 0
    S_LOAD 0x40001000, 13
    S_STORE 0x40001000, 15
    li t0, 0x40000
    csrs sstatus, t0
    S_LOAD 0x40001000, 0
    S_STORE 0x40001000, 0
    li t0, 0x40000
    csrc sstatus, t0
    S_LOAD 0x40001000, 13
    S_STORE 0x40001000, 15
    li t0, 0x40000
    csrs sstatus, t0
    S_LOAD 0x40001000, 0
    S_STORE 0x40001000, 0
    csrc sstatus, t0

    /* MXR applies before the generic read test, on cold and cached paths. */
    li s6, 2
    li s5, 0
    sfence.vma
    S_LOAD 0x40004000, 13
    li t0, 0x80000
    csrs sstatus, t0
    S_LOAD 0x40004000, 0
    li t0, 0x80000
    csrc sstatus, t0
    S_LOAD 0x40004000, 13
    li t0, 0x80000
    csrs sstatus, t0
    S_LOAD 0x40004000, 0
    li t0, 0x80000
    csrc sstatus, t0

    /* U cannot read/write S data, cold or after S has filled the entry. */
    li s6, 3
    li s5, 0
    sfence.vma
    U_RUN user_load, user_load_done, 0x40002000, 13
    sfence.vma
    U_RUN user_store, user_store_done, 0x40002000, 15
    S_LOAD 0x40002000, 0
    U_RUN user_load, user_load_done, 0x40002000, 13
    S_STORE 0x40002000, 0
    U_RUN user_store, user_store_done, 0x40002000, 15

    /* Warm executable U code, then attempt S fetch with either SUM value. */
    li s6, 4
    li s5, 0
    U_RUN user_jump, user_jump_done, 0x40006000, 0
    S_FETCH 0x40006000, 12
    li t0, 0x40000
    csrs sstatus, t0
    S_FETCH 0x40006000, 12
    li t0, 0x40000
    csrc sstatus, t0
    sfence.vma
    S_FETCH 0x40006000, 12
    li t0, 0x40000
    csrs sstatus, t0
    sfence.vma
    S_FETCH 0x40006000, 12
    li t0, 0x40000
    csrc sstatus, t0

    /* Warm S code, including retained native code. U indirect and direct
     * calls must still fault before the cached kernel instruction executes.
     */
    li s6, 5
    li s5, 0
    S_FETCH 0x40003000, 0
    U_RUN user_jump, user_jump_done, 0x40003000, 12
    S_FETCH 0x40003000, 0
    U_RUN user_direct, user_direct_done, 0x40003000, 12
    sfence.vma
    U_RUN user_jump, user_jump_done, 0x40003000, 12
    /* The handler grants access and retries the same instruction. Context
     * changes must stop stale compiled probes without losing the destination.
     */
    li s6, 6
    li s5, 1
    li s8, 13
    li s9, 0x40001000
    li s7, 0
    li s1, N
1:
    li t0, 0x40000
    csrc sstatus, t0
    li a0, 0x4242
    lw a0, 0(s9)
    bnez a0, fail_value
    addi s1, s1, -1
    bnez s1, 1b
    jal ra, check_count
    li s5, 2
    li s9, 0x40004000
    li s7, 0
    li s1, N
1:
    li t0, 0x80000
    csrc sstatus, t0
    li a0, 0x4242
    lw a0, 0(s9)
    li t0, 0x12345678
    bne a0, t0, fail_value
    addi s1, s1, -1
    bnez s1, 1b
    jal ra, check_count
    /* SUM+MXR together, plus U-mode MXR transitions on X-only and R pages. */
    li s6, 7
    li s5, 0
    S_LOAD 0x40005000, 0
    li t0, 0xc0000
    csrc sstatus, t0
    U_RUN user_load, user_load_done, 0x40005000, 13
    li t0, 0x80000
    csrs sstatus, t0
    li s3, 0x12345678
    U_RUN user_read, user_read_done, 0x40005000, 0
    li s3, 0
    U_RUN user_read, user_read_done, 0x40001000, 0
    /* The first U store must update D before class-zero writes can hit. */
    la t0, leaves
    lw t1, 4(t0)
    andi t1, t1, -129
    sw t1, 4(t0)
    li t0, 0x40001000
    sfence.vma t0
    li s3, 0x55667788
    U_RUN user_write, user_write_done, 0x40001000, 0
    la t0, leaves
    lw t1, 4(t0)
    andi t1, t1, 128
    beqz t1, fail_value
    U_RUN user_read, user_read_done, 0x40001000, 0
    li t0, 0x80000
    csrc sstatus, t0
    U_RUN user_read, user_read_done, 0x40001000, 0
    li s3, 0x11223344
    U_RUN user_write, user_write_done, 0x40001000, 0
    U_RUN user_read, user_read_done, 0x40001000, 0
    U_RUN user_load, user_load_done, 0x40005000, 13
    li s3, 0x8067           /* ret in a readable, non-writable U code page */
    U_RUN user_read, user_read_done, 0x40006000, 0
    U_RUN user_store, user_store_done, 0x40006000, 15
    la a0, success
    j print

s_load:
    mv a0, s4
    lw a0, 0(s0)
s_load_done:
    beqz s8, 2f
    bne a0, s4, fail_value
2:
    addi s1, s1, -1
    bnez s1, s_load
    ret
s_store:
    sw zero, 0(s0)
s_store_done:
    addi s1, s1, -1
    bnez s1, s_store
    ret
s_fetch:
    mv s2, ra
1:
    jalr ra, s0, 0
s_fetch_done:
    addi s1, s1, -1
    bnez s1, 1b
    jr s2
check_count:
    beqz s8, 1f
    li t0, N
    bne s7, t0, fail_count
    ret
1:
    bnez s7, fail_count
    ret
handler:
    csrr t0, scause
    li t1, 8
    beq t0, t1, user_done
    bne t0, s8, fail_cause
    csrr t0, stval
    bne t0, s9, fail_addr
    addi s7, s7, 1
    li t0, 6
    beq s6, t0, retry_access
    csrw sepc, s10
    sret
retry_access:
    li t0, 0x40000
    li t1, 1
    beq s5, t1, 1f
    slli t0, t0, 1
1:
    csrs sstatus, t0
    sret
user_done:
    csrr t0, sstatus
    andi t0, t0, 0x100
    bnez t0, fail_mode
    li t0, 0x100
    csrs sstatus, t0
    csrw sepc, s11
    sret
fail_value:
    li s3, 1
    j fail_case
fail_count:
    li s3, 2
    j fail_case
fail_cause:
    li t0, 0xf4000000
    csrr t1, scause
    addi t1, t1, 48
    sb t1, 0(t0)
    csrr t1, sstatus
    srli t1, t1, 18
    andi t1, t1, 1
    addi t1, t1, 48
    sb t1, 0(t0)
    li t1, 10
    sb t1, 0(t0)
    csrr t2, stval
    li t1, 8
1:
    srli t3, t2, 28
    addi t3, t3, 48
    li t4, 58
    blt t3, t4, 2f
    addi t3, t3, 7
2:
    sb t3, 0(t0)
    slli t2, t2, 4
    addi t1, t1, -1
    bnez t1, 1b
    li t1, 10
    sb t1, 0(t0)
    li s3, 3
    j fail_case
fail_addr:
    li s3, 4
    j fail_case
fail_mode:
    li s3, 5
fail_case:
    li t0, 0xf4000000
    addi t1, s6, 48
    sb t1, 0(t0)
    li t1, 58
    sb t1, 0(t0)
    addi t1, s5, 48
    sb t1, 0(t0)
    li t1, 58
    sb t1, 0(t0)
    addi t1, s3, 48
    sb t1, 0(t0)
    li t1, 10
    sb t1, 0(t0)
    j fail
#include "system-exit.inc"
success: .asciz "SYSTEM MMU privilege OK\n"
failure: .asciz "SYSTEM MMU privilege FAIL\n"
.set root, 0x4000
.set leaves, 0x5000
.org 0x10000, 0
user_begin:
user_load:
    mv a0, s4
    lw a0, 0(s0)
user_load_done:
    beq a0, s4, 1f
    li s7, -1
    ecall
1:
    addi s1, s1, -1
    bnez s1, user_load
    ecall
    j user_load
user_read:
    lw a0, 0(s0)
user_read_done:
    beq a0, s3, 1f
    li s7, -1
    ecall
1:
    addi s1, s1, -1
    bnez s1, user_read
    ecall
    j user_read
user_write:
    sw s3, 0(s0)
user_write_done:
    addi s1, s1, -1
    bnez s1, user_write
    ecall
    j user_write
user_store:
    sw zero, 0(s0)
user_store_done:
    addi s1, s1, -1
    bnez s1, user_store
    ecall
    j user_store
user_jump:
    jalr ra, s0, 0
user_jump_done:
    addi s1, s1, -1
    bnez s1, user_jump
    ecall
    j user_jump
user_direct:
    jal ra, kernel_fn
user_direct_done:
    addi s1, s1, -1
    bnez s1, user_direct
    ecall
    j user_direct
.org 0x13000, 0
kernel_fn:
    ret
.org 0x16000, 0
    ret
