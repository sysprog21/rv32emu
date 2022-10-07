/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include "io.h"
#include "map.h"
#include "riscv.h"

/* state structure passed to the runtime */
typedef struct {
    memory_t *mem;

    /* the data segment break address */
    riscv_word_t break_addr;

    /* file descriptor map: int -> (FILE *) */
    map_t fd_map;
} state_t;

static inline state_t *state_new()
{
    state_t *s = malloc(sizeof(state_t));
    s->mem = memory_new();
    s->break_addr = 0;

    s->fd_map = map_init(int, FILE *, map_cmp_int);
    FILE *files[] = {stdin, stdout, stderr};
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++)
        map_insert(s->fd_map, &i, &files[i]);

    return s;
}

static inline void state_delete(state_t *s)
{
    if (!s)
        return;

    map_delete(s->fd_map);
    memory_delete(s->mem);
    free(s);
}
