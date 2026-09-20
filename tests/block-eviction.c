/*
 * Exercise JIT block-cache replacement after installing a pair of conditional
 * edges, two direct predecessors for one target, and a finite self-loop.
 * BLOCK_MAP_CAPACITY_BITS is 10, so the direct-jump sequence then creates
 * more blocks than the cache can retain and requires incoming chain edges to
 * be unlinked as their destinations are replaced.
 *
 * Freestanding: report through the write/exit syscalls instead of a guest libc.
 */

static void guest_write(const char *buf, long len)
{
    register long a0 __asm__("a0") = 1;
    register const char *a1 __asm__("a1") = buf;
    register long a2 __asm__("a2") = len;
    register long a7 __asm__("a7") = 64;
    __asm__ volatile("ecall" : : "r"(a0), "r"(a1), "r"(a2), "r"(a7) : "memory");
}

int main(void)
{
    int flag, latch, count;

    __asm__ volatile(
        "li %0, 0\n"
        "li %1, 0\n"
        "1:\n"
        "beq %0, zero, 2f\n"
        "li %1, 1\n"
        "jal zero, 3f\n"
        "2:\n"
        "li %0, 1\n"
        "jal zero, 3f\n"
        "3:\n"
        "beq %1, zero, 1b\n"
        "li %2, 0\n"
        "li t3, 2\n"
        "4:\n"
        "addi %2, %2, 1\n"
        "blt %2, t3, 4b\n"
        ".rept 2048\n"
        "jal zero, 5f\n"
        "5:\n"
        ".endr\n"
        : "=&r"(flag), "=&r"(latch), "=&r"(count)
        :
        : "t3");

    /* The two-pass diamond sets both registers once, and the self-loop runs
     * its bound exactly. Surviving the eviction storm is not enough: the
     * chained edges must also have produced the right control flow.
     */
    if (flag != 1 || latch != 1 || count != 2)
        return 1;

    guest_write("block eviction OK\n", 18);
    return 0;
}
