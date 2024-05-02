/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdint.h>

/*
 * This is a pseudorandom number generator implemented using the mt19937 32-bit
 * algorithm. For more information on the mt19937 algorithm, please refer to the
 * Wikipedia page: https://en.wikipedia.org/wiki/Mersenne_Twister
 */

/* Initialize the random number generator with a seed */
void mt19937_init(uint64_t seed);

/* Generate and extract a 32-bit random number */
uint64_t mt19937_extract(void);
