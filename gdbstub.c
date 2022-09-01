#include <assert.h>
#include "mini-gdbstub/include/gdbstub.h"
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
        *((uint8_t *) val + i) = rv->io.mem_read_b(rv, addr);
}

static gdb_action_t rv_cont(void *args)
{
    struct riscv_t *rv = (struct riscv_t *) args;
    const uint32_t cycles_per_step = 1;

    for (; !rv_has_halted(rv);) {
        if (rv->breakpoint_specified && (rv_get_pc(rv) == rv->breakpoint_addr)) {
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
    if (type != BP_SOFTWARE || rv->breakpoint_specified)
        return false;

    rv->breakpoint_specified = true;
    rv->breakpoint_addr = addr;
    return true;;
}

static bool rv_del_bp(void *args, size_t addr, bp_type_t type)
{
    struct riscv_t *rv = (struct riscv_t *) args;
    if (type != BP_SOFTWARE)
        return false;
    /* When there is no matched breakpoint, no further action is taken */
    if (!rv->breakpoint_specified || addr != rv->breakpoint_addr)
        return true;

    rv->breakpoint_specified = false;
    rv->breakpoint_addr = 0;
    return true;
}

struct target_ops rv_ops = {
    .read_reg = rv_read_reg,
    .read_mem = rv_read_mem,
    .cont = rv_cont,
    .stepi = rv_stepi,
    .set_bp = rv_set_bp,
    .del_bp = rv_del_bp,
};
