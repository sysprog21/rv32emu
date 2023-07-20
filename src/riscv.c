/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "riscv_private.h"
#include "utils.h"

#if !RV32_HAS(JIT)
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

#if !RV32_HAS(JIT)
    /* initialize the block map */
    block_map_init(&rv->block_map, 10);
#else
    rv->block_cache = cache_create(10);
    rv->code_cache = cache_create(10);
#endif

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

#if RV32_HAS(JIT)
static void release_block(void *block)
{
    free(((block_t *) block)->ir);
    free(block);
}
#endif

void rv_delete(riscv_t *rv)
{
    assert(rv);
#if !RV32_HAS(JIT)
    block_map_clear(&rv->block_map);
    free(rv->block_map.map);
#else
    cache_free(rv->block_cache, release_block);
#endif
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

/* get current time in microsecnds and update csr_time register */
static inline void update_time(riscv_t *rv)
{
    struct timeval tv;
    rv_gettimeofday(&tv);

    uint64_t t = (uint64_t) tv.tv_sec * 1e6 + (uint32_t) tv.tv_usec;
    rv->csr_time[0] = t & 0xFFFFFFFF;
    rv->csr_time[1] = t >> 32;
}

#if RV32_HAS(Zicsr)
/* get a pointer to a CSR */
static uint32_t *csr_get_ptr(riscv_t *rv, uint32_t csr)
{
    /* csr & 0xFFF prevent sign-extension in decode stage */
    switch (csr & 0xFFF) {
    case CSR_MSTATUS: /* Machine Status */
        return (uint32_t *) (&rv->csr_mstatus);
    case CSR_MTVEC: /* Machine Trap Handler */
        return (uint32_t *) (&rv->csr_mtvec);
    case CSR_MISA: /* Machine ISA and Extensions */
        return (uint32_t *) (&rv->csr_misa);

    /* Machine Trap Handling */
    case CSR_MSCRATCH: /* Machine Scratch Register */
        return (uint32_t *) (&rv->csr_mscratch);
    case CSR_MEPC: /* Machine Exception Program Counter */
        return (uint32_t *) (&rv->csr_mepc);
    case CSR_MCAUSE: /* Machine Exception Cause */
        return (uint32_t *) (&rv->csr_mcause);
    case CSR_MTVAL: /* Machine Trap Value */
        return (uint32_t *) (&rv->csr_mtval);
    case CSR_MIP: /* Machine Interrupt Pending */
        return (uint32_t *) (&rv->csr_mip);

    /* Machine Counter/Timers */
    case CSR_CYCLE: /* Cycle counter for RDCYCLE instruction */
        return (uint32_t *) &rv->csr_cycle;
    case CSR_CYCLEH: /* Upper 32 bits of cycle */
        return &((uint32_t *) &rv->csr_cycle)[1];

    /* TIME/TIMEH - very roughly about 1 ms per tick */
    case CSR_TIME: { /* Timer for RDTIME instruction */
        update_time(rv);
        return &rv->csr_time[0];
    }
    case CSR_TIMEH: { /* Upper 32 bits of time */
        update_time(rv);
        return &rv->csr_time[1];
    }
    case CSR_INSTRET: /* Number of Instructions Retired Counter */
        /* Number of Instructions Retired Counter, just use cycle */
        return (uint32_t *) (&rv->csr_cycle);
#if RV32_HAS(EXT_F)
    case CSR_FFLAGS:
        return (uint32_t *) (&rv->csr_fcsr);
    case CSR_FCSR:
        return (uint32_t *) (&rv->csr_fcsr);
#endif
    default:
        return NULL;
    }
}

static inline bool csr_is_writable(uint32_t csr)
{
    return csr < 0xc00;
}

uint32_t csr_csrrw(riscv_t *rv, uint32_t csr, uint32_t val)
{
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    uint32_t out = *c;
#if RV32_HAS(EXT_F)
    if (csr == CSR_FFLAGS)
        out &= FFLAG_MASK;
#endif
    if (csr_is_writable(csr))
        *c = val;

    return out;
}

uint32_t csr_csrrs(riscv_t *rv, uint32_t csr, uint32_t val)
{
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    uint32_t out = *c;
#if RV32_HAS(EXT_F)
    if (csr == CSR_FFLAGS)
        out &= FFLAG_MASK;
#endif
    if (csr_is_writable(csr))
        *c |= val;

    return out;
}

uint32_t csr_csrrc(riscv_t *rv, uint32_t csr, uint32_t val)
{
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    uint32_t out = *c;
#if RV32_HAS(EXT_F)
    if (csr == CSR_FFLAGS)
        out &= FFLAG_MASK;
#endif
    if (csr_is_writable(csr))
        *c &= ~val;
    return out;
}
#endif
