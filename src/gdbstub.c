#include "mini-gdbstub/include/gdbstub.h"
#include <assert.h>
#include "breakpoint.h"
#include "riscv_private.h"

static size_t rv_read_reg(void *args, int regno)
{
    struct riscv_t *rv = (struct riscv_t *) args;

    if (regno < 32) {
        return rv_get_reg(rv, regno);
    }

    if (regno == 32) {
        return rv_get_pc(rv);
    }

    return -1;
}

static void rv_read_mem(void *args, size_t addr, size_t len, void *val)
{
    struct riscv_t *rv = (struct riscv_t *) args;

    for (size_t i = 0; i < len; i++)
        *((uint8_t *) val + i) = rv->io.mem_read_b(rv, addr + i);
}

static gdb_action_t rv_cont(void *args)
{
    struct riscv_t *rv = (struct riscv_t *) args;
    const uint32_t cycles_per_step = 1;

    for (; !rv_has_halted(rv);) {
        if (breakpoint_map_find(rv->breakpoint_map, rv_get_pc(rv)) != NULL) {
            break;
        }
        rv_step(rv, cycles_per_step);
    }

    return ACT_RESUME;
}

static gdb_action_t rv_stepi(void *args)
{
    struct riscv_t *rv = (struct riscv_t *) args;
    rv_step(rv, 1);
    return ACT_RESUME;
}

static bool rv_set_bp(void *args, size_t addr, bp_type_t type)
{
    struct riscv_t *rv = (struct riscv_t *) args;
    if (type != BP_SOFTWARE)
        return false;

    return breakpoint_map_insert(rv->breakpoint_map, addr);
}

static bool rv_del_bp(void *args, size_t addr, bp_type_t type)
{
    struct riscv_t *rv = (struct riscv_t *) args;
    if (type != BP_SOFTWARE)
        return false;
    /* When there is no matched breakpoint, no further action is taken */
    breakpoint_map_del(rv->breakpoint_map, addr);
    return true;
}

const struct target_ops gdbstub_ops = {
    .read_reg = rv_read_reg,
    .read_mem = rv_read_mem,
    .cont = rv_cont,
    .stepi = rv_stepi,
    .set_bp = rv_set_bp,
    .del_bp = rv_del_bp,
};
