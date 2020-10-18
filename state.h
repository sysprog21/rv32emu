#pragma once

#include "c_map.h"
#include "io.h"
#include "riscv.h"

/* state structure passed to the runtime */
typedef struct {
    memory_t *mem;

    /* the data segment break address */
    riscv_word_t break_addr;

    /* file descriptor map: int -> (FILE *) */
    c_map_t fd_map;
} state_t;

static inline state_t *state_new()
{
    state_t *s = malloc(sizeof(state_t));
    s->mem = memory_new();
    s->break_addr = 0;

    s->fd_map = c_map_init(int, FILE *, c_map_cmp_int);
    FILE *files[] = {stdin, stdout, stderr};
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++)
        c_map_insert(s->fd_map, &i, &files[i]);

    return s;
}

static inline void state_delete(state_t *s)
{
    if (!s)
        return;

    c_map_delete(s->fd_map);
    memory_delete(s->mem);
    free(s);
}

/* main syscall handler */
void syscall_handler(struct riscv_t *);
