/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

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

typedef struct {
    char *code;
    size_t code_size;
    size_t curr;
} jit_item_t;

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
#define HASH(val) \
    (((val) * (GOLDEN_RATIO_32)) >> (32 - SET_SIZE_BITS)) & ((SET_SIZE) -1)

typedef struct {
    uint32_t table[SET_SIZE][32];
} set_t;

static inline void set_reset(set_t *set)
{
    memset(set, 0, sizeof(set_t));
}

static bool set_add(set_t *set, uint32_t key)
{
    uint32_t index = HASH(key);
    uint8_t count = 0;
    while (set->table[index][count]) {
        if (set->table[index][count++] == key)
            return false;
    }

    set->table[index][count] = key;
    return true;
}

static bool set_has(set_t *set, uint32_t key)
{
    uint32_t index = HASH(key);
    uint8_t count = 0;
    while (set->table[index][count]) {
        if (set->table[index][count++] == key)
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

typedef void (*gen_func_t)(riscv_t *, const rv_insn_t *, char *, uint32_t);
static gen_func_t dispatch_table[N_RV_INSN];
static char funcbuf[128] = {0};
#define GEN(...)                   \
    sprintf(funcbuf, __VA_ARGS__); \
    strcat(gencode, funcbuf);
#define UPDATE_PC(inc) GEN("  rv->PC += %d;\n", inc)
#define NEXT_INSN(target) GEN("  goto insn_%x;\n", target)
#define RVOP(inst, code)                                            \
    static void gen_##inst(riscv_t *rv UNUSED, const rv_insn_t *ir, \
                           char *gencode, uint32_t pc)              \
    {                                                               \
        GEN("insn_%x:\n"                                            \
            "  rv->X[0] = 0;\n"                                     \
            "  rv->csr_cycle++;\n",                                 \
            (pc));                                                  \
        code;                                                       \
        if (!insn_is_branch(ir->opcode)) {                          \
            GEN("  rv->PC += ir->insn_len;\n");                     \
            GEN("  ir = ir + 1;\n");                                \
            NEXT_INSN(pc + ir->insn_len);                           \
        }                                                           \
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
    GEN("  ir = ir->branch_taken;\n");
    NEXT_INSN(pc + ir->imm);
})

#define BRNACH_FUNC(type, comp)                                               \
    GEN("  if ((%s) rv->X[%u] %s (%s) rv->X[%u]) {\n", #type, ir->rs1, #comp, \
        #type, ir->rs2);                                                      \
    UPDATE_PC(ir->imm);                                                       \
    if (ir->branch_taken) {                                                   \
        GEN("    ir = ir->branch_taken;\n");                                  \
        NEXT_INSN(pc + ir->imm);                                              \
    } else                                                                    \
        GEN("    return true;\n");                                            \
    GEN("  }\n");                                                             \
    UPDATE_PC(ir->insn_len);                                                  \
    if (ir->branch_untaken) {                                                 \
        GEN("  ir = ir->branch_untaken;\n");                                  \
        NEXT_INSN(pc + ir->insn_len);                                         \
    } else                                                                    \
        GEN("  return true;\n");

RVOP(beq, { BRNACH_FUNC(uint32_t, ==); })

RVOP(bne, { BRNACH_FUNC(uint32_t, !=); })

RVOP(blt, { BRNACH_FUNC(int32_t, <); })

RVOP(bge, { BRNACH_FUNC(int32_t, >=); })

RVOP(bltu, { BRNACH_FUNC(uint32_t, <); })

RVOP(bgeu, { BRNACH_FUNC(uint32_t, >=); })

RVOP(lb, {
    GEN("  addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);
    GEN("  c = m->chunks[addr >> 16];\n");
    GEN("  rv->X[%u] = sign_extend_b(* (const uint32_t *) (c->data + "
        "(addr & 0xffff)));\n",
        ir->rd);
})

RVOP(lh, {
    GEN("  addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);
    GEN("  c = m->chunks[addr >> 16];\n");
    GEN("  rv->X[%u] = sign_extend_h(* (const uint32_t *) (c->data + "
        "(addr & 0xffff)));\n",
        ir->rd);
})

#define MEMORY_FUNC(type, IO)                                                \
    GEN("  addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);                     \
    GEN("  c = m->chunks[addr >> 16];\n");                                   \
    IIF(IO)                                                                  \
    (GEN("  rv->X[%u] = * (const %s *) (c->data + (addr & 0xffff));\n",      \
         ir->rd, #type),                                                     \
     GEN("  *(%s *) (c->data + (addr & 0xffff)) = (%s) rv->X[%u];\n", #type, \
         #type, ir->rs2));

RVOP(lw, {MEMORY_FUNC(uint32_t, 1)})

RVOP(lbu, {MEMORY_FUNC(uint8_t, 1)})

RVOP(lhu, {MEMORY_FUNC(uint16_t, 1)})

RVOP(sb, {MEMORY_FUNC(uint8_t, 0)})

RVOP(sh, {MEMORY_FUNC(uint16_t, 0)})

RVOP(sw, {MEMORY_FUNC(uint32_t, 0)})

#if RV32_HAS(EXT_C)
RVOP(clw, {
    GEN("  addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);
    GEN("  c = m->chunks[addr >> 16];\n");
    GEN("  rv->X[%u] = * (const uint32_t *) (c->data + (addr & "
        "0xffff));\n",
        ir->rd);
})

RVOP(csw, {
    GEN("  addr = rv->X[%u] + %u;\n", ir->rs1, ir->imm);
    GEN("  c = m->chunks[addr >> 16];\n");
    GEN("  *(uint32_t *) (c->data + (addr & 0xffff)) = rv->X[%u];\n", ir->rs2);
})

RVOP(cjal, {
    GEN("  rv->X[1] = rv->PC += %u;\n", ir->insn_len);
    UPDATE_PC(ir->imm);
    GEN("  ir = ir->branch_taken;\n");
    NEXT_INSN(pc + ir->imm);
})

RVOP(cj, {
    UPDATE_PC(ir->imm);
    GEN("  ir = ir->branch_taken;\n");
    NEXT_INSN(pc + ir->imm);
})

RVOP(cbeqz, {
    GEN("  if (!rv->X[%u]){\n", ir->rs1);
    UPDATE_PC(ir->imm);
    if (ir->branch_taken) {
        GEN("    ir = ir->branch_taken;\n");
        NEXT_INSN(pc + ir->imm);
    } else
        GEN("    return true;\n");
    GEN("  }\n");
    UPDATE_PC(ir->insn_len);
    if (ir->branch_untaken) {
        GEN("  ir = ir->branch_untaken;\n");
        NEXT_INSN(pc + ir->insn_len);
    } else
        GEN("  return true;\n");
})

RVOP(cbnez, {
    GEN("  if (rv->X[%u]){\n", ir->rs1);
    UPDATE_PC(ir->imm);
    if (ir->branch_taken) {
        GEN("    ir = ir->branch_taken;\n");
        NEXT_INSN(pc + ir->imm);
    } else
        GEN("    return true;\n");
    GEN("  }\n");
    UPDATE_PC(ir->insn_len);
    if (ir->branch_untaken) {
        GEN("  ir = ir->branch_untaken;\n");
        NEXT_INSN(pc + ir->insn_len);
    } else
        GEN("  return true;\n");
})
#endif

RVOP(fuse3, {
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        GEN("  addr = rv->X[%u] + %u;\n", fuse[i].rs1, fuse[i].imm);
        GEN("  c = m->chunks[addr >> 16];\n");
        GEN("  *(uint32_t *) (c->data + (addr & 0xffff)) = rv->X[%u];\n",
            fuse[i].rs2);
    }
})

RVOP(fuse4, {
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        GEN("  addr = rv->X[%u] + %u;\n", fuse[i].rs1, fuse[i].imm);
        GEN("  c = m->chunks[addr >> 16];\n");
        GEN("  rv->X[%u] = * (const uint32_t *) (c->data + (addr & "
            "0xffff));\n",
            fuse[i].rd);
    }
})
#undef RVOP

static void trace_ebb(riscv_t *rv, char *gencode, rv_insn_t *ir, set_t *set)
{
    while (1) {
        if (set_add(set, ir->pc))
            dispatch_table[ir->opcode](rv, ir, gencode, ir->pc);

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
    block_t *block = cache_get(rv->cache, rv->PC);
    trace_ebb(rv, gencode, block->ir, &set);
    strcat(gencode, EPILOGUE);
}

static int get_func(void *data)
{
    jit_item_t *item = data;
    return item->curr >= item->code_size ? EOF : item->code[item->curr++];
}

#define DLIST_ITEM_FOREACH(modules, item)                     \
    for (item = DLIST_HEAD(MIR_item_t, modules->items); item; \
         item = DLIST_NEXT(MIR_item_t, item))

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
static jit_item_t *jit_ptr = NULL;

/* TODO: fix error when running on ARM architecture */
static uint8_t *compile(riscv_t *rv)
{
    char func_name[25];
    snprintf(func_name, 25, "jit_func_%d", rv->PC);
    c2mir_init(jit->ctx);
    size_t gen_num = 0;
    MIR_gen_init(jit->ctx, gen_num);
    MIR_gen_set_optimize_level(jit->ctx, gen_num, jit->optimize_level);
    if (!c2mir_compile(jit->ctx, jit->options, get_func, jit_ptr, func_name,
                       NULL)) {
        perror("Compile failure");
        exit(EXIT_FAILURE);
    }
    MIR_module_t module =
        DLIST_TAIL(MIR_module_t, *MIR_get_module_list(jit->ctx));
    MIR_load_module(jit->ctx, module);
    MIR_link(jit->ctx, MIR_set_gen_interface, import_resolver);
    MIR_item_t func = DLIST_HEAD(MIR_item_t, module->items);
    size_t code_len = 0;
    size_t func_len = DLIST_LENGTH(MIR_item_t, module->items);
    for (size_t i = 0; i < func_len; i++, func = DLIST_NEXT(MIR_item_t, func)) {
        if (func->item_type == MIR_func_item) {
            uint32_t *tmp = (uint32_t *) func->addr;
            while (*tmp) {
                code_len += 4;
                tmp += 1;
            }
            break;
        }
    }

    MIR_gen_finish(jit->ctx);
    c2mir_finish(jit->ctx);
    return code_cache_add(rv->cache, rv->PC, func->addr, code_len, 4);
}

uint8_t *block_compile(riscv_t *rv)
{
    if (!jit) {
        jit = calloc(1, sizeof(riscv_jit_t));
        jit->options = calloc(1, sizeof(struct c2mir_options));
        jit->ctx = MIR_init();
        jit->optimize_level = 1;
    }

    if (!jit_ptr) {
        jit_ptr = malloc(sizeof(jit_item_t));
        jit_ptr->curr = 0;
        jit_ptr->code = calloc(1, 1024 * 1024);
    } else {
        jit_ptr->curr = 0;
        memset(jit_ptr->code, 0, 1024 * 1024);
    }
    trace_and_gencode(rv, jit_ptr->code);
    jit_ptr->code_size = strlen(jit_ptr->code);
    return compile(rv);
}