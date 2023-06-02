/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "riscv_private.h"

/* initialize the block map */
static void block_map_init(block_map_t *map, const uint8_t bits)
{
    map->block_capacity = 1 << bits;
    map->size = 0;
    map->map = calloc(map->block_capacity, sizeof(struct block *));
}

/* clear all block in the block map */
void block_map_clear(block_map_t *map)
{
    assert(map);
    for (uint32_t i = 0; i < map->block_capacity; i++) {
        block_t *block = map->map[i];
        if (!block)
            continue;
        for (uint32_t i = 0; i < block->n_insn; i++)
            free(block->ir[i].fuse);
        free(block->ir);
        free(block);
        map->map[i] = NULL;
    }
    map->size = 0;
}

riscv_user_t rv_userdata(riscv_t *rv)
{
    assert(rv);
    return rv->userdata;
}

bool rv_set_pc(riscv_t *rv, riscv_word_t pc)
{
    assert(rv);
#if RV32_HAS(EXT_C)
    if (pc & 1)
#else
    if (pc & 3)
#endif
        return false;

    rv->PC = pc;
    return true;
}

riscv_word_t rv_get_pc(riscv_t *rv)
{
    assert(rv);
    return rv->PC;
}

void rv_set_reg(riscv_t *rv, uint32_t reg, riscv_word_t in)
{
    assert(rv);
    if (reg < RV_N_REGS && reg != rv_reg_zero)
        rv->X[reg] = in;
}

riscv_word_t rv_get_reg(riscv_t *rv, uint32_t reg)
{
    assert(rv);
    if (reg < RV_N_REGS)
        return rv->X[reg];

    return ~0U;
}

riscv_t *rv_create(const riscv_io_t *io,
                   riscv_user_t userdata,
                   bool output_exit_code)
{
    assert(io);

    riscv_t *rv = calloc(1, sizeof(riscv_t));

    /* copy over the IO interface */
    memcpy(&rv->io, io, sizeof(riscv_io_t));

    /* copy over the userdata */
    rv->userdata = userdata;

    rv->output_exit_code = output_exit_code;

    /* initialize the block map */
    block_map_init(&rv->block_map, 10);

    /* reset */
    rv_reset(rv, 0U);

    return rv;
}

void rv_halt(riscv_t *rv)
{
    rv->halt = true;
}

bool rv_has_halted(riscv_t *rv)
{
    return rv->halt;
}

bool rv_enables_to_output_exit_code(riscv_t *rv)
{
    return rv->output_exit_code;
}

void rv_delete(riscv_t *rv)
{
    assert(rv);
    block_map_clear(&rv->block_map);
    free(rv->block_map.map);
    free(rv);
}

void rv_reset(riscv_t *rv, riscv_word_t pc)
{
    assert(rv);
    memset(rv->X, 0, sizeof(uint32_t) * RV_N_REGS);

    /* set the reset address */
    rv->PC = pc;

    /* set the default stack pointer */
    rv->X[rv_reg_sp] = DEFAULT_STACK_ADDR;

    /* reset the csrs */
    rv->csr_mtvec = 0;
    rv->csr_cycle = 0;
    rv->csr_mstatus = 0;

#if RV32_HAS(EXT_F)
    /* reset float registers */
    memset(rv->F, 0, sizeof(float) * RV_N_REGS);
    rv->csr_fcsr = 0;
#endif

    rv->halt = false;
}
