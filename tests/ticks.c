#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

typedef uint64_t ticks;
static inline ticks getticks(void)
{
    uint64_t result;
    uint32_t l, h, h2;
    asm volatile(
        "rdcycleh %0\n"
        "rdcycle %1\n"
        "rdcycleh %2\n"
        "sub %0, %0, %2\n"
        "seqz %0, %0\n"
        "sub %0, zero, %0\n"
        "and %1, %1, %0\n"
        : "=r"(h), "=r"(l), "=r"(h2));
    result = (((uint64_t) h) << 32) | ((uint64_t) l);
    return result;
}

static uint64_t fib(uint64_t n)
{
    if (n <= 1)
        return n;
    return fib(n - 1) + fib(n - 2);
}

int main()
{
    ticks t0 = getticks();
    fib(19);
    ticks t1 = getticks();
    printf("elapsed cycle: %" PRIu64 "\n", t1 - t0);
    return 0;
}
