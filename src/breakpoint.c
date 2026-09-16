/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#if !RV32_HAS(GDBSTUB)
#error "Do not manage to build this file unless you enable gdbstub support."
#endif

#include "breakpoint.h"

breakpoint_map_t breakpoint_map_new(void)
{
    /* riscv_word_t is uint32_t, so the built-in unsigned comparator applies and
     * the map can specialize the search instead of calling through a function
     * pointer at every level.
     */
    return map_init(riscv_word_t, breakpoint_t, map_cmp_uint);
}

bool breakpoint_map_insert(breakpoint_map_t map, riscv_word_t addr)
{
    breakpoint_t bp = (breakpoint_t) {.addr = addr, .orig_insn = 0};
    return map_insert(map, &addr, &bp);
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

bool breakpoint_map_del(breakpoint_map_t map, riscv_word_t addr)
{
    map_iter_t it;
    if (!breakpoint_map_find_it(map, addr, &it))
        return false;

    map_erase(&it);
    return true;
}

void breakpoint_map_destroy(breakpoint_map_t map)
{
    map_delete(map);
}
