#pragma once

#include <signal.h>
#include <stdbool.h>

#include "c2mir/c2mir.h"
#include "mir.h"
#include "riscv_private.h"

typedef void (*call_block_t)(struct riscv_t *);

/* a translated basic block */
struct block_t {
    /* number of instructions encompased */
    uint32_t instructions;

    /* address range of the basic block */
    uint32_t pc_start, pc_end;

    /* static next block prediction */
    struct block_t *predict;

    /* start of this blocks code */
    uint32_t *code;
    uint32_t code_capacity;

    call_block_t func;
};

struct block_map_t {
    uint32_t bits;
    /* max number of entries in the block map */
    uint32_t capacity;

    /* number of entries currently in the map */
    uint32_t size;

    /* block map */
    struct block_t **map;
};

struct riscv_jit_t {
    MIR_context_t ctx;
    struct c2mir_options *options;
    uint8_t debug_level;
    uint8_t optimize_level;
    uint32_t insn_len;
    struct block_map_t *block_map;
    FILE *codegen_log;
    FILE *code_log;
    FILE *cache;
};

struct jit_config_t {
    bool cache;
    bool report;
    char *program;
    struct riscv_jit_t *jit;
};

extern struct jit_config_t *jit_config;
// used for import solver
typedef struct {
    char *name;
    void *func;
} rv_func;

// used for codegen
typedef struct {
    char *src;
    size_t cur;
    size_t size;
    size_t capacity;
} rv_buffer;

extern rv_func import_funcs[];

struct jit_config_t *jit_config_init();

void jit_set_file_name(struct jit_config_t *config, const char *opt_prog_name);
struct riscv_jit_t *rv_jit_init(uint32_t bits);

void rv_jit_free(struct riscv_jit_t *jit);

void jit_handler(int sig);

struct block_map_t *block_map_alloc(uint32_t bits);

struct block_t *block_find_or_translate(struct riscv_t *rv,
                                        struct block_t *prev);