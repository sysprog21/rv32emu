/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "mpool.h"
#include "riscv_private.h"
#include "state.h"
#include "utils.h"
#if RV32_HAS(JIT)
#include "cache.h"
#include "jit_x64.h"
#define CODE_CACHE_SIZE (1024 * 1024)
#endif

#define BLOCK_IR_MAP_CAPACITY_BITS 10

#if !RV32_HAS(JIT)
/* initialize the block map */
static void block_map_init(block_map_t *map, const uint8_t bits)
{
    map->block_capacity = 1 << bits;
    map->size = 0;
    map->map = calloc(map->block_capacity, sizeof(struct block *));
}

/* clear all block in the block map */
void block_map_clear(riscv_t *rv)
{
    block_map_t *map = &rv->block_map;
    for (uint32_t i = 0; i < map->block_capacity; i++) {
        block_t *block = map->map[i];
        if (!block)
            continue;
        uint32_t idx;
        rv_insn_t *ir, *next;
        for (idx = 0, ir = block->ir_head; idx < block->n_insn;
             idx++, ir = next) {
            free(ir->fuse);
            free(ir->branch_table);
            next = ir->next;
            mpool_free(rv->block_ir_mp, ir);
        }
        mpool_free(rv->block_mp, block);
        map->map[i] = NULL;
    }
    map->size = 0;
}

static void block_map_destroy(riscv_t *rv)
{
    block_map_clear(rv);
    free(rv->block_map.map);

    mpool_destroy(rv->block_mp);
    mpool_destroy(rv->block_ir_mp);
}
#endif

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
    if (reg < N_RV_REGS && reg != rv_reg_zero)
        rv->X[reg] = in;
}

riscv_word_t rv_get_reg(riscv_t *rv, uint32_t reg)
{
    assert(rv);
    if (reg < N_RV_REGS)
        return rv->X[reg];

    return ~0U;
}

riscv_t *rv_create(const riscv_io_t *io,
                   riscv_user_t userdata,
                   int argc,
                   char **args,
                   bool output_exit_code)
{
    assert(io);

    riscv_t *rv = calloc(1, sizeof(riscv_t));

    /* copy over the IO interface */
    memcpy(&rv->io, io, sizeof(riscv_io_t));

    /* copy over the userdata */
    rv->userdata = userdata;

    rv->output_exit_code = output_exit_code;

    /* create block and IRs memory pool */
    rv->block_mp = mpool_create(sizeof(block_t) << BLOCK_MAP_CAPACITY_BITS,
                                sizeof(block_t));
    rv->block_ir_mp = mpool_create(
        sizeof(rv_insn_t) << BLOCK_IR_MAP_CAPACITY_BITS, sizeof(rv_insn_t));

#if !RV32_HAS(JIT)
    /* initialize the block map */
    block_map_init(&rv->block_map, BLOCK_MAP_CAPACITY_BITS);
#else
    rv->jit_state = init_state(CODE_CACHE_SIZE);
    rv->block_cache = cache_create(BLOCK_MAP_CAPACITY_BITS);
#endif
    /* reset */
    rv_reset(rv, 0U, argc, args);

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
#if !RV32_HAS(JIT)
    block_map_destroy(rv);
#else
    destroy_state(rv->jit_state);
    cache_free(rv->block_cache);
#endif
    free(rv);
}

void rv_reset(riscv_t *rv, riscv_word_t pc, int argc, char **args)
{
    assert(rv);
    memset(rv->X, 0, sizeof(uint32_t) * N_RV_REGS);

    /* set the reset address */
    rv->PC = pc;

    /* set the default stack pointer */
    rv->X[rv_reg_sp] = DEFAULT_STACK_ADDR;

    /* Store 'argc' and 'args' of the target program in 'state->mem'. Thus,
     * we can use an offset trick to emulate 32/64-bit target programs on
     * a 64-bit built emulator.
     *
     * memory layout of arguments as below:
     * -----------------------
     * |    NULL            |
     * -----------------------
     * |    envp[n]         |
     * -----------------------
     * |    envp[n - 1]     |
     * -----------------------
     * |    ...             |
     * -----------------------
     * |    envp[0]         |
     * -----------------------
     * |    NULL            |
     * -----------------------
     * |    args[n]         |
     * -----------------------
     * |    args[n - 1]     |
     * -----------------------
     * |    ...             |
     * -----------------------
     * |    args[0]         |
     * -----------------------
     * |    argc            |
     * -----------------------
     *
     * TODO: access to envp
     */

    state_t *s = rv_userdata(rv);

    /* copy args to RAM */
    uintptr_t args_size = (1 + argc + 1) * sizeof(uint32_t);
    uintptr_t args_bottom = DEFAULT_ARGS_ADDR;
    uintptr_t args_top = args_bottom - args_size;
    args_top &= 16;

    /* argc */
    uintptr_t *args_p = (uintptr_t *) args_top;
    memory_write(s->mem, (uintptr_t) args_p, (void *) &argc, sizeof(int));
    args_p++;

    /* args */
    /* used for calculating the offset of args when pushing to stack */
    size_t args_space[256];
    size_t args_space_idx = 0;
    size_t args_len;
    size_t args_len_total = 0;
    for (int i = 0; i < argc; i++) {
        const char *arg = args[i];
        args_len = strlen(arg);
        memory_write(s->mem, (uintptr_t) args_p, (void *) arg,
                     (args_len + 1) * sizeof(uint8_t));
        args_space[args_space_idx++] = args_len + 1;
        args_p = (uintptr_t *) ((uintptr_t) args_p + args_len + 1);
        args_len_total += args_len + 1;
    }
    args_p = (uintptr_t *) ((uintptr_t) args_p - args_len_total);
    args_p--; /* point to argc */

    /* ready to push argc, args to stack */
    int stack_size = (1 + argc + 1) * sizeof(uint32_t);
    uintptr_t stack_bottom = (uintptr_t) rv->X[rv_reg_sp];
    uintptr_t stack_top = stack_bottom - stack_size;
    stack_top &= -16;

    /* argc */
    uintptr_t *sp = (uintptr_t *) stack_top;
    memory_write(s->mem, (uintptr_t) sp,
                 (void *) (s->mem->mem_base + (uintptr_t) args_p), sizeof(int));
    args_p++;
    /* keep argc and args[0] within one word due to RV32 ABI */
    sp = (uintptr_t *) ((uint32_t *) sp + 1);

    /* args */
    for (int i = 0; i < argc; i++) {
        uintptr_t offset = (uintptr_t) args_p;
        memory_write(s->mem, (uintptr_t) sp, (void *) &offset,
                     sizeof(uintptr_t));
        args_p = (uintptr_t *) ((uintptr_t) args_p + args_space[i]);
        sp = (uintptr_t *) ((uint32_t *) sp + 1);
    }
    memory_fill(s->mem, (uintptr_t) sp, sizeof(uint32_t), 0);

    /* reset sp pointing to argc */
    rv->X[rv_reg_sp] = stack_top;

    /* reset the csrs */
    rv->csr_mtvec = 0;
    rv->csr_cycle = 0;
    rv->csr_mstatus = 0;
    rv->csr_misa |= MISA_SUPER | MISA_USER | MISA_I;
#if RV32_HAS(EXT_A)
    rv->csr_misa |= MISA_A;
#endif
#if RV32_HAS(EXT_C)
    rv->csr_misa |= MISA_C;
#endif
#if RV32_HAS(EXT_M)
    rv->csr_misa |= MISA_M;
#endif
#if RV32_HAS(EXT_F)
    rv->csr_misa |= MISA_F;
    /* reset float registers */
    for (int i = 0; i < N_RV_REGS; i++)
        rv->F[i].v = 0;
    rv->csr_fcsr = 0;
#endif

    rv->halt = false;
}
