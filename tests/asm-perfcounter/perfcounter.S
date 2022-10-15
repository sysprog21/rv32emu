# RISC-V assembly code to test performance counter

.section .text
.global main

.text
get_cycle:
        addi    sp,sp,-16
        sw      s0,12(sp)
        addi    s0,sp,16

        csrsi mstatus, (1 << 2)
        csrr t1, mstatus

        addi a7, zero, 64     # "write" syscall
        addi a0, zero, 1      # 1 = stdout
        addi a1, zero, 99
        li a2, 14
        ecall                 # involke syscall

        # Not working

        lw      s0,12(sp)
        addi    sp,sp,16
        jr      ra

main:
        addi    sp,sp,-16
        sw      ra,12(sp)
        sw      s0,8(sp)
        addi    s0,sp,16
.L4:
        call    get_cycle
        j       .L4
