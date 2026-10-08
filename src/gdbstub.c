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
#include "riscv.h"
#include "riscv_private.h"
#if RV32_HAS(SYSTEM)
#include "system.h"
#endif

static size_t rv_get_reg_bytes(UNUSED int regno)
{
    return 4;
}

static int rv_read_reg(void *args, int regno, void *data)
{
    riscv_t *rv = (riscv_t *) args;

    if (unlikely(regno > 32))
        return EFAULT;

    if (regno == 32)
        *(riscv_word_t *) data = rv_get_pc(rv);
    else
        *(riscv_word_t *) data = rv_get_reg(rv, regno);

    return 0;
}

static int rv_write_reg(void *args, int regno, void *data)
{
    if (unlikely(regno > 32))
        return EFAULT;

    riscv_t *rv = (riscv_t *) args;
    if (regno == 32)
        rv_set_pc(rv, *(riscv_word_t *) data);
    else
        rv_set_reg(rv, regno, *(riscv_word_t *) data);

    return 0;
}

/* Find the guest RAM behind a debugger address. The hart's own accessors are
 * not usable here: in system mode they raise page faults into the guest, which
 * then runs its handler and may kill the current process, and they set A/D
 * bits, fill the TLB and read device registers that change state when read.
 * Translate without side effects instead, and reach RAM only.
 *
 * Returns how many of the @len bytes at @addr are contiguous in RAM from
 * *@paddr, up to the end of the page, or 0 if @addr itself is not.
 */
static uint32_t rv_debug_ram_span(riscv_t *rv,
                                  size_t addr,
                                  size_t len,
                                  uint32_t *paddr)
{
    *paddr = 0;
    if (addr > UINT32_MAX)
        return 0;
    size_t span = RV_PG_SIZE - (addr & (RV_PG_SIZE - 1));
    if (span > len)
        span = len;
#if RV32_HAS(SYSTEM)
    if (!mmu_debug_translate(rv, (uint32_t) addr, paddr))
        return 0;
#else
    *paddr = (uint32_t) addr;
#endif
    return GUEST_RAM_CONTAINS(PRIV(rv)->mem, *paddr, span) ? span : 0;
}

static int rv_read_mem(void *args, size_t addr, size_t len, void *val)
{
    riscv_t *rv = (riscv_t *) args;
    uint8_t *buf = val;

    while (len) {
        uint32_t paddr;
        uint32_t span = rv_debug_ram_span(rv, addr, len, &paddr);
        if (!span)
            return EFAULT;
        memory_read(PRIV(rv)->mem, buf, paddr, span);
        addr += span;
        buf += span;
        len -= span;
    }

    return 0;
}

static int rv_write_mem(void *args, size_t addr, size_t len, void *val)
{
    riscv_t *rv = (riscv_t *) args;
    const uint8_t *buf = val;

    /* Check the whole range first, so a failed write changes nothing */
    for (size_t done = 0; done < len;) {
        uint32_t paddr;
        uint32_t span = rv_debug_ram_span(rv, addr + done, len - done, &paddr);
        if (!span)
            return EFAULT;
        done += span;
    }

    while (len) {
        uint32_t paddr;
        uint32_t span = rv_debug_ram_span(rv, addr, len, &paddr);
        memory_write(PRIV(rv)->mem, paddr, buf, span);
        addr += span;
        buf += span;
        len -= span;
    }

    return 0;
}

static inline bool rv_is_interrupt(riscv_t *rv)
{
    return ATOMIC_LOAD(&rv->is_interrupted, ATOMIC_RELAXED);
}

static bool rv_debug_should_stop(riscv_t *rv)
{
    return rv_has_halted(rv) || rv_is_interrupt(rv) ||
           breakpoint_map_find(rv->breakpoint_map, rv_get_pc(rv));
}

/* Run one debugger request: a single instruction, or for "continue" every
 * instruction up to the next stop. With hart coroutines this runs on the hart
 * stack, so a continue switches stacks once instead of twice per instruction.
 */
void rv_debug_run(riscv_t *rv)
{
    do {
        rv_step_debug(rv);
#if RV32_HAS(VIRTIO_NET)
        rv_refresh_vnet(rv);
#endif
#if RV32_HAS(VIRTIO_SND)
        rv_refresh_vsnd(rv);
#endif
    } while (rv->debug_continue && !rv_debug_should_stop(rv));
}

static void rv_debug_resume(riscv_t *rv)
{
#if RV32_HAS_HART_CORO
    rv_coroutine_step(rv);
#else
    rv_debug_run(rv);
#endif
}

static gdb_action_t rv_cont(void *args)
{
    riscv_t *rv = (riscv_t *) args;
    assert(rv);

    /* Loop, since a guest reboot restarts the hart coroutine and returns here
     * before any stop condition holds.
     */
    rv->debug_continue = true;
    while (!rv_debug_should_stop(rv))
        rv_debug_resume(rv);
    rv->debug_continue = false;

    /* Clear the interrupt if it's pending */
    ATOMIC_STORE(&rv->is_interrupted, false, ATOMIC_RELAXED);

    return ACT_RESUME;
}

static gdb_action_t rv_stepi(void *args)
{
    riscv_t *rv = (riscv_t *) args;
    assert(rv);

    rv_debug_resume(rv);
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
    ATOMIC_STORE(&rv->is_interrupted, true, ATOMIC_RELAXED);
}

const struct target_ops gdbstub_ops = {
    .get_reg_bytes = rv_get_reg_bytes,
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
