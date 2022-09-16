.org 0
.global _start

.data

str: .ascii "Hello World!\n"
     .set str_size, .-str

/* newlib system calls */
.set SYSEXIT,  93
.set SYSWRITE, 64

.text

_start:
    li a7, SYSWRITE
    li a0, 1
    la a1, str
    li a2, str_size
    ecall

    li a7, SYSEXIT
    add a0, x0, 0
    ecall
