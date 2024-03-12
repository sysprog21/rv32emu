/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdint.h>

#include "riscv_private.h"
#include "utils.h"

struct jump {
    uint32_t offset_loc;
    uint32_t target_pc;
    uint32_t target_offset;
};

struct offset_map {
    uint32_t pc;
    uint32_t offset;
};

struct jit_state {
    set_t set;
    uint8_t *buf;
    uint32_t offset;
    uint32_t stack_size;
    uint32_t size;
    uint32_t entry_loc;
    uint32_t exit_loc;
    uint32_t retpoline_loc;
    struct offset_map *offset_map;
    int n_blocks;
    struct jump *jumps;
    int n_jumps;
};

struct jit_state *jit_state_init(size_t size);
void jit_state_exit(struct jit_state *state);
uint32_t jit_translate(riscv_t *rv, block_t *block);
