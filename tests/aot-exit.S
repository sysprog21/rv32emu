/* AOT regions must leave libc's exit entry to the dispatcher, which marks
 * on_exit before guest cleanup closes the standard file descriptors.
 */
    .text
    .global _start, exit
_start:
    j exit
exit:
    li a0, 0
    li a7, 57
    ecall
    bltz a0, fail
    li a0, 0
    j finish
fail:
    li a0, 1
finish:
    li a7, 93
    ecall
