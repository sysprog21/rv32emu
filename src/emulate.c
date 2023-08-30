/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if RV32_HAS(EXT_F)
#include <math.h>
#include "softfloat.h"
#endif /* RV32_HAS(EXT_F) */

#if RV32_HAS(GDBSTUB)
extern struct target_ops gdbstub_ops;
#endif

#include "decode.h"
#include "riscv.h"
#include "riscv_private.h"
#include "state.h"
#include "utils.h"
#if RV32_HAS(JIT)
#include "cache.h"
#include "compile.h"
#endif

/* RISC-V exception code list */
#define RV_EXCEPTION_LIST                                       \
    _(insn_misaligned, 0)  /* Instruction address misaligned */ \
    _(illegal_insn, 2)     /* Illegal instruction */            \
    _(breakpoint, 3)       /* Breakpoint */                     \
    _(load_misaligned, 4)  /* Load address misaligned */        \
    _(store_misaligned, 6) /* Store/AMO address misaligned */   \
    _(ecall_M, 11)         /* Environment call from M-mode */

enum {
#define _(type, code) rv_exception_code##type = code,
    RV_EXCEPTION_LIST
#undef _
};

static void rv_exception_default_handler(riscv_t *rv)
{
    rv->csr_mepc += rv->compressed ? INSN_16 : INSN_32;
    rv->PC = rv->csr_mepc; /* mret */
}

#define EXCEPTION_HANDLER_IMPL(type, code)                                \
    static void rv_except_##type(riscv_t *rv, uint32_t mtval)             \
    {                                                                     \
        /* mtvec (Machine Trap-Vector Base Address Register)              \
         * mtvec[MXLEN-1:2]: vector base address                          \
         * mtvec[1:0] : vector mode                                       \
         */                                                               \
        const uint32_t base = rv->csr_mtvec & ~0x3;                       \
        const uint32_t mode = rv->csr_mtvec & 0x3;                        \
        /* mepc  (Machine Exception Program Counter)                      \
         * mtval (Machine Trap Value Register)                            \
         * mcause (Machine Cause Register): store exception code          \
         */                                                               \
        rv->csr_mepc = rv->PC;                                            \
        rv->csr_mtval = mtval;                                            \
        rv->csr_mcause = code;                                            \
        if (!rv->csr_mtvec) { /* in case CSR is not configured */         \
            rv_exception_default_handler(rv);                             \
            return;                                                       \
        }                                                                 \
        switch (mode) {                                                   \
        case 0: /* DIRECT: All exceptions set PC to base */               \
            rv->PC = base;                                                \
            break;                                                        \
        /* VECTORED: Asynchronous interrupts set PC to base + 4 * code */ \
        case 1:                                                           \
            rv->PC = base + 4 * code;                                     \
            break;                                                        \
        }                                                                 \
    }

/* RISC-V exception handlers */
#define _(type, code) EXCEPTION_HANDLER_IMPL(type, code)
RV_EXCEPTION_LIST
#undef _

/* wrap load/store and insn misaligned handler
 * @mask_or_pc: mask for load/store and pc for insn misaligned handler.
 * @type: type of misaligned handler
 * @compress: compressed instruction or not
 * @IO: whether the misaligned handler is for load/store or insn.
 */
#define RV_EXC_MISALIGN_HANDLER(mask_or_pc, type, compress, IO)       \
    IIF(IO)                                                           \
    (if (!rv->io.allow_misalign && unlikely(addr & (mask_or_pc))),    \
     if (unlikely(insn_is_misaligned(rv->PC))))                       \
    {                                                                 \
        rv->compressed = compress;                                    \
        rv_except_##type##_misaligned(rv, IIF(IO)(addr, mask_or_pc)); \
        return false;                                                 \
    }

#if RV32_HAS(GDBSTUB)
void rv_debug(riscv_t *rv)
{
    if (!gdbstub_init(&rv->gdbstub, &gdbstub_ops,
                      (arch_info_t){
                          .reg_num = 33,
                          .reg_byte = 4,
                          .target_desc = TARGET_RV32,
                      },
                      GDBSTUB_COMM)) {
        return;
    }

    rv->debug_mode = true;
    rv->breakpoint_map = breakpoint_map_new();
    rv->is_interrupted = false;

    if (!gdbstub_run(&rv->gdbstub, (void *) rv))
        return;

    breakpoint_map_destroy(rv->breakpoint_map);
    gdbstub_close(&rv->gdbstub);
}
#endif /* RV32_HAS(GDBSTUB) */

static inline bool insn_is_misaligned(uint32_t pc)
{
    return (pc &
#if RV32_HAS(EXT_C)
            0x1
#else
            0x3
#endif
    );
}

/* can-branch information for each RISC-V instruction */
enum {
#define _(inst, can_branch) __rv_insn_##inst##_canbranch = can_branch,
    RISCV_INSN_LIST
#undef _
};

#if RV32_HAS(GDBSTUB)
#define RVOP_NO_NEXT(ir) (ir->tailcall || rv->debug_mode)
#else
#define RVOP_NO_NEXT(ir) (ir->tailcall)
#endif

/* record whether the branch is taken or not during emulation */
static bool branch_taken = false;

/* record the program counter of the previous block */
static uint32_t last_pc = 0;

/* record whether the block is replaced by cache. If so, clear the EBB
 * information */
static bool clear_flag = false;

/* Interpreter-based execution path */
#define RVOP(inst, code)                                    \
    static bool do_##inst(riscv_t *rv, const rv_insn_t *ir) \
    {                                                       \
        rv->X[rv_reg_zero] = 0;                             \
        rv->csr_cycle++;                                    \
        code;                                               \
    nextop:                                                 \
        rv->PC += ir->insn_len;                             \
        if (unlikely(RVOP_NO_NEXT(ir)))                     \
            return true;                                    \
        const rv_insn_t *next = ir + 1;                     \
        MUST_TAIL return next->impl(rv, next);              \
    }

#include "rv32_template.c"
#undef RVOP

/* FIXME: Add JIT-based execution path */

/* AUIPC + ADDI */
static bool do_fuse1(riscv_t *rv, const rv_insn_t *ir)
{
    rv->X[rv_reg_zero] = 0;
    rv->csr_cycle += 2;
    rv->X[ir->rd] = rv->PC + ir->imm;
    rv->X[ir->rs1] = rv->X[ir->rd] + ir->imm2;
    rv->PC += 2 * ir->insn_len;
    if (unlikely(RVOP_NO_NEXT(ir)))
        return true;
    const rv_insn_t *next = ir + 2;
    MUST_TAIL return next->impl(rv, next);
}

/* AUIPC + ADD */
static bool do_fuse2(riscv_t *rv, const rv_insn_t *ir)
{
    rv->X[rv_reg_zero] = 0;
    rv->csr_cycle += 2;
    rv->X[ir->rd] = rv->PC + ir->imm;
    rv->X[ir->rs2] = rv->X[ir->rd] + rv->X[ir->rs1];
    rv->PC += 2 * ir->insn_len;
    if (unlikely(RVOP_NO_NEXT(ir)))
        return true;
    const rv_insn_t *next = ir + 2;
    MUST_TAIL return next->impl(rv, next);
}

/* multiple SW */
static bool do_fuse3(riscv_t *rv, const rv_insn_t *ir)
{
    rv->X[rv_reg_zero] = 0;
    rv->csr_cycle += ir->imm2;
    opcode_fuse_t *fuse = ir->fuse;
    uint32_t addr = rv->X[fuse[0].rs1] + fuse[0].imm;
    /* The memory addresses of the sw instructions are contiguous, thus only
     * the first SW instruction needs to be checked to determine if its memory
     * address is misaligned or if the memory chunk does not exist.
     */
    RV_EXC_MISALIGN_HANDLER(3, store, false, 1);
    rv->io.mem_write_w(addr, rv->X[fuse[0].rs2]);
    for (int i = 1; i < ir->imm2; i++) {
        addr = rv->X[fuse[i].rs1] + fuse[i].imm;
        rv->io.mem_write_w(addr, rv->X[fuse[i].rs2]);
    }
    rv->PC += ir->imm2 * ir->insn_len;
    if (unlikely(RVOP_NO_NEXT(ir)))
        return true;
    const rv_insn_t *next = ir + ir->imm2;
    MUST_TAIL return next->impl(rv, next);
}

/* multiple LW */
static bool do_fuse4(riscv_t *rv, const rv_insn_t *ir)
{
    rv->X[rv_reg_zero] = 0;
    rv->csr_cycle += ir->imm2;
    opcode_fuse_t *fuse = ir->fuse;
    uint32_t addr = rv->X[fuse[0].rs1] + fuse[0].imm;
    /* The memory addresses of the lw instructions are contiguous, therefore
     * only the first LW instruction needs to be checked to determine if its
     * memory address is misaligned or if the memory chunk does not exist.
     */
    RV_EXC_MISALIGN_HANDLER(3, load, false, 1);
    rv->X[fuse[0].rd] = rv->io.mem_read_w(addr);
    for (int i = 1; i < ir->imm2; i++) {
        addr = rv->X[fuse[i].rs1] + fuse[i].imm;
        rv->X[fuse[i].rd] = rv->io.mem_read_w(addr);
    }
    rv->PC += ir->imm2 * ir->insn_len;
    if (unlikely(RVOP_NO_NEXT(ir)))
        return true;
    const rv_insn_t *next = ir + ir->imm2;
    MUST_TAIL return next->impl(rv, next);
}

/* LUI + ADDI */
static bool do_fuse5(riscv_t *rv, const rv_insn_t *ir)
{
    rv->X[rv_reg_zero] = 0;
    rv->csr_cycle += 2;
    rv->X[ir->rd] = ir->imm;
    rv->X[ir->rs1] = ir->imm + ir->imm2;
    rv->PC += 2 * ir->insn_len;
    if (unlikely(RVOP_NO_NEXT(ir)))
        return true;
    const rv_insn_t *next = ir + 2;
    MUST_TAIL return next->impl(rv, next);
}

/* clang-format off */
static const void *dispatch_table[] = {
/* RV32 instructions */
#define _(inst, can_branch) [rv_insn_##inst] = do_##inst,
    RISCV_INSN_LIST
#undef _
/* Macro operation fusion instructions */
#define _(inst) [rv_insn_##inst] = do_##inst,
    FUSE_INSN_LIST
#undef _
};
/* clang-format on */

static inline bool insn_is_branch(uint8_t opcode)
{
    switch (opcode) {
#define _(inst, can_branch) IIF(can_branch)(case rv_insn_##inst:, )
        RISCV_INSN_LIST
#undef _
        return true;
    }
    return false;
}

static inline bool insn_is_conditional_branch(uint8_t opcode)
{
    switch (opcode) {
    case rv_insn_beq:
    case rv_insn_bne:
    case rv_insn_blt:
    case rv_insn_bltu:
    case rv_insn_bge:
    case rv_insn_bgeu:
#if RV32_HAS(EXT_C)
    case rv_insn_cbeqz:
    case rv_insn_cbnez:
#endif
        return true;
    }
    return false;
}

/* TODO: unify the hash function of cache and map */
#if !RV32_HAS(JIT)
/* hash function is used when mapping address into the block map */
static inline uint32_t hash(size_t k)
{
    k ^= k << 21;
    k ^= k >> 17;
#if (SIZE_MAX > 0xFFFFFFFF)
    k ^= k >> 35;
    k ^= k >> 51;
#endif
    return k;
}
#endif

/* allocate a basic block */
static block_t *block_alloc(const uint8_t bits)
{
    block_t *block = malloc(sizeof(struct block));
    block->insn_capacity = 1 << bits;
    block->n_insn = 0;
    block->predict = NULL;
    block->ir = malloc(block->insn_capacity * sizeof(rv_insn_t));
#if RV32_HAS(JIT)
    block->hot = false;
#endif
    return block;
}

#if !RV32_HAS(JIT)
/* insert a block into block map */
static void block_insert(block_map_t *map, const block_t *block)
{
    assert(map && block);
    const uint32_t mask = map->block_capacity - 1;
    uint32_t index = hash(block->pc_start);

    /* insert into the block map */
    for (;; index++) {
        if (!map->map[index & mask]) {
            map->map[index & mask] = (block_t *) block;
            break;
        }
    }
    map->size++;
}

/* try to locate an already translated block in the block map */
static block_t *block_find(const block_map_t *map, const uint32_t addr)
{
    assert(map);
    uint32_t index = hash(addr);
    const uint32_t mask = map->block_capacity - 1;

    /* find block in block map */
    for (;; index++) {
        block_t *block = map->map[index & mask];
        if (!block)
            return NULL;

        if (block->pc_start == addr)
            return block;
    }
    return NULL;
}
#endif

static void block_translate(riscv_t *rv, block_t *block)
{
    block->pc_start = block->pc_end = rv->PC;

    /* translate the basic block */
    while (block->n_insn < block->insn_capacity) {
        rv_insn_t *ir = block->ir + block->n_insn;
        memset(ir, 0, sizeof(rv_insn_t));

        /* fetch the next instruction */
        const uint32_t insn = rv->io.mem_ifetch(block->pc_end);

        /* decode the instruction */
        if (!rv_decode(ir, insn)) {
            rv->compressed = (ir->insn_len == INSN_16);
            rv_except_illegal_insn(rv, insn);
            break;
        }
        ir->impl = dispatch_table[ir->opcode];
#if RV32_HAS(JIT)
        ir->pc = block->pc_end;
#endif
        /* compute the end of pc */
        block->pc_end += ir->insn_len;
        block->n_insn++;
        /* stop on branch */
        if (insn_is_branch(ir->opcode))
            break;
    }
    block->ir[block->n_insn - 1].tailcall = true;
}

#define COMBINE_MEM_OPS(RW)                                                \
    count = 1;                                                             \
    next_ir = ir + 1;                                                      \
    if (next_ir->opcode != IIF(RW)(rv_insn_lw, rv_insn_sw))                \
        break;                                                             \
    sign = (ir->imm - next_ir->imm) >> 31 ? -1 : 1;                        \
    for (uint32_t j = 1; j < block->n_insn - 1 - i; j++) {                 \
        next_ir = ir + j;                                                  \
        if (next_ir->opcode != IIF(RW)(rv_insn_lw, rv_insn_sw) ||          \
            ir->rs1 != next_ir->rs1 || ir->imm - next_ir->imm != 4 * sign) \
            break;                                                         \
        count++;                                                           \
    }                                                                      \
    if (count > 1) {                                                       \
        ir->opcode = IIF(RW)(rv_insn_fuse4, rv_insn_fuse3);                \
        ir->fuse = malloc(count * sizeof(opcode_fuse_t));                  \
        ir->imm2 = count;                                                  \
        memcpy(ir->fuse, ir, sizeof(opcode_fuse_t));                       \
        ir->impl = dispatch_table[ir->opcode];                             \
        for (int j = 1; j < count; j++) {                                  \
            next_ir = ir + j;                                              \
            memcpy(ir->fuse + j, next_ir, sizeof(opcode_fuse_t));          \
        }                                                                  \
        ir->tailcall = next_ir->tailcall;                                  \
    }

/* Check if instructions in a block match a specific pattern. If they do,
 * rewrite them as fused instructions.
 *
 * Strategies are being devised to increase the number of instructions that
 * match the pattern, including possible instruction reordering.
 */
static void match_pattern(block_t *block)
{
    for (uint32_t i = 0; i < block->n_insn - 1; i++) {
        rv_insn_t *ir = block->ir + i, *next_ir = NULL;
        int32_t count = 0, sign = 1;
        switch (ir->opcode) {
        case rv_insn_auipc:
            next_ir = ir + 1;
            if (next_ir->opcode == rv_insn_addi && ir->rd == next_ir->rs1) {
                /* The destination register of the AUIPC instruction is the
                 * same as the source register 1 of the next instruction ADDI.
                 */
                ir->opcode = rv_insn_fuse1;
                ir->rs1 = next_ir->rd;
                ir->imm2 = next_ir->imm;
                ir->impl = dispatch_table[ir->opcode];
                ir->tailcall = next_ir->tailcall;
            } else if (next_ir->opcode == rv_insn_add &&
                       ir->rd == next_ir->rs2) {
                /* The destination register of the AUIPC instruction is the
                 * same as the source register 2 of the next instruction ADD.
                 */
                ir->opcode = rv_insn_fuse2;
                ir->rs2 = next_ir->rd;
                ir->rs1 = next_ir->rs1;
                ir->impl = dispatch_table[ir->opcode];
            } else if (next_ir->opcode == rv_insn_add &&
                       ir->rd == next_ir->rs1) {
                /* The destination register of the AUIPC instruction is the
                 * same as the source register 1 of the next instruction ADD.
                 */
                ir->opcode = rv_insn_fuse2;
                ir->rs2 = next_ir->rd;
                ir->rs1 = next_ir->rs2;
                ir->impl = dispatch_table[ir->opcode];
            }
            break;

        /* If the memory addresses of a sequence of store or load instructions
         * are contiguous, combine these instructions.
         */
        case rv_insn_sw:
            COMBINE_MEM_OPS(0);
            break;
        case rv_insn_lw:
            COMBINE_MEM_OPS(1);
            break;
        case rv_insn_lui:
            next_ir = ir + 1;
            if (next_ir->opcode == rv_insn_addi && ir->rd == next_ir->rs1) {
                /* The destination register of the LUI instruction is the
                 * same as the source register 1 of the next instruction ADDI.
                 */
                ir->opcode = rv_insn_fuse5;
                ir->rs1 = next_ir->rd;
                ir->imm2 = next_ir->imm;
                ir->impl = dispatch_table[ir->opcode];
                ir->tailcall = next_ir->tailcall;
            }
            break;
            /* TODO: mixture of SW and LW */
            /* TODO: reorder insturction to match pattern */
        }
    }
}

static block_t *prev = NULL;
static block_t *block_find_or_translate(riscv_t *rv)
{
#if !RV32_HAS(JIT)
    block_map_t *map = &rv->block_map;

    /* lookup the next block in the block map */
    block_t *next = block_find(map, rv->PC);
#else
    /* lookup the next block in the block cache */
    block_t *next = (block_t *) cache_get(rv->block_cache, rv->PC);
#endif

    if (!next) {
#if !RV32_HAS(JIT)
        if (map->size * 1.25 > map->block_capacity) {
            block_map_clear(map);
            prev = NULL;
        }
#endif
        /* allocate a new block */
        next = block_alloc(10);

        /* translate the basic block */
        block_translate(rv, next);
#if RV32_HAS(GDBSTUB)
        if (likely(!rv->debug_mode))
#endif
            /* macro operation fusion */
            match_pattern(next);

#if !RV32_HAS(JIT)
        /* insert the block into block map */
        block_insert(&rv->block_map, next);
#else
        /* insert the block into block cache */
        block_t *delete_target = cache_put(rv->block_cache, rv->PC, &(*next));
        if (delete_target) {
            free(delete_target->ir);
            free(delete_target);
        }
#endif
        /* update the block prediction.
         * When translating a new block, the block predictor may benefit,
         * but updating it after finding a particular block may penalize
         * significantly.
         */
        if (prev)
            prev->predict = next;
    }

    return next;
}

#if RV32_HAS(JIT)
typedef bool (*exec_block_func_t)(riscv_t *rv);
#endif

void rv_step(riscv_t *rv, int32_t cycles)
{
    assert(rv);

    /* find or translate a block for starting PC */
    const uint64_t cycles_target = rv->csr_cycle + cycles;

    /* loop until hitting the cycle target */
    while (rv->csr_cycle < cycles_target && !rv->halt) {
        block_t *block;
        /* try to predict the next block */
        if (prev && prev->predict && prev->predict->pc_start == rv->PC) {
            block = prev->predict;
        } else {
            /* lookup the next block in block map or translate a new block,
             * and move onto the next block.
             */
            block = block_find_or_translate(rv);
        }

        /* by now, a block should be available */
        assert(block);
        /* After emulating the previous block, it is determined whether the
         * branch is taken or not. The IR array of the current block is then
         * assigned to either the branch_taken or branch_untaken pointer of
         * the previous block.
         */
        if (prev) {
            /* update previous block */
            if (prev->pc_start != last_pc)
#if !RV32_HAS(JIT)
                prev = block_find(&rv->block_map, last_pc);
#else
                prev = cache_get(rv->block_cache, last_pc);
#endif
            if (prev) {
                rv_insn_t *last_ir = prev->ir + prev->n_insn - 1;
                /* chain block */
                if (insn_is_conditional_branch(last_ir->opcode)) {
                    if (branch_taken && (!last_ir->branch_taken || clear_flag))
                        last_ir->branch_taken = block->ir;
                    else if (!branch_taken &&
                             (!last_ir->branch_untaken || clear_flag))
                        last_ir->branch_untaken = block->ir;
                } else if (last_ir->opcode == rv_insn_jal
#if RV32_HAS(EXT_C)
                           || last_ir->opcode == rv_insn_cj ||
                           last_ir->opcode == rv_insn_cjal
#endif
                ) {
                    if (!last_ir->branch_taken || clear_flag)
                        last_ir->branch_taken = block->ir;
                }
            }
        }
        clear_flag = false;
        last_pc = rv->PC;

#if RV32_HAS(JIT)
        /* execute the block by JIT compiler */
        exec_block_func_t code = NULL;
        if (block->hot)
            code = (exec_block_func_t) cache_get(rv->code_cache, rv->PC);
        if (!code) {
            /* check if using frequency of block exceed threshold */
            if ((block->hot = cache_hot(rv->block_cache, block->pc_start))) {
                code = (exec_block_func_t) block_compile(rv);
                cache_put(rv->code_cache, rv->PC, code);
            }
        }
        if (code) {
            /* execute machine code */
            code(rv);
            /* block should not be extended if execution mode is jit */
            prev = NULL;
            continue;
        }
#endif
        /* execute the block by interpreter */
        const rv_insn_t *ir = block->ir;
        if (unlikely(!ir->impl(rv, ir))) {
            /* block should not be extended if execption handler invoked */
            prev = NULL;
            break;
        }
        prev = block;
    }
}

void ebreak_handler(riscv_t *rv)
{
    assert(rv);
    rv_except_breakpoint(rv, rv->PC);
}

void ecall_handler(riscv_t *rv)
{
    assert(rv);
    rv_except_ecall_M(rv, 0);
    syscall_handler(rv);
}

void dump_registers(riscv_t *rv, char *out_file_path)
{
    FILE *f;
    if (strncmp(out_file_path, "-", 1) == 0) {
        f = stdout;
    } else {
        f = fopen(out_file_path, "w");
    }

    if (!f) {
        fprintf(stderr, "Cannot open registers output file.\n");
        return;
    }

    fprintf(f, "{\n");
    for (unsigned i = 0; i < RV_N_REGS; i++) {
        char *comma = i < RV_N_REGS - 1 ? "," : "";
        fprintf(f, "  \"x%d\": %u%s\n", i, rv->X[i], comma);
    }
    fprintf(f, "}\n");
}
