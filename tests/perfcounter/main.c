#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern uint64_t get_cycles();
extern uint64_t get_instret();

/*
 * Taken from the Sparkle-suite which is a collection of lightweight symmetric
 * cryptographic algorithms currently in the final round of the NIST
 * standardization effort.
 * See https://sparkle-lwc.github.io/
 */
extern void sparkle_asm(unsigned int *state, unsigned int ns);

#define WORDS 12
#define ROUNDS 7

int main(void)
{
    unsigned int state[WORDS] = {0};

    /* measure cycles */
    uint64_t instret = get_instret();
    uint64_t oldcount = get_cycles();
    sparkle_asm(state, ROUNDS);
    uint64_t cyclecount = get_cycles() - oldcount;

    printf("cycle count: %u\n", (unsigned int) cyclecount);
    printf("instret: %x\n", (unsigned) (instret & 0xffffffff));

    memset(state, 0, WORDS * sizeof(uint32_t));

    sparkle_asm(state, ROUNDS);

    printf("Sparkle state:\n");
    for (int i = 0; i < WORDS; i += 2)
        printf("%X %X\n", state[i], state[i + 1]);

    return 0;
}
