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
    uint32_t org_size; /* size of prologue and epilogue */
    uint32_t retpoline_loc;
    struct offset_map *offset_map;
    int n_blocks;
    struct jump *jumps;
    int n_jumps;
};

struct host_reg {
    uint8_t reg_idx : 5;   /* index to the host's register file */
    int8_t vm_reg_idx : 6; /* index to the vm register */
    bool dirty : 1; /* whether the context of register has been overridden */
    bool alive : 1; /* whether the register is no longer used in current basic
                       block */
};

struct jit_state *jit_state_init(size_t size);
void jit_state_exit(struct jit_state *state);
void jit_translate(riscv_t *rv, block_t *block);
typedef void (*exec_block_func_t)(riscv_t *rv, uintptr_t);

#if RV32_HAS(T2C)
void t2c_compile(block_t *block, uint64_t mem_base);
typedef void (*exec_t2c_func_t)(riscv_t *);
#endif
