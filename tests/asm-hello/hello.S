# RISC-V assembly program to print "Hello World!" to stdout.

.org 0
# Provide program starting address to linker
.global _start

/* newlib system calls */
.set SYSEXIT,  93
.set SYSWRITE, 64

.section .rodata
str: .ascii "Hello World!\n"
     .set str_size, .-str

.text
_start:
    li t0, 0
    li t1, 5

    # dummy test for jal instruction
.L1:
    jal x0, .L2
.L2:
    nop

loop:
    beq t0, t1, end

    li a7, SYSWRITE     # "write" syscall
    li a0, 1            # 1 = standard output (stdout)
    la a1, str          # load address of hello string
    li a2, str_size     # length of hello string
    ecall               # invoke syscall to print the string
    addi t0, t0, 1
    j loop

end:
    li a7, SYSEXIT      # "exit" syscall
    add a0, x0, 0       # Use 0 return code
    ecall               # invoke syscall to terminate the program
