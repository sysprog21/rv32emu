/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdint.h>

struct riscv_internal;

/* One slot of an AOT artifact's entry index. tools/aot.py includes this
 * header in the index it generates, so the emulator and the artifact share
 * a single layout. hash is the page key, zero in user mode; digest is the
 * page's SHA-256, used in system mode only.
 */
struct aot_entry {
    uint32_t pc;
    uint64_t hash;
    void (*entry)(struct riscv_internal *);
    const uint8_t *digest;
};
