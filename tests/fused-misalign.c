/* Verify that a fault in the second member of a fused LW pair resumes at that
 * member, rather than reporting the first instruction as the faulting PC. */

#include <stdint.h>
#include <stdio.h>

static volatile uint32_t data[] = {0x11223344, 0x55667788, 0x99aabbcc};
static volatile uint32_t stores[3];

int main(void)
{
    const volatile uint8_t *base = (const volatile uint8_t *) data;
    uint32_t first;
    uint32_t second;

    /* Keep the two LW instructions consecutive so MOP fusion combines them.
     * The second access is intentionally unaligned and is emulated by the
     * user-mode trap handler. */
    asm volatile(
        "lw %0, 0(%2)\n\t"
        "lw %1, 5(%2)"
        : "=&r"(first), "=&r"(second)
        : "r"(base)
        : "memory");

    if (first != 0x11223344 || second != 0xcc556677) {
        puts("fused misalign failed");
        return 1;
    }

    /* Exercise the corresponding fused SW path.  The first store must retire
     * before the second member traps and is emulated. */
    asm volatile(
        "sw %1, 0(%0)\n\t"
        "sw %2, 5(%0)"
        :
        : "r"(stores), "r"(0xa1b2c3d4), "r"(0x10203040)
        : "memory");

    if (stores[0] != 0xa1b2c3d4 || stores[1] != 0x20304000 ||
        stores[2] != 0x00000010) {
        puts("fused misalign failed");
        return 1;
    }

    puts("fused misalign passed");
    return 0;
}
