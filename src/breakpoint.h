/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include "map.h"
#include "mini-gdbstub/include/gdbstub.h"
#include "riscv.h"

typedef struct {
    riscv_word_t addr;
    uint32_t orig_insn;
} breakpoint_t;

typedef map_t breakpoint_map_t;

breakpoint_map_t breakpoint_map_new(void);
int breakpoint_map_insert(breakpoint_map_t map, riscv_word_t addr);
breakpoint_t *breakpoint_map_find(breakpoint_map_t map, riscv_word_t addr);
int breakpoint_map_del(breakpoint_map_t map, riscv_word_t addr);
void breakpoint_map_destroy(breakpoint_map_t map);

#define RV_MAX_WATCHPOINTS 4

typedef struct {
    riscv_word_t addr;
    uint32_t len;
    bp_type_t type;
} watchpoint_t;

typedef struct {
    watchpoint_t items[RV_MAX_WATCHPOINTS];
    uint32_t count;

    /* The memory I/O callbacks the watchpoint wrappers forward to */
    riscv_io_t orig_io;

    bool triggered;
    riscv_word_t hit_addr;
    bp_type_t hit_type;
} watchpoint_set_t;

int watchpoint_set_add(watchpoint_set_t *ws,
                       riscv_word_t addr,
                       size_t len,
                       bp_type_t type);
int watchpoint_set_del(watchpoint_set_t *ws,
                       riscv_word_t addr,
                       size_t len,
                       bp_type_t type);

void watchpoint_set_check(watchpoint_set_t *ws,
                          riscv_word_t addr,
                          uint32_t len,
                          bool is_write);

bool watchpoint_set_take_hit(watchpoint_set_t *ws,
                             riscv_word_t *addr,
                             bp_type_t *type);
