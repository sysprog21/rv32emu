/* Warm an untaken misaligned branch, then take it after T1 compilation. */
.option norvc
.option norelax
.text
.globl _start
.globl fault_branch
_start:
    la t0, handler
    csrw mtvec, t0
    li s0, 6000
    li s1, INITIAL
    li t1, 1
again:
    jal ra, branch_body
    addi s0, s0, -1
    bnez s0, again
    li s1, FINAL
    jal ra, branch_body
    j .
branch_body:
    addi s1, s1, 0
fault_branch:
    /* Branch immediate +2, with s1 as rs1 and optional t1 as rs2. */
    .word ((RS2 << 20) | (9 << 15) | (FUNCT << 12) | (1 << 8) | 0x63)
    ret
handler:
    csrr s2, mepc
    csrr s3, mtval
    csrr s4, mcause
    rdcycle s5
    csrw mtvec, zero
    li a0, 0
    li a7, 93
    ecall
