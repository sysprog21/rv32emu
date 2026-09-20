/* Run the cache eviction regression without a guest libc. */
    .text
    .global _start
_start:
    call main
    li a7, 93
    ecall
    .section .note.GNU-stack,"",@progbits
