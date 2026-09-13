/* write(2) validates its whole guest buffer before any output: a zero-length
 * write succeeds for any buffer pointer, and a range that wraps past the end of
 * the address space is rejected. */

#include <stdint.h>
#include <stdio.h>

int main(void)
{
    register uint32_t a0 asm("a0") = 1;
    register uint32_t a1 asm("a1") = UINT32_MAX;
    register uint32_t a2 asm("a2") = 0;
    register uint32_t a7 asm("a7") = 64;

    asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");

    if (a0 != 0) {
        puts("zero-length write failed");
        return 1;
    }

    a0 = 1;
    a1 = UINT32_MAX;
    /* Two bytes cross the end of the 32-bit guest address space. */
    a2 = 2;
    a7 = 64;
    asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");

    if (a0 != UINT32_MAX) {
        puts("invalid nonzero write accepted");
        return 1;
    }

    puts("zero-length write passed");
    return 0;
}
