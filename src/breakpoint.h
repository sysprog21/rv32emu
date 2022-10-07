/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include "map.h"
#include "riscv.h"

typedef struct {
    riscv_word_t addr;
    uint32_t orig_insn;
} breakpoint_t;

typedef map_t breakpoint_map_t;

breakpoint_map_t breakpoint_map_new();
bool breakpoint_map_insert(breakpoint_map_t map, riscv_word_t addr);
breakpoint_t *breakpoint_map_find(breakpoint_map_t map, riscv_word_t addr);
bool breakpoint_map_del(breakpoint_map_t map, riscv_word_t addr);
void breakpoint_map_destroy(breakpoint_map_t map);
