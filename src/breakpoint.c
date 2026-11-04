/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#if !RV32_HAS(GDBSTUB)
#error "Do not manage to build this file unless you enable gdbstub support."
#endif

#include <errno.h>

#include "breakpoint.h"

breakpoint_map_t breakpoint_map_new(void)
{
    /* riscv_word_t is uint32_t, so the built-in unsigned comparator applies and
     * the map can specialize the search instead of calling through a function
     * pointer at every level.
     */
    return map_init(riscv_word_t, breakpoint_t, map_cmp_uint);
}

int breakpoint_map_insert(breakpoint_map_t map, riscv_word_t addr)
{
    breakpoint_t bp = (breakpoint_t) {.addr = addr, .orig_insn = 0};
    return map_set(map, &addr, &bp) ? 0 : ENOMEM;
}

static bool breakpoint_map_find_it(breakpoint_map_t map,
                                   riscv_word_t addr,
                                   map_iter_t *it)
{
    map_find(map, it, &addr);
    if (map_at_end(it))
        return false;

    return true;
}

breakpoint_t *breakpoint_map_find(breakpoint_map_t map, riscv_word_t addr)
{
    map_iter_t it;
    if (!breakpoint_map_find_it(map, addr, &it))
        return NULL;

    return map_iter_value_ptr(&it);
}

int breakpoint_map_del(breakpoint_map_t map, riscv_word_t addr)
{
    map_iter_t it;
    if (breakpoint_map_find_it(map, addr, &it))
        map_erase(&it);

    return 0;
}

void breakpoint_map_destroy(breakpoint_map_t map)
{
    map_delete(map);
}

int watchpoint_set_add(watchpoint_set_t *ws,
                       riscv_word_t addr,
                       uint32_t len,
                       bp_type_t type)
{
    if (len == 0 || (uint64_t) addr + len > (uint64_t) UINT32_MAX + 1)
        return EFAULT;
    if (ws->count == RV_MAX_WATCHPOINTS)
        return ENOSPC;

    ws->items[ws->count++] =
        (watchpoint_t) {.addr = addr, .len = len, .type = type};
    return 0;
}

int watchpoint_set_del(watchpoint_set_t *ws,
                       riscv_word_t addr,
                       uint32_t len,
                       bp_type_t type)
{
    for (uint32_t i = 0; i < ws->count; i++) {
        if (ws->items[i].addr == addr && ws->items[i].len == len &&
            ws->items[i].type == type) {
            ws->items[i] = ws->items[--ws->count];
            break;
        }
    }
    /* Removing an absent watchpoint is not an error */
    return 0;
}

void watchpoint_set_check(watchpoint_set_t *ws,
                          riscv_word_t addr,
                          uint32_t len,
                          bool is_write)
{
    for (uint32_t i = 0; i < ws->count; i++) {
        bp_type_t type = ws->items[i].type;
        if (type == WP_WRITE && !is_write)
            continue;
        if (type == WP_READ && is_write)
            continue;

        /* Widen to 64-bit so an interval reaching the top of the address
         * space does not wrap: [addr, addr + len) overlaps [wa, wa + wl). */
        uint64_t wa = ws->items[i].addr, wl = ws->items[i].len;
        uint64_t a = addr, l = len;
        if (a < wa + wl && wa < a + l) {
            ws->triggered = true;
            ws->hit_addr = ws->items[i].addr;
            ws->hit_type = type;
            return;
        }
    }
}

bool watchpoint_set_take_hit(watchpoint_set_t *ws,
                             riscv_word_t *addr,
                             bp_type_t *type)
{
    if (!ws->triggered)
        return false;

    *addr = ws->hit_addr;
    *type = ws->hit_type;
    ws->triggered = false;
    return true;
}
