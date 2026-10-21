/* Shared exit path of the flat supervisor test kernels. "fail" prints the
 * kernel's failure string and "print" the string at a0 to the UART, then the
 * SBI system reset call stops the emulator. Each kernel defines "failure".
 */
fail:
    la a0, failure
print:
    li t1, 0xf4000000
1:
    lbu t0, 0(a0)
    beqz t0, stop
    sb t0, 0(t1)
    addi a0, a0, 1
    j 1b
stop:
    li a7, 0x53525354
    li a6, 0
    li a0, 0
    li a1, 0
    ecall
    j stop
