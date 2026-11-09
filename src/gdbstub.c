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
#if RV32_HAS(SYSTEM) && RV32_HAS(JIT)
        mmu_note_guest_write(rv, paddr, span);
#endif
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
           rv->watchpoints.triggered ||
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
    rv->watchpoints.triggered = false;
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

    rv->watchpoints.triggered = false;
    rv_debug_resume(rv);
    return ACT_RESUME;
}

/* Data watchpoints wrap the memory I/O callbacks for the whole debug session.
 * Every guest load and store reaches the callbacks, whichever RVOP or helper
 * issues it, so this is the one place that tests them. The check runs after
 * the access and only when it completed: a faulting access touched nothing, so
 * it must not be reported as a hit.
 */
#if RV32_HAS(SYSTEM)
#define WP_ACCESS_FAULTED() unlikely(need_handle_signal)
#else
#define WP_ACCESS_FAULTED() false
#endif

#define WP_IO_READ(fn, type, len)                                     \
    static type wp_mem_##fn(riscv_t *rv, riscv_word_t addr)           \
    {                                                                 \
        type value = rv->watchpoints.orig_io.mem_##fn(rv, addr);      \
        if (rv->watchpoints.count && !WP_ACCESS_FAULTED())            \
            watchpoint_set_check(&rv->watchpoints, addr, len, false); \
        return value;                                                 \
    }
#define WP_IO_WRITE(fn, type, len)                                      \
    static void wp_mem_##fn(riscv_t *rv, riscv_word_t addr, type value) \
    {                                                                   \
        rv->watchpoints.orig_io.mem_##fn(rv, addr, value);              \
        if (rv->watchpoints.count && !WP_ACCESS_FAULTED())              \
            watchpoint_set_check(&rv->watchpoints, addr, len, true);    \
    }
WP_IO_READ(read_w, riscv_word_t, 4)
WP_IO_READ(read_s, riscv_half_t, 2)
WP_IO_READ(read_b, riscv_byte_t, 1)
WP_IO_WRITE(write_w, riscv_word_t, 4)
WP_IO_WRITE(write_s, riscv_half_t, 2)
WP_IO_WRITE(write_b, riscv_byte_t, 1)

/* Called once before the guest runs under the debugger. Installing the
 * wrappers up front keeps the JIT build's dTLB fast path from ever tagging a
 * page (jit_dtlb_fill tags none while the I/O is not the default), so no
 * access can bypass them later.
 */
void rv_debug_wrap_io(riscv_t *rv)
{
    rv->watchpoints.orig_io = rv->io;
    rv->io.mem_read_w = wp_mem_read_w;
    rv->io.mem_read_s = wp_mem_read_s;
    rv->io.mem_read_b = wp_mem_read_b;
    rv->io.mem_write_w = wp_mem_write_w;
    rv->io.mem_write_s = wp_mem_write_s;
    rv->io.mem_write_b = wp_mem_write_b;
}

static int rv_set_bp(void *args, size_t addr, size_t len, bp_type_t type)
{
    riscv_t *rv = (riscv_t *) args;

    switch (type) {
    case BP_SOFTWARE:
        return breakpoint_map_insert(rv->breakpoint_map, addr);
    case WP_WRITE:
    case WP_READ:
    case WP_ACCESS:
        return watchpoint_set_add(&rv->watchpoints, addr, len, type);
    default:
        return ENOTSUP;
    }
}

static int rv_del_bp(void *args, size_t addr, size_t len, bp_type_t type)
{
    riscv_t *rv = (riscv_t *) args;

    switch (type) {
    case BP_SOFTWARE:
        return breakpoint_map_del(rv->breakpoint_map, addr);
    case WP_WRITE:
    case WP_READ:
    case WP_ACCESS:
        return watchpoint_set_del(&rv->watchpoints, addr, len, type);
    default:
        return ENOTSUP;
    }
}

/* GDB only treats a stop as a watchpoint hit when the stop reply names the
 * triggering address, so hand back the armed watchpoint's base and consume
 * the flag: one stop per hit. */
static bool rv_wp_hit(void *args, size_t *addr, bp_type_t *type)
{
    riscv_t *rv = (riscv_t *) args;
    riscv_word_t hit_addr;

    if (!watchpoint_set_take_hit(&rv->watchpoints, &hit_addr, type))
        return false;

    *addr = hit_addr;
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
    .wp_hit = rv_wp_hit,
    .on_interrupt = rv_on_interrupt,
};
