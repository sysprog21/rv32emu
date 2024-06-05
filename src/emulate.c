/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#if RV32_HAS(EXT_F)
#include <math.h>
#include "softfloat.h"
#endif /* RV32_HAS(EXT_F) */

#if RV32_HAS(GDBSTUB)
extern struct target_ops gdbstub_ops;
#endif

#include "decode.h"
#include "mpool.h"
#include "riscv.h"
#include "riscv_private.h"
#include "utils.h"

#if RV32_HAS(JIT)
#include "cache.h"
#include "jit.h"
#endif

/* Shortcuts for comparing each field of specified RISC-V instruction */
#define IF_insn(i, o) (i->opcode == rv_insn_##o)
#define IF_rd(i, r) (i->rd == rv_reg_##r)
#define IF_rs1(i, r) (i->rs1 == rv_reg_##r)
#define IF_rs2(i, r) (i->rs2 == rv_reg_##r)
#define IF_imm(i, v) (i->imm == v)

/* RISC-V exception code list */
/* clang-format off */
#define RV_EXCEPTION_LIST                                          \
    IIF(RV32_HAS(EXT_C))(,                                         \
        _(insn_misaligned, 0) /* Instruction address misaligned */ \
    )                                                              \
    _(illegal_insn, 2)     /* Illegal instruction */               \
    _(breakpoint, 3)       /* Breakpoint */                        \
    _(load_misaligned, 4)  /* Load address misaligned */           \
    _(store_misaligned, 6) /* Store/AMO address misaligned */      \
    _(ecall_M, 11)         /* Environment call from M-mode */
/* clang-format on */

enum {
#define _(type, code) rv_exception_code##type = code,
    RV_EXCEPTION_LIST
#undef _
};

static void rv_exception_default_handler(riscv_t *rv)
{
    rv->csr_mepc += rv->compressed ? 2 : 4;
    rv->PC = rv->csr_mepc; /* mret */
}

/* When a trap occurs in M-mode, mtval is either initialized to zero or
 * populated with exception-specific details to assist software in managing
 * the trap. Otherwise, the implementation never modifies mtval, although
 * software can explicitly write to it. The hardware platform will define
 * which exceptions are required to informatively set mtval and which may
 * consistently set it to zero.
 *
 * When a hardware breakpoint is triggered or an exception like address
 * misalignment, access fault, or page fault occurs during an instruction
 * fetch, load, or store operation, mtval is updated with the virtual address
 * that caused the fault. In the case of an illegal instruction trap, mtval
 * might be updated with the first XLEN or ILEN bits of the offending
 * instruction. For all other traps, mtval is simply set to zero. However,
 * it is worth noting that a future standard could redefine how mtval is
 * handled for different types of traps.
 */
#define EXCEPTION_HANDLER_IMPL(type, code)                                   \
    static void rv_except_##type(riscv_t *rv, uint32_t mtval)                \
    {                                                                        \
        /* mtvec (Machine Trap-Vector Base Address Register)                 \
         * mtvec[MXLEN-1:2]: vector base address                             \
         * mtvec[1:0] : vector mode                                          \
         */                                                                  \
        const uint32_t base = rv->csr_mtvec & ~0x3;                          \
        const uint32_t mode = rv->csr_mtvec & 0x3;                           \
        /* mepc  (Machine Exception Program Counter)                         \
         * mtval (Machine Trap Value Register)                               \
         * mcause (Machine Cause Register): store exception code             \
         * mstatus (Machine Status Register): keep track of and controls the \
         * hart’s current operating state                                  \
         */                                                                  \
        rv->csr_mepc = rv->PC;                                               \
        rv->csr_mtval = mtval;                                               \
        rv->csr_mcause = code;                                               \
        rv->csr_mstatus = MSTATUS_MPP; /* set privilege mode */              \
        if (!rv->csr_mtvec) {          /* in case CSR is not configured */   \
            rv_exception_default_handler(rv);                                \
            return;                                                          \
        }                                                                    \
        switch (mode) {                                                      \
        case 0: /* DIRECT: All exceptions set PC to base */                  \
            rv->PC = base;                                                   \
            break;                                                           \
        /* VECTORED: Asynchronous interrupts set PC to base + 4 * code */    \
        case 1:                                                              \
            rv->PC = base + 4 * code;                                        \
            break;                                                           \
        }                                                                    \
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
    (if (!PRIV(rv)->allow_misalign && unlikely(addr & (mask_or_pc))), \
     if (unlikely(insn_is_misaligned(PC))))                           \
    {                                                                 \
        rv->compressed = compress;                                    \
        rv->csr_cycle = cycle;                                        \
        rv->PC = PC;                                                  \
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
    case CSR_TIME: /* Timer for RDTIME instruction */
        update_time(rv);
        return &rv->csr_time[0];
    case CSR_TIMEH: /* Upper 32 bits of time */
        update_time(rv);
        return &rv->csr_time[1];
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

#if !RV32_HAS(JIT)
/* hash function for the block map */
HASH_FUNC_IMPL(map_hash, BLOCK_MAP_CAPACITY_BITS, 1 << BLOCK_MAP_CAPACITY_BITS)
#endif

/* allocate a basic block */
static block_t *block_alloc(riscv_t *rv)
{
    block_t *block = mpool_alloc(rv->block_mp);
    assert(block);
    block->n_insn = 0;
#if RV32_HAS(JIT)
    block->translatable = true;
    block->hot = false;
    block->has_loops = false;
    INIT_LIST_HEAD(&block->list);
#endif
    return block;
}

#if !RV32_HAS(JIT)
/* insert a block into block map */
static void block_insert(block_map_t *map, const block_t *block)
{
    assert(map && block);
    const uint32_t mask = map->block_capacity - 1;
    uint32_t index = map_hash(block->pc_start);

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
    uint32_t index = map_hash(addr);
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

#if !RV32_HAS(EXT_C)
FORCE_INLINE bool insn_is_misaligned(uint32_t pc)
{
    return pc & 0x3;
}
#endif

/* instruction length information for each RISC-V instruction */
enum {
#define _(inst, can_branch, insn_len, translatable, reg_mask) \
    __rv_insn_##inst##_len = insn_len,
    RV_INSN_LIST
#undef _
};

/* can-branch information for each RISC-V instruction */
enum {
#define _(inst, can_branch, insn_len, translatable, reg_mask) \
    __rv_insn_##inst##_canbranch = can_branch,
    RV_INSN_LIST
#undef _
};

#if RV32_HAS(GDBSTUB)
#define RVOP_NO_NEXT(ir) (!ir->next | rv->debug_mode)
#else
#define RVOP_NO_NEXT(ir) (!ir->next)
#endif

/* record whether the branch is taken or not during emulation */
static bool is_branch_taken = false;

/* record the program counter of the previous block */
static uint32_t last_pc = 0;

#if RV32_HAS(JIT)
static set_t pc_set;
static bool has_loops = false;
#endif

/* Interpreter-based execution path */
#define RVOP(inst, code, asm)                                         \
    static bool do_##inst(riscv_t *rv, rv_insn_t *ir, uint64_t cycle, \
                          uint32_t PC)                                \
    {                                                                 \
        cycle++;                                                      \
        code;                                                         \
    nextop:                                                           \
        PC += __rv_insn_##inst##_len;                                 \
        if (unlikely(RVOP_NO_NEXT(ir))) {                             \
            goto end_op;                                              \
        }                                                             \
        const rv_insn_t *next = ir->next;                             \
        MUST_TAIL return next->impl(rv, next, cycle, PC);             \
    end_op:                                                           \
        rv->csr_cycle = cycle;                                        \
        rv->PC = PC;                                                  \
        return true;                                                  \
    }

#include "rv32_template.c"
#undef RVOP

/* multiple LUI */
static bool do_fuse1(riscv_t *rv, rv_insn_t *ir, uint64_t cycle, uint32_t PC)
{
    cycle += ir->imm2;
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++)
        rv->X[fuse[i].rd] = fuse[i].imm;
    PC += ir->imm2 * 4;
    if (unlikely(RVOP_NO_NEXT(ir))) {
        rv->csr_cycle = cycle;
        rv->PC = PC;
        return true;
    }
    const rv_insn_t *next = ir->next;
    MUST_TAIL return next->impl(rv, next, cycle, PC);
}

/* LUI + ADD */
static bool do_fuse2(riscv_t *rv, rv_insn_t *ir, uint64_t cycle, uint32_t PC)
{
    cycle += 2;
    rv->X[ir->rd] = ir->imm;
    rv->X[ir->rs2] = rv->X[ir->rd] + rv->X[ir->rs1];
    PC += 8;
    if (unlikely(RVOP_NO_NEXT(ir))) {
        rv->csr_cycle = cycle;
        rv->PC = PC;
        return true;
    }
    const rv_insn_t *next = ir->next;
    MUST_TAIL return next->impl(rv, next, cycle, PC);
}

/* multiple SW */
static bool do_fuse3(riscv_t *rv, rv_insn_t *ir, uint64_t cycle, uint32_t PC)
{
    cycle += ir->imm2;
    opcode_fuse_t *fuse = ir->fuse;
    /* The memory addresses of the sw instructions are contiguous, thus only
     * the first SW instruction needs to be checked to determine if its memory
     * address is misaligned or if the memory chunk does not exist.
     */
    for (int i = 0; i < ir->imm2; i++) {
        uint32_t addr = rv->X[fuse[i].rs1] + fuse[i].imm;
        RV_EXC_MISALIGN_HANDLER(3, store, false, 1);
        rv->io.mem_write_w(addr, rv->X[fuse[i].rs2]);
    }
    PC += ir->imm2 * 4;
    if (unlikely(RVOP_NO_NEXT(ir))) {
        rv->csr_cycle = cycle;
        rv->PC = PC;
        return true;
    }
    const rv_insn_t *next = ir->next;
    MUST_TAIL return next->impl(rv, next, cycle, PC);
}

/* multiple LW */
static bool do_fuse4(riscv_t *rv, rv_insn_t *ir, uint64_t cycle, uint32_t PC)
{
    cycle += ir->imm2;
    opcode_fuse_t *fuse = ir->fuse;
    /* The memory addresses of the lw instructions are contiguous, therefore
     * only the first LW instruction needs to be checked to determine if its
     * memory address is misaligned or if the memory chunk does not exist.
     */
    for (int i = 0; i < ir->imm2; i++) {
        uint32_t addr = rv->X[fuse[i].rs1] + fuse[i].imm;
        RV_EXC_MISALIGN_HANDLER(3, load, false, 1);
        rv->X[fuse[i].rd] = rv->io.mem_read_w(addr);
    }
    PC += ir->imm2 * 4;
    if (unlikely(RVOP_NO_NEXT(ir))) {
        rv->csr_cycle = cycle;
        rv->PC = PC;
        return true;
    }
    const rv_insn_t *next = ir->next;
    MUST_TAIL return next->impl(rv, next, cycle, PC);
}

/* multiple shift immediate */
static bool do_fuse5(riscv_t *rv,
                     const rv_insn_t *ir,
                     uint64_t cycle,
                     uint32_t PC)
{
    cycle += ir->imm2;
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++)
        shift_func(rv, (const rv_insn_t *) (&fuse[i]));
    PC += ir->imm2 * 4;
    if (unlikely(RVOP_NO_NEXT(ir))) {
        rv->csr_cycle = cycle;
        rv->PC = PC;
        return true;
    }
    const rv_insn_t *next = ir->next;
    MUST_TAIL return next->impl(rv, next, cycle, PC);
}

/* clang-format off */
static const void *dispatch_table[] = {
    /* RV32 instructions */
#define _(inst, can_branch, insn_len, translatable, reg_mask) [rv_insn_##inst] = do_##inst,
    RV_INSN_LIST
#undef _
    /* Macro operation fusion instructions */
#define _(inst) [rv_insn_##inst] = do_##inst,
    FUSE_INSN_LIST
#undef _
};
/* clang-format on */

FORCE_INLINE bool insn_is_branch(uint8_t opcode)
{
    switch (opcode) {
#define _(inst, can_branch, insn_len, translatable, reg_mask) \
    IIF(can_branch)                                           \
    (case rv_insn_##inst:, )
        RV_INSN_LIST
#undef _
        return true;
    }
    return false;
}

#if RV32_HAS(JIT)
FORCE_INLINE bool insn_is_translatable(uint8_t opcode)
{
    switch (opcode) {
#define _(inst, can_branch, insn_len, translatable, reg_mask) \
    IIF(translatable)                                         \
    (case rv_insn_##inst:, )
        RV_INSN_LIST
#undef _
        return true;
    }
    return false;
}
#endif

FORCE_INLINE bool insn_is_unconditional_branch(uint8_t opcode)
{
    switch (opcode) {
    case rv_insn_ecall:
    case rv_insn_ebreak:
    case rv_insn_jal:
    case rv_insn_jalr:
    case rv_insn_sret:
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

static void block_translate(riscv_t *rv, block_t *block)
{
    block->pc_start = block->pc_end = rv->PC;

    rv_insn_t *prev_ir = NULL;
    rv_insn_t *ir = mpool_calloc(rv->block_ir_mp);
    block->ir_head = ir;

    /* translate the basic block */
    while (true) {
        if (prev_ir)
            prev_ir->next = ir;

        /* fetch the next instruction */
        const uint32_t insn = rv->io.mem_ifetch(block->pc_end);

        /* decode the instruction */
        if (!rv_decode(ir, insn)) {
            rv->compressed = is_compressed(insn);
            rv_except_illegal_insn(rv, insn);
            break;
        }
        ir->impl = dispatch_table[ir->opcode];
        ir->pc = block->pc_end;
        /* compute the end of pc */
        block->pc_end += is_compressed(insn) ? 2 : 4;
        block->n_insn++;
        prev_ir = ir;
#if RV32_HAS(JIT)
        if (!insn_is_translatable(ir->opcode))
            block->translatable = false;
#endif
        /* stop on branch */
        if (insn_is_branch(ir->opcode)) {
            if (ir->opcode == rv_insn_jalr
#if RV32_HAS(EXT_C)
                || ir->opcode == rv_insn_cjalr || ir->opcode == rv_insn_cjr
#endif
            ) {
                ir->branch_table = calloc(1, sizeof(branch_history_table_t));
                assert(ir->branch_table);
            }
            break;
        }

        ir = mpool_calloc(rv->block_ir_mp);
    }

    assert(prev_ir);
    block->ir_tail = prev_ir;
    block->ir_tail->next = NULL;
}

#define COMBINE_MEM_OPS(RW)                                       \
    next_ir = ir->next;                                           \
    count = 1;                                                    \
    while (1) {                                                   \
        if (next_ir->opcode != IIF(RW)(rv_insn_lw, rv_insn_sw))   \
            break;                                                \
        count++;                                                  \
        if (!next_ir->next)                                       \
            break;                                                \
        next_ir = next_ir->next;                                  \
    }                                                             \
    if (count > 1) {                                              \
        ir->opcode = IIF(RW)(rv_insn_fuse4, rv_insn_fuse3);       \
        ir->fuse = malloc(count * sizeof(opcode_fuse_t));         \
        assert(ir->fuse);                                         \
        ir->imm2 = count;                                         \
        memcpy(ir->fuse, ir, sizeof(opcode_fuse_t));              \
        ir->impl = dispatch_table[ir->opcode];                    \
        next_ir = ir->next;                                       \
        for (int j = 1; j < count; j++, next_ir = next_ir->next)  \
            memcpy(ir->fuse + j, next_ir, sizeof(opcode_fuse_t)); \
        remove_next_nth_ir(rv, ir, block, count - 1);             \
    }

static inline void remove_next_nth_ir(const riscv_t *rv,
                                      rv_insn_t *ir,
                                      block_t *block,
                                      uint8_t n)
{
    for (uint8_t i = 0; i < n; i++) {
        rv_insn_t *next = ir->next;
        ir->next = ir->next->next;
        mpool_free(rv->block_ir_mp, next);
    }
    if (!ir->next)
        block->ir_tail = ir;
    block->n_insn -= n;
}

/* Check if instructions in a block match a specific pattern. If they do,
 * rewrite them as fused instructions.
 *
 * Strategies are being devised to increase the number of instructions that
 * match the pattern, including possible instruction reordering.
 */
static void match_pattern(riscv_t *rv, block_t *block)
{
    uint32_t i;
    rv_insn_t *ir;
    for (i = 0, ir = block->ir_head; i < block->n_insn - 1;
         i++, ir = ir->next) {
        assert(ir);
        rv_insn_t *next_ir = NULL;
        int32_t count = 0;
        switch (ir->opcode) {
        case rv_insn_lui:
            next_ir = ir->next;
            switch (next_ir->opcode) {
            case rv_insn_add:
                if (ir->rd == next_ir->rs2 || ir->rd == next_ir->rs1) {
                    ir->opcode = rv_insn_fuse2;
                    ir->rs2 = next_ir->rd;
                    if (ir->rd == next_ir->rs2)
                        ir->rs1 = next_ir->rs1;
                    else
                        ir->rs1 = next_ir->rs2;
                    ir->impl = dispatch_table[ir->opcode];
                    remove_next_nth_ir(rv, ir, block, 1);
                }
                break;
            case rv_insn_lui:
                count = 1;
                while (1) {
                    if (!IF_insn(next_ir, lui))
                        break;
                    count++;
                    if (!next_ir->next)
                        break;
                    next_ir = next_ir->next;
                }
                if (count > 1) {
                    ir->opcode = rv_insn_fuse1;
                    ir->fuse = malloc(count * sizeof(opcode_fuse_t));
                    assert(ir->fuse);
                    ir->imm2 = count;
                    memcpy(ir->fuse, ir, sizeof(opcode_fuse_t));
                    ir->impl = dispatch_table[ir->opcode];
                    next_ir = ir->next;
                    for (int j = 1; j < count; j++, next_ir = next_ir->next)
                        memcpy(ir->fuse + j, next_ir, sizeof(opcode_fuse_t));
                    remove_next_nth_ir(rv, ir, block, count - 1);
                }
                break;
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
            /* TODO: mixture of SW and LW */
            /* TODO: reorder insturction to match pattern */
        case rv_insn_slli:
        case rv_insn_srli:
        case rv_insn_srai:
            count = 1;
            next_ir = ir->next;
            while (1) {
                if (!IF_insn(next_ir, slli) && !IF_insn(next_ir, srli) &&
                    !IF_insn(next_ir, srai))
                    break;
                count++;
                if (!next_ir->next)
                    break;
                next_ir = next_ir->next;
            }
            if (count > 1) {
                ir->fuse = malloc(count * sizeof(opcode_fuse_t));
                assert(ir->fuse);
                memcpy(ir->fuse, ir, sizeof(opcode_fuse_t));
                ir->opcode = rv_insn_fuse5;
                ir->imm2 = count;
                ir->impl = dispatch_table[ir->opcode];
                next_ir = ir->next;
                for (int j = 1; j < count; j++, next_ir = next_ir->next)
                    memcpy(ir->fuse + j, next_ir, sizeof(opcode_fuse_t));
                remove_next_nth_ir(rv, ir, block, count - 1);
            }
            break;
        }
    }
}

typedef struct {
    bool is_constant[N_RV_REGS];
    uint32_t const_val[N_RV_REGS];
} constopt_info_t;

#define CONSTOPT(inst, code)                                  \
    static void constopt_##inst(rv_insn_t *ir UNUSED,         \
                                constopt_info_t *info UNUSED) \
    {                                                         \
        code;                                                 \
    }

#include "rv32_constopt.c"
static const void *constopt_table[] = {
#define _(inst, can_branch, insn_len, translatable, reg_mask) \
    [rv_insn_##inst] = constopt_##inst,
    RV_INSN_LIST
#undef _
};
#undef CONSTOPT

typedef void (*constopt_func_t)(rv_insn_t *, constopt_info_t *);
static void optimize_constant(riscv_t *rv UNUSED, block_t *block)
{
    constopt_info_t info = {.is_constant[0] = true};
    assert(rv->X[0] == 0);

    uint32_t i;
    rv_insn_t *ir;
    for (i = 0, ir = block->ir_head; i < block->n_insn; i++, ir = ir->next)
        ((constopt_func_t) constopt_table[ir->opcode])(ir, &info);
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
    block_t *next = (block_t *) cache_get(rv->block_cache, rv->PC, true);
#endif

    if (!next) {
#if !RV32_HAS(JIT)
        if (map->size * 1.25 > map->block_capacity) {
            block_map_clear(rv);
            prev = NULL;
        }
#endif
        /* allocate a new block */
        next = block_alloc(rv);
        block_translate(rv, next);

        optimize_constant(rv, next);
#if RV32_HAS(GDBSTUB)
        if (likely(!rv->debug_mode))
#endif
            /* macro operation fusion */
            match_pattern(rv, next);

#if !RV32_HAS(JIT)
        /* insert the block into block map */
        block_insert(&rv->block_map, next);
#else
        /* insert the block into block cache */
        block_t *delete_target = cache_put(rv->block_cache, rv->PC, &(*next));
        if (delete_target) {
            if (prev == delete_target)
                prev = NULL;
            chain_entry_t *entry, *safe;
            /* correctly remove deleted block from its chained block */
            rv_insn_t *taken = delete_target->ir_tail->branch_taken,
                      *untaken = delete_target->ir_tail->branch_untaken;
            if (taken && taken->pc != delete_target->pc_start) {
                block_t *target = cache_get(rv->block_cache, taken->pc, false);
                bool flag = false;
                list_for_each_entry_safe (entry, safe, &target->list, list) {
                    if (entry->block == delete_target) {
                        list_del_init(&entry->list);
                        mpool_free(rv->chain_entry_mp, entry);
                        flag = true;
                    }
                }
                assert(flag);
            }
            if (untaken && untaken->pc != delete_target->pc_start) {
                block_t *target =
                    cache_get(rv->block_cache, untaken->pc, false);
                assert(target);
                bool flag = false;
                list_for_each_entry_safe (entry, safe, &target->list, list) {
                    if (entry->block == delete_target) {
                        list_del_init(&entry->list);
                        mpool_free(rv->chain_entry_mp, entry);
                        flag = true;
                    }
                }
                assert(flag);
            }
            /* correctly remove deleted block from the block chained to it */
            list_for_each_entry_safe (entry, safe, &delete_target->list, list) {
                if (entry->block == delete_target)
                    continue;
                rv_insn_t *target = entry->block->ir_tail;
                if (target->branch_taken == delete_target->ir_head)
                    target->branch_taken = NULL;
                else if (target->branch_untaken == delete_target->ir_head)
                    target->branch_untaken = NULL;
                mpool_free(rv->chain_entry_mp, entry);
            }
            /* free deleted block */
            uint32_t idx;
            rv_insn_t *ir, *next;
            for (idx = 0, ir = delete_target->ir_head;
                 idx < delete_target->n_insn; idx++, ir = next) {
                free(ir->fuse);
                next = ir->next;
                mpool_free(rv->block_ir_mp, ir);
            }
            mpool_free(rv->block_mp, delete_target);
        }
#endif
    }

    return next;
}

#if RV32_HAS(JIT)
static bool runtime_profiler(riscv_t *rv, block_t *block)
{
    /* Based on our observations, a significant number of true hotspots are
     * characterized by high usage frequency and including loop. Consequently,
     * we posit that our profiler could effectively identify hotspots using
     * three key indicators.
     */
    uint32_t freq = cache_freq(rv->block_cache, block->pc_start);
    /* To profile a block after chaining, it must first be executed. */
    if (unlikely(freq >= 2 && block->has_loops))
        return true;
    /* using frequency exceeds predetermined threshold */
    if (unlikely(freq == THRESHOLD))
        return true;
    return false;
}

typedef void (*exec_block_func_t)(riscv_t *rv, uintptr_t);
#endif

void rv_step(void *arg)
{
    assert(arg);
    riscv_t *rv = arg;

    vm_attr_t *attr = PRIV(rv);
    uint32_t cycles = attr->cycle_per_step;

    /* find or translate a block for starting PC */
    const uint64_t cycles_target = rv->csr_cycle + cycles;

    /* loop until hitting the cycle target */
    while (rv->csr_cycle < cycles_target && !rv->halt) {
        if (prev && prev->pc_start != last_pc) {
            /* update previous block */
#if !RV32_HAS(JIT)
            prev = block_find(&rv->block_map, last_pc);
#else
            prev = cache_get(rv->block_cache, last_pc, false);
#endif
        }
        /* lookup the next block in block map or translate a new block,
         * and move onto the next block.
         */
        block_t *block = block_find_or_translate(rv);
        /* by now, a block should be available */
        assert(block);

        /* After emulating the previous block, it is determined whether the
         * branch is taken or not. The IR array of the current block is then
         * assigned to either the branch_taken or branch_untaken pointer of
         * the previous block.
         */

        if (prev) {
            rv_insn_t *last_ir = prev->ir_tail;
            /* chain block */
            if (!insn_is_unconditional_branch(last_ir->opcode)) {
                if (is_branch_taken && !last_ir->branch_taken) {
                    last_ir->branch_taken = block->ir_head;
#if RV32_HAS(JIT)
                    chain_entry_t *new_entry = mpool_alloc(rv->chain_entry_mp);
                    new_entry->block = prev;
                    list_add(&new_entry->list, &block->list);
#endif
                } else if (!is_branch_taken && !last_ir->branch_untaken) {
                    last_ir->branch_untaken = block->ir_head;
#if RV32_HAS(JIT)
                    chain_entry_t *new_entry = mpool_alloc(rv->chain_entry_mp);
                    new_entry->block = prev;
                    list_add(&new_entry->list, &block->list);
#endif
                }
            } else if (IF_insn(last_ir, jal)
#if RV32_HAS(EXT_C)
                       || IF_insn(last_ir, cj) || IF_insn(last_ir, cjal)
#endif
            ) {
                if (!last_ir->branch_taken) {
                    last_ir->branch_taken = block->ir_head;
#if RV32_HAS(JIT)
                    chain_entry_t *new_entry = mpool_alloc(rv->chain_entry_mp);
                    new_entry->block = prev;
                    list_add(&new_entry->list, &block->list);
#endif
                }
            }
        }
        last_pc = rv->PC;
#if RV32_HAS(JIT)
        /* execute by tier-1 JIT compiler */
        struct jit_state *state = rv->jit_state;
        if (block->hot) {
            ((exec_block_func_t) state->buf)(
                rv, (uintptr_t) (state->buf + block->offset));
            prev = NULL;
            continue;
        } /* check if using frequency of block exceed threshold */
        else if (block->translatable && runtime_profiler(rv, block)) {
            jit_translate(rv, block);
            ((exec_block_func_t) state->buf)(
                rv, (uintptr_t) (state->buf + block->offset));
            prev = NULL;
            continue;
        }
        set_reset(&pc_set);
        has_loops = false;
#endif
        /* execute the block by interpreter */
        const rv_insn_t *ir = block->ir_head;
        if (unlikely(!ir->impl(rv, ir, rv->csr_cycle, rv->PC))) {
            /* block should not be extended if execption handler invoked */
            prev = NULL;
            break;
        }
#if RV32_HAS(JIT)
        if (has_loops && !block->has_loops)
            block->has_loops = true;
#endif
        prev = block;
    }

#ifdef __EMSCRIPTEN__
    if (rv_has_halted(rv)) {
        printf("inferior exit code %d\n", attr->exit_code);
        emscripten_cancel_main_loop();
        rv_delete(rv); /* clean up and reuse memory */
    }
#endif
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

void memset_handler(riscv_t *rv)
{
    memory_t *m = PRIV(rv)->mem;
    memset((char *) m->mem_base + rv->X[rv_reg_a0], rv->X[rv_reg_a1],
           rv->X[rv_reg_a2]);
    rv->PC = rv->X[rv_reg_ra] & ~1U;
}

void memcpy_handler(riscv_t *rv)
{
    memory_t *m = PRIV(rv)->mem;
    memcpy((char *) m->mem_base + rv->X[rv_reg_a0],
           (char *) m->mem_base + rv->X[rv_reg_a1], rv->X[rv_reg_a2]);
    rv->PC = rv->X[rv_reg_ra] & ~1U;
}

void dump_registers(riscv_t *rv, char *out_file_path)
{
    FILE *f = out_file_path[0] == '-' ? stdout : fopen(out_file_path, "w");
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

    if (out_file_path[0] != '-')
        fclose(f);
}
