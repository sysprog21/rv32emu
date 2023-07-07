/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#if !RV32_HAS(GDBSTUB)
#error "Do not manage to build this file unless you enable gdbstub support."
#endif

#include <assert.h>
#include <errno.h>

#include "mini-gdbstub/include/gdbstub.h"

#include "breakpoint.h"
#include "riscv_private.h"
#include "state.h"

static int rv_read_reg(void *args, int regno, size_t *data)
{
    riscv_t *rv = (riscv_t *) args;

    if (unlikely(regno > 32))
        return EFAULT;

    if (regno == 32)
        *data = rv_get_pc(rv);
    else
        *data = rv_get_reg(rv, regno);

    return 0;
}

static int rv_write_reg(void *args, int regno, size_t data)
{
    if (unlikely(regno > 32))
        return EFAULT;

    riscv_t *rv = (riscv_t *) args;
    if (regno == 32)
        rv_set_pc(rv, data);
    else
        rv_set_reg(rv, regno, data);

    return 0;
}

static int rv_read_mem(void *args, size_t addr, size_t len, void *val)
{
    riscv_t *rv = (riscv_t *) args;

    int err = 0;
    for (size_t i = 0; i < len; i++) {
        /* FIXME: This is implemented as a simple workaround for reading
         * an invalid address. We may have to do error handling in the
         * mem_read_* function directly. */
        *((uint8_t *) val + i) = rv->io.mem_read_b(addr + i);
    }

    return err;
}

static int rv_write_mem(void *args, size_t addr, size_t len, void *val)
{
    riscv_t *rv = (riscv_t *) args;

    for (size_t i = 0; i < len; i++)
        rv->io.mem_write_b(addr + i, *((uint8_t *) val + i));

    return 0;
}

static inline bool rv_is_interrupt(riscv_t *rv)
{
    return __atomic_load_n(&rv->is_interrupted, __ATOMIC_RELAXED);
}

static gdb_action_t rv_cont(void *args)
{
    riscv_t *rv = (riscv_t *) args;
    const uint32_t cycles_per_step = 1;

    for (; !rv_has_halted(rv) && !rv_is_interrupt(rv);) {
        if (breakpoint_map_find(rv->breakpoint_map, rv_get_pc(rv)))
            break;

        rv_step(rv, cycles_per_step);
    }

    /* Clear the interrupt if it's pending */
    __atomic_store_n(&rv->is_interrupted, false, __ATOMIC_RELAXED);

    return ACT_RESUME;
}

static gdb_action_t rv_stepi(void *args)
{
    riscv_t *rv = (riscv_t *) args;
    rv_step(rv, 1);
    return ACT_RESUME;
}

static bool rv_set_bp(void *args, size_t addr, bp_type_t type)
{
    riscv_t *rv = (riscv_t *) args;
    if (type != BP_SOFTWARE)
        return false;

    return breakpoint_map_insert(rv->breakpoint_map, addr);
}

static bool rv_del_bp(void *args, size_t addr, bp_type_t type)
{
    riscv_t *rv = (riscv_t *) args;
    if (type != BP_SOFTWARE)
        return false;

    /* When there is no matched breakpoint, no further action is taken */
    breakpoint_map_del(rv->breakpoint_map, addr);
    return true;
}

static void rv_on_interrupt(void *args)
{
    riscv_t *rv = (riscv_t *) args;

    /* Notify the emulator to break out the for loop in rv_cont */
    __atomic_store_n(&rv->is_interrupted, true, __ATOMIC_RELAXED);
}

const struct target_ops gdbstub_ops = {
    .read_reg = rv_read_reg,
    .write_reg = rv_write_reg,
    .read_mem = rv_read_mem,
    .write_mem = rv_write_mem,
    .cont = rv_cont,
    .stepi = rv_stepi,
    .set_bp = rv_set_bp,
    .del_bp = rv_del_bp,
    .on_interrupt = rv_on_interrupt,
};
