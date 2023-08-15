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

#if defined(__APPLE__)
static inline int isinff(float x)
{
    return __builtin_fabsf(x) == __builtin_inff();
}
static inline int isnanf(float x)
{
    return x != x;
}
#endif
#endif /* RV32_HAS(EXT_F) */

#if RV32_HAS(GDBSTUB)
extern struct target_ops gdbstub_ops;
#endif

#include "decode.h"
#include "riscv.h"
#include "riscv_private.h"
#include "state.h"
#include "utils.h"

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

/* get current time in microsecnds and update csr_time register */
static inline void update_time(riscv_t *rv)
{
    struct timeval tv;
    rv_gettimeofday(&tv);

    uint64_t t = (uint64_t) tv.tv_sec * 1e6 + (uint32_t) tv.tv_usec;
    rv->csr_time[0] = t & 0xFFFFFFFF;
    rv->csr_time[1] = t >> 32;
}

#if RV32_HAS(Zicsr)
/* get a pointer to a CSR */
static uint32_t *csr_get_ptr(riscv_t *rv, uint32_t csr)
{
    /* csr & 0xFFF prevent sign-extension in decode stage */
    switch (csr & 0xFFF) {
    case CSR_MSTATUS: /* Machine Status */
        return (uint32_t *) (&rv->csr_mstatus);
    case CSR_MTVEC: /* Machine Trap Handler */
        return (uint32_t *) (&rv->csr_mtvec);
    case CSR_MISA: /* Machine ISA and Extensions */
        return (uint32_t *) (&rv->csr_misa);

    /* Machine Trap Handling */
    case CSR_MSCRATCH: /* Machine Scratch Register */
        return (uint32_t *) (&rv->csr_mscratch);
    case CSR_MEPC: /* Machine Exception Program Counter */
        return (uint32_t *) (&rv->csr_mepc);
    case CSR_MCAUSE: /* Machine Exception Cause */
        return (uint32_t *) (&rv->csr_mcause);
    case CSR_MTVAL: /* Machine Trap Value */
        return (uint32_t *) (&rv->csr_mtval);
    case CSR_MIP: /* Machine Interrupt Pending */
        return (uint32_t *) (&rv->csr_mip);

    /* Machine Counter/Timers */
    case CSR_CYCLE: /* Cycle counter for RDCYCLE instruction */
        return (uint32_t *) &rv->csr_cycle;
    case CSR_CYCLEH: /* Upper 32 bits of cycle */
        return &((uint32_t *) &rv->csr_cycle)[1];

    /* TIME/TIMEH - very roughly about 1 ms per tick */
    case CSR_TIME: { /* Timer for RDTIME instruction */
        update_time(rv);
        return &rv->csr_time[0];
    }
    case CSR_TIMEH: { /* Upper 32 bits of time */
        update_time(rv);
        return &rv->csr_time[1];
    }
    case CSR_INSTRET: /* Number of Instructions Retired Counter */
        return (uint32_t *) (&rv->csr_cycle);
#if RV32_HAS(EXT_F)
    case CSR_FFLAGS:
        return (uint32_t *) (&rv->csr_fcsr);
    case CSR_FCSR:
        return (uint32_t *) (&rv->csr_fcsr);
#endif
    default:
        return NULL;
    }
}

static inline bool csr_is_writable(uint32_t csr)
{
    return csr < 0xc00;
}

/* CSRRW (Atomic Read/Write CSR) instruction atomically swaps values in the
 * CSRs and integer registers. CSRRW reads the old value of the CSR,
 * zero-extends the value to XLEN bits, and then writes it to register rd.
 * The initial value in rs1 is written to the CSR.
 * If rd == x0, then the instruction shall not read the CSR and shall not cause
 * any of the side effects that might occur on a CSR read.
 */
static uint32_t csr_csrrw(riscv_t *rv, uint32_t csr, uint32_t val)
{
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    uint32_t out = *c;
#if RV32_HAS(EXT_F)
    if (csr == CSR_FFLAGS)
        out &= FFLAG_MASK;
#endif
    if (csr_is_writable(csr))
        *c = val;

    return out;
}

/* perform csrrs (atomic read and set) */
static uint32_t csr_csrrs(riscv_t *rv, uint32_t csr, uint32_t val)
{
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    uint32_t out = *c;
#if RV32_HAS(EXT_F)
    if (csr == CSR_FFLAGS)
        out &= FFLAG_MASK;
#endif
    if (csr_is_writable(csr))
        *c |= val;

    return out;
}

/* perform csrrc (atomic read and clear)
 * Read old value of CSR, zero-extend to XLEN bits, write to rd.
 * Read value from rs1, use as bit mask to clear bits in CSR.
 */
static uint32_t csr_csrrc(riscv_t *rv, uint32_t csr, uint32_t val)
{
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    uint32_t out = *c;
#if RV32_HAS(EXT_F)
    if (csr == CSR_FFLAGS)
        out &= FFLAG_MASK;
#endif
    if (csr_is_writable(csr))
        *c &= ~val;
    return out;
}
#endif

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
#define _(inst, can_branch, reg_mask) __rv_insn_##inst##_canbranch = can_branch,
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

/* Macro operation fusion */

/* macro operation fusion: convert specific RISC-V instruction patterns
 * into faster and equivalent code
 */
#define FUSE_INSN_LIST \
    _(fuse1)           \
    _(fuse2)           \
    _(fuse3)           \
    _(fuse4)           \
    _(fuse5)

enum {
    rv_insn_fuse0 = N_RISCV_INSN_LIST,
#define _(inst) rv_insn_##inst,
    FUSE_INSN_LIST
#undef _
};

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
#define _(inst, can_branch, reg_mask) [rv_insn_##inst] = do_##inst,
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
#define _(inst, can_branch, reg_mask) IIF(can_branch)(case rv_insn_##inst:, )
        RISCV_INSN_LIST
#undef _
        return true;
    }
    return false;
}

static inline bool insn_is_unconditional_branch(uint8_t opcode)
{
    switch (opcode) {
    case rv_insn_ecall:
    case rv_insn_ebreak:
    case rv_insn_jal:
    case rv_insn_jalr:
    case rv_insn_mret:
#if RV32_HAS(EXT_C)
    case rv_insn_cj:
    case rv_insn_cjalr:
    case rv_insn_cjal:
    case rv_insn_cjr:
    case rv_insn_cebreak:
#endif
        return true;
    }
    return false;
}

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

/* allocate a basic block */
static block_t *block_alloc(const uint8_t bits)
{
    block_t *block = malloc(sizeof(struct block));
    block->insn_capacity = 1 << bits;
    block->n_insn = 0;
    block->predict = NULL;
    block->ir = malloc(block->insn_capacity * sizeof(rv_insn_t));
    return block;
}

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
        /* compute the end of pc */
        block->pc_end += ir->insn_len;
        block->n_insn++;
        /* stop on branch */
        if (insn_is_branch(ir->opcode)) {
            /* recursive jump translation */
            if (ir->opcode == rv_insn_jal
#if RV32_HAS(EXT_C)
                || ir->opcode == rv_insn_cj || ir->opcode == rv_insn_cjal
#endif
            ) {
                block->pc_end = block->pc_end - ir->insn_len + ir->imm;
                ir->branch_taken = ir + 1;
                continue;
            }
            break;
        }
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
    block_map_t *map = &rv->block_map;
    /* lookup the next block in the block map */
    block_t *next = block_find(map, rv->PC);

    if (!next) {
        if (map->size * 1.25 > map->block_capacity) {
            block_map_clear(map);
            prev = NULL;
        }

        /* allocate a new block */
        next = block_alloc(10);

        /* translate the basic block */
        block_translate(rv, next);
#if RV32_HAS(GDBSTUB)
        if (likely(!rv->debug_mode))
#endif
            /* macro operation fusion */
            match_pattern(next);

        /* insert the block into block map */
        block_insert(&rv->block_map, next);

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
                prev = block_find(&rv->block_map, last_pc);

            rv_insn_t *last_ir = prev->ir + prev->n_insn - 1;
            /* chain block */
            if (!insn_is_unconditional_branch(last_ir->opcode)) {
                if (branch_taken && !last_ir->branch_taken)
                    last_ir->branch_taken = block->ir;
                else if (!last_ir->branch_untaken)
                    last_ir->branch_untaken = block->ir;
            }
        }
        last_pc = rv->PC;

        /* execute the block */
        const rv_insn_t *ir = block->ir;
        if (unlikely(!ir->impl(rv, ir)))
            break;

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
    for (unsigned i = 0; i < N_RV_REGS; i++) {
        char *comma = i < N_RV_REGS - 1 ? "," : "";
        fprintf(f, "  \"x%d\": %u%s\n", i, rv->X[i], comma);
    }
    fprintf(f, "}\n");
}
