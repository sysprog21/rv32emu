/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include "mt19937.h"

static uint64_t MT[624];
static uint64_t index = 0;

void mt19937_init(uint64_t seed)
{
    MT[0] = seed;
    for (int i = 1; i < 624; i++) {
        MT[i] = 0xffffffffULL &
                (0x6c078965ULL * (MT[i - 1] ^ (MT[i - 1] >> 30)) + i);
    }
}

static void generate_numbers(void)
{
    for (int i = 0; i < 624; i++) {
        uint64_t y =
            (MT[i] & 0x80000000ULL) + (MT[(i + 1) % 624] & 0x7fffffffULL);
        MT[i] = MT[(i + 397) % 624] ^ (y >> 1);
        if (y % 2 != 0)
            MT[i] = MT[i] ^ 0x9908b0dfULL;
    }
}

uint64_t mt19937_extract(void)
{
    if (index == 0)
        generate_numbers();

    uint64_t y = MT[index];
    y = y ^ (y >> 11);
    y = y ^ ((y << 7) & 0x9d2c5680ULL);
    y = y ^ ((y << 15) & 0xefc60000ULL);
    y = y ^ (y >> 18);

    index = (index + 1) % 624;
    return y;
}
