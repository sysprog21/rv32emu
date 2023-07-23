/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "c2mir/c2mir.h"
#include "cache.h"
#include "compile.h"
#include "decode.h"
#include "mir-gen.h"
#include "mir.h"
#include "riscv_private.h"
#include "utils.h"

/* Provide for c2mir to retrieve the generated code string. */
typedef struct {
    char *code;
    size_t code_size;
    size_t curr; /**< the current pointer to code string */
} code_string_t;

/* mir module */
typedef struct {
    MIR_context_t ctx;
    struct c2mir_options *options;
    uint8_t debug_level;
    uint8_t optimize_level;
} riscv_jit_t;

typedef struct {
    char *name;
    void *func;
} func_obj_t;

#define SET_SIZE_BITS 10
#define SET_SIZE 1 << SET_SIZE_BITS
#define SET_SLOTS_SIZE 32
#define HASH(val) \
    (((val) * (GOLDEN_RATIO_32)) >> (32 - SET_SIZE_BITS)) & ((SET_SIZE) -1)
#define sys_icache_invalidate(addr, size) \
    __builtin___clear_cache((char *) (addr), (char *) (addr) + (size));

/*
 * The set consists of SET_SIZE buckets, with each bucket containing
 * SET_SLOTS_SIZE slots.
 */
typedef struct {
    uint32_t table[SET_SIZE][SET_SLOTS_SIZE];
} set_t;

/**
 * set_reset - clear a set
 * @set: a pointer points to target set
 */
static inline void set_reset(set_t *set)
{
    memset(set, 0, sizeof(set_t));
}

/**
 * set_add - insert a new element into the set
 * @set: a pointer points to target set
 * @key: the key of the inserted entry
 */
static bool set_add(set_t *set, uint32_t key)
{
    const uint32_t index = HASH(key);
    uint8_t count = 0;
    while (set->table[index][count]) {
        if (set->table[index][count++] == key)
            return false;
    }

    set->table[index][count] = key;
    return true;
}

/**
 * set_has - check whether the element exist in the set or not
 * @set: a pointer points to target set
 * @key: the key of the inserted entry
 */
static bool set_has(set_t *set, uint32_t key)
{
    const uint32_t index = HASH(key);
    for (uint8_t count = 0; set->table[index][count]; count++) {
        if (set->table[index][count] == key)
            return true;
    }
    return false;
}

static bool insn_is_branch(uint8_t opcode)
{
    switch (opcode) {
#define _(inst, can_branch) IIF(can_branch)(case rv_insn_##inst:, )
        RISCV_INSN_LIST
#undef _
        return true;
    }
    return false;
}

typedef void (*gen_func_t)(riscv_t *, rv_insn_t *, char *);
static gen_func_t dispatch_table[N_RV_INSN];
static char funcbuf[128] = {0};
#define GEN(...)                   \
    sprintf(funcbuf, __VA_ARGS__); \
    strcat(gencode, funcbuf);
#define UPDATE_PC(inc) GEN("  rv->PC += %d;\n", inc)
#define NEXT_INSN(target) GEN("  goto insn_%x;\n", target)
#define RVOP(inst, code)                                                     \
    static void gen_##inst(riscv_t *rv UNUSED, rv_insn_t *ir, char *gencode) \
    {                                                                        \
        GEN("insn_%x:\n"                                                     \
            "  rv->X[0] = 0;\n"                                              \
            "  rv->csr_cycle++;\n",                                          \
            (ir->pc));                                                       \
        code;                                                                \
        if (!insn_is_branch(ir->opcode)) {                                   \
            GEN("  rv->PC += %d;\n", ir->insn_len);                          \
            NEXT_INSN(ir->pc + ir->insn_len);                                \
        }                                                                    \
    }

#include "jit_template.c"
/*
 * In the decoding and emulation stage, specific information is stored in the
 * IR, such as register numbers and immediates. We can leverage this information
 * to generate more efficient code instead of relying on the original source
 * code.
 */
RVOP(jal, {
    GEN("  pc = rv->PC;\n");
    UPDATE_PC(ir->imm);
    if (ir->rd) {
        GEN("  rv->X[%u] = pc + %u;\n", ir->rd, ir->insn_len);
    }
    NEXT_INSN(ir->pc + ir->imm);
})

#define BRNACH_FUNC(type, comp)                                               \
    GEN("  if ((%s) rv->X[%u] %s (%s) rv->X[%u]) {\n", #type, ir->rs1, #comp, \
        #type, ir->rs2);                                                      \
    UPDATE_PC(ir->imm);                                                       \
    if (ir->branch_taken) {                                                   \
        block_t *block = cache_get(rv->block_cache, ir->pc + ir->imm);        \
        if (!block) {                                                         \
            ir->branch_taken = NULL;                                          \
            GEN("    return true;\n");                                        \
        } else {                                                              \
            if (ir->branch_taken->pc != ir->pc + ir->imm)                     \
                ir->branch_taken = block->ir;                                 \
            NEXT_INSN(ir->pc + ir->imm);                                      \
        }                                                                     \
    } else {                                                                  \
        GEN("    return true;\n");                                            \
    }                                                                         \
    GEN("  }\n");                                                             \
    UPDATE_PC(ir->insn_len);                                                  \
    if (ir->branch_untaken) {                                                 \
        block_t *block = cache_get(rv->block_cache, ir->pc + ir->insn_len);   \
        if (!block) {                                                         \
            ir->branch_untaken = NULL;                                        \
            GEN("    return true;\n");                                        \
        } else {                                                              \
            if (ir->branch_untaken->pc != ir->pc + ir->insn_len)              \
                ir->branch_untaken = block->ir;                               \
            NEXT_INSN(ir->pc + ir->insn_len);                                 \
        }                                                                     \
    } else {                                                                  \
        GEN("  return true;\n");                                              \
    }

RVOP(beq, { BRNACH_FUNC(uint32_t, ==); })

RVOP(bne, { BRNACH_FUNC(uint32_t, !=); })

RVOP(blt, { BRNACH_FUNC(int32_t, <); })

RVOP(bge, { BRNACH_FUNC(int32_t, >=); })

RVOP(bltu, { BRNACH_FUNC(uint32_t, <); })

RVOP(bgeu, { BRNACH_FUNC(uint32_t, >=); })

RVOP(lb, {
    GEN("  addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);
    GEN("  rv->X[%u] = sign_extend_b(*((const uint8_t *) (m->mem_base + "
        "addr)));\n",
        ir->rd);
})

RVOP(lh, {
    GEN("  addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);
    GEN("  rv->X[%u] = sign_extend_h(*((const uint16_t *) (m->mem_base + "
        "addr)));\n",
        ir->rd);
})

#define MEMORY_FUNC(type, IO)                                                  \
    GEN("  addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);                       \
    IIF(IO)                                                                    \
    (GEN("  rv->X[%u] = *((const %s *) (m->mem_base + addr));\n", ir->rd,      \
         #type),                                                               \
     GEN("  *((%s *) (m->mem_base + addr)) = (%s) rv->X[%u];\n", #type, #type, \
         ir->rs2));

RVOP(lw, {MEMORY_FUNC(uint32_t, 1)})

RVOP(lbu, {MEMORY_FUNC(uint8_t, 1)})

RVOP(lhu, {MEMORY_FUNC(uint16_t, 1)})

RVOP(sb, {MEMORY_FUNC(uint8_t, 0)})

RVOP(sh, {MEMORY_FUNC(uint16_t, 0)})

RVOP(sw, {MEMORY_FUNC(uint32_t, 0)})

#if RV32_HAS(EXT_F)
RVOP(flw, {
    GEN("  addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);
    GEN("  rv->F_int[%u] = *((const uint32_t *) (m->mem_base + addr));\n",
        ir->rd);
})

/* FSW */
RVOP(fsw, {
    GEN("  addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);
    GEN("  *((uint32_t *) (m->mem_base + addr)) = rv->F_int[%u];\n", ir->rs2);
})
#endif

#if RV32_HAS(EXT_C)
RVOP(clw, {
    GEN("  addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);
    GEN("  rv->X[%u] = *((const uint32_t *) (m->mem_base + addr));\n", ir->rd);
})

RVOP(csw, {
    GEN("  addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);
    GEN("  *((uint32_t *) (m->mem_base + addr)) = rv->X[%u];\n", ir->rs2);
})

RVOP(cjal, {
    GEN("  rv->X[1] = rv->PC + %u;\n", ir->insn_len);
    UPDATE_PC(ir->imm);
    NEXT_INSN(ir->pc + ir->imm);
})

RVOP(cj, {
    UPDATE_PC(ir->imm);
    NEXT_INSN(ir->pc + ir->imm);
})

RVOP(cbeqz, {
    GEN("  if (!rv->X[%u]){\n", ir->rs1);
    UPDATE_PC(ir->imm);
    if (ir->branch_taken) {
        block_t *block = cache_get(rv->block_cache, ir->pc + ir->imm);
        if (!block) {
            ir->branch_taken = NULL;
            GEN("    return true;\n");
        } else {
            if (ir->branch_taken->pc != ir->pc + ir->imm)
                ir->branch_taken = block->ir;
            NEXT_INSN(ir->pc + ir->imm);
        }
    } else {
        GEN("    return true;\n");
    }
    GEN("  }\n");
    UPDATE_PC(ir->insn_len);
    if (ir->branch_untaken) {
        block_t *block = cache_get(rv->block_cache, ir->pc + ir->insn_len);
        if (!block) {
            ir->branch_untaken = NULL;
            GEN("    return true;\n");
        } else {
            if (ir->branch_untaken->pc != ir->pc + ir->insn_len)
                ir->branch_untaken = block->ir;
            NEXT_INSN(ir->pc + ir->insn_len);
        }
    } else {
        GEN("  return true;\n");
    }
})

RVOP(cbnez, {
    GEN("  if (rv->X[%u]){\n", ir->rs1);
    UPDATE_PC(ir->imm);
    if (ir->branch_taken) {
        block_t *block = cache_get(rv->block_cache, ir->pc + ir->imm);
        if (!block) {
            ir->branch_taken = NULL;
            GEN("    return true;\n");
        } else {
            if (ir->branch_taken->pc != ir->pc + ir->imm)
                ir->branch_taken = block->ir;
            NEXT_INSN(ir->pc + ir->imm);
        }
    } else {
        GEN("    return true;\n");
    }
    GEN("  }\n");
    UPDATE_PC(ir->insn_len);
    if (ir->branch_untaken) {
        block_t *block = cache_get(rv->block_cache, ir->pc + ir->insn_len);
        if (!block) {
            ir->branch_untaken = NULL;
            GEN("    return true;\n");
        } else {
            if (ir->branch_untaken->pc != ir->pc + ir->insn_len)
                ir->branch_untaken = block->ir;
            NEXT_INSN(ir->pc + ir->insn_len);
        }
    } else {
        GEN("  return true;\n");
    }
})

RVOP(clwsp, {
    GEN("addr = rv->X[rv_reg_sp] + %u;\n", ir->imm);
    GEN("  rv->X[%u] = *((const uint32_t *) (m->mem_base + addr));\n", ir->rd);
})

RVOP(cswsp, {
    GEN("addr = rv->X[rv_reg_sp] + %u;\n", ir->imm);
    GEN("  *((uint32_t *) (m->mem_base + addr)) = rv->X[%u];\n", ir->rs2);
})
#endif


RVOP(fuse3, {
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        GEN("  addr = rv->X[%u] + %u;\n", fuse[i].rs1, fuse[i].imm);
        GEN("  *((uint32_t *) (m->mem_base + addr)) = rv->X[%u];\n",
            fuse[i].rs2)
    }
})

RVOP(fuse4, {
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        GEN("  addr = rv->X[%u] + %u;\n", fuse[i].rs1, fuse[i].imm);
        GEN("  rv->X[%u] = *((const uint32_t *) (m->mem_base + addr));\n",
            fuse[i].rd);
    }
})
#undef RVOP

static void trace_ebb(riscv_t *rv, char *gencode, rv_insn_t *ir, set_t *set)
{
    while (1) {
        if (set_add(set, ir->pc))
            dispatch_table[ir->opcode](rv, ir, gencode);

        if (ir->tailcall)
            break;
        ir++;
    }
    if (ir->branch_untaken && !set_has(set, ir->branch_untaken->pc))
        trace_ebb(rv, gencode, ir->branch_untaken, set);
    if (ir->branch_taken && !set_has(set, ir->branch_taken->pc))
        trace_ebb(rv, gencode, ir->branch_taken, set);
}

#define EPILOGUE "}"

static void trace_and_gencode(riscv_t *rv, char *gencode)
{
#define _(inst, can_branch) dispatch_table[rv_insn_##inst] = &gen_##inst;
    RISCV_INSN_LIST
#undef _
    set_t set;
    strcat(gencode, PROLOGUE);
    set_reset(&set);
    block_t *block = cache_get(rv->block_cache, rv->PC);
    trace_ebb(rv, gencode, block->ir, &set);
    strcat(gencode, EPILOGUE);
}

static int get_string_func(void *data)
{
    code_string_t *codestr = data;
    return codestr->curr >= codestr->code_size ? EOF
                                               : codestr->code[codestr->curr++];
}

#define DLIST_ITEM_FOREACH(modules, item)                     \
    for (item = DLIST_HEAD(MIR_item_t, modules->items); item; \
         item = DLIST_NEXT(MIR_item_t, item))

/* parse the funcitons are not defined in mir */
static void *import_resolver(const char *name)
{
    func_obj_t func_list[] = {
        {"sign_extend_b", sign_extend_b},
        {"sign_extend_h", sign_extend_h},
#if RV32_HAS(Zicsr)
        {"csr_csrrw", csr_csrrw},
        {"csr_csrrs", csr_csrrs},
        {"csr_csrrc", csr_csrrc},
#endif
#if RV32_HAS(EXT_F)
        {"isnanf", isnanf},
        {"isinff", isinff},
        {"sqrtf", sqrtf},
        {"calc_fclass", calc_fclass},
        {"is_nan", is_nan},
        {"is_snan", is_snan},
#endif
        {NULL, NULL},
    };
    for (int i = 0; func_list[i].name; i++) {
        if (!strcmp(name, func_list[i].name))
            return func_list[i].func;
    }
    return NULL;
}

static riscv_jit_t *jit = NULL;
static code_string_t *jit_code_string = NULL;

/* TODO: fix the segmentation fault error that occurs when invoking a function
 * compiled by mir while running on Apple M1 MacOS.
 */
static uint8_t *compile(riscv_t *rv)
{
    char func_name[25];
    snprintf(func_name, 25, "jit_func_%d", rv->PC);
    c2mir_init(jit->ctx);
    size_t gen_num = 0;
    MIR_gen_init(jit->ctx, gen_num);
    MIR_gen_set_optimize_level(jit->ctx, gen_num, jit->optimize_level);
    if (!c2mir_compile(jit->ctx, jit->options, get_string_func, jit_code_string,
                       func_name, NULL)) {
        perror("Compile failure");
        exit(EXIT_FAILURE);
    }
    MIR_module_t module =
        DLIST_TAIL(MIR_module_t, *MIR_get_module_list(jit->ctx));
    MIR_load_module(jit->ctx, module);
    MIR_link(jit->ctx, MIR_set_gen_interface, import_resolver);
    MIR_item_t func = DLIST_HEAD(MIR_item_t, module->items);
    uint8_t *code = NULL;
    size_t func_len = DLIST_LENGTH(MIR_item_t, module->items);
    for (size_t i = 0; i < func_len; i++, func = DLIST_NEXT(MIR_item_t, func)) {
        if (func->item_type == MIR_func_item)
            code = func->addr;
    }

    MIR_gen_finish(jit->ctx);
    c2mir_finish(jit->ctx);
    return code;
}

uint8_t *block_compile(riscv_t *rv)
{
    if (!jit) {
        jit = calloc(1, sizeof(riscv_jit_t));
        jit->options = calloc(1, sizeof(struct c2mir_options));
        jit->ctx = MIR_init();
        jit->optimize_level = 1;
    }

    if (!jit_code_string) {
        jit_code_string = malloc(sizeof(code_string_t));
        jit_code_string->code = calloc(1, 1024 * 1024);
    } else {
        memset(jit_code_string->code, 0, 1024 * 1024);
    }
    jit_code_string->curr = 0;
    trace_and_gencode(rv, jit_code_string->code);
    jit_code_string->code_size = strlen(jit_code_string->code);
    return compile(rv);
}