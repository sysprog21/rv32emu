/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdint.h>

/* SHA-256 of exactly one 4 KiB instruction page, in standard byte order. */
void aot_page_digest(const uint8_t page[4096], uint8_t digest[32]);
