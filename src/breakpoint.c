#include "breakpoint.h"
#include <assert.h>

static inline int cmp(const void *arg0, const void *arg1)
{
    riscv_word_t *a = (riscv_word_t *) arg0, *b = (riscv_word_t *) arg1;
    return (*a < *b) ? _CMP_LESS : (*a > *b) ? _CMP_GREATER : _CMP_EQUAL;
}

breakpoint_map_t breakpoint_map_new()
{
    return map_init(riscv_word_t, breakpoint_t, cmp);
}

bool breakpoint_map_insert(breakpoint_map_t map, riscv_word_t addr)
{
    breakpoint_t bp = (breakpoint_t){.addr = addr, .orig_insn = 0};
    map_iter_t it;
    map_find(map, &it, &addr);
    /* We don't expect to set breakpoint at duplicate address */
    if (!map_at_end(map, &it))
        return false;
    return map_insert(map, &addr, &bp);
}

static bool breakpoint_map_find_it(breakpoint_map_t map,
                                   riscv_word_t addr,
                                   map_iter_t *it)
{
    map_find(map, it, &addr);
    if (map_at_end(map, it)) {
        return false;
    }

    return true;
}

breakpoint_t *breakpoint_map_find(breakpoint_map_t map, riscv_word_t addr)
{
    map_iter_t it;
    if (!breakpoint_map_find_it(map, addr, &it))
        return NULL;

    return (breakpoint_t *) map_iter_value(&it, breakpoint_t *);
}

bool breakpoint_map_del(breakpoint_map_t map, riscv_word_t addr)
{
    map_iter_t it;
    if (!breakpoint_map_find_it(map, addr, &it))
        return false;

    map_erase(map, &it);
    return true;
}

void breakpoint_map_destroy(breakpoint_map_t map)
{
    map_delete(map);
}
