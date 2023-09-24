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
    assert(block);
    block->insn_capacity = 1 << bits;
    block->n_insn = 0;
    block->predict = NULL;
    block->ir = malloc(block->insn_capacity * sizeof(rv_insn_t));
    assert(block->ir);
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
    RV_INSN_LIST
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
    _(fuse5)           \
    _(fuse6)           \
    _(fuse7)

enum {
    rv_insn_fuse0 = N_RV_INSNS,
#define _(inst) rv_insn_##inst,
    FUSE_INSN_LIST
#undef _
};

/* AUIPC + ADDI */
static bool do_fuse1(riscv_t *rv, const rv_insn_t *ir)
{
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
    rv->csr_cycle += 2;
    rv->X[ir->rd] = ir->imm;
    rv->X[ir->rs1] = ir->imm + ir->imm2;
    rv->PC += 2 * ir->insn_len;
    if (unlikely(RVOP_NO_NEXT(ir)))
        return true;
    const rv_insn_t *next = ir + 2;
    MUST_TAIL return next->impl(rv, next);
}

/* memset */
static bool do_fuse6(riscv_t *rv, const rv_insn_t *ir)
{
    rv->csr_cycle += 2;
    memory_t *m = ((state_t *) rv->userdata)->mem;
    memset((char *) m->mem_base + rv->X[rv_reg_a0], rv->X[rv_reg_a1],
           rv->X[rv_reg_a2]);
    rv->PC = rv->X[rv_reg_ra] & ~1U;
    if (unlikely(RVOP_NO_NEXT(ir)))
        return true;
    const rv_insn_t *next = ir + 1;
    MUST_TAIL return next->impl(rv, next);
}

/* memcpy */
static bool do_fuse7(riscv_t *rv, const rv_insn_t *ir)
{
    rv->csr_cycle += 2;
    memory_t *m = ((state_t *) rv->userdata)->mem;
    memcpy((char *) m->mem_base + rv->X[rv_reg_a0],
           (char *) m->mem_base + rv->X[rv_reg_a1], rv->X[rv_reg_a2]);
    rv->PC = rv->X[rv_reg_ra] & ~1U;
    if (unlikely(RVOP_NO_NEXT(ir)))
        return true;
    const rv_insn_t *next = ir + 1;
    MUST_TAIL return next->impl(rv, next);
}

/* clang-format off */
static const void *dispatch_table[] = {
    /* RV32 instructions */
#define _(inst, can_branch, reg_mask) [rv_insn_##inst] = do_##inst,
    RV_INSN_LIST
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
        RV_INSN_LIST
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

static bool detect_memset(riscv_t *rv, int lib)
{
    static const uint32_t memset_insn[] = {
        0x00f00313, /* li	t1,15 */
        0x00050713, /* mv	a4,a0 */
        0x02c37e63, /* bgeu	t1,a2,0x11828 */
        0x00f77793, /* and	a5,a4,15 */
        0x0a079063, /* bnez	a5,0x11894 */
        0x08059263, /* bnez	a1,0x1187c */
        0xff067693, /* and	a3,a2,-16 */
        0x00f67613, /* and	a2,a2,15 */
        0x00e686b3, /* add	a3,a3,a4 */
        0x00b72023, /* sw	a1,0(a4) */
        0x00b72223, /* sw	a1,4(a4) */
        0x00b72423, /* sw	a1,8(a4) */
        0x00b72623, /* sw	a1,12(a4) */
        0x01070713, /* add	a4,a4,16 */
        0xfed766e3, /* bltu	a4,a3,0x11808 */
        0x00061463, /* bnez	a2,0x11828 */
        0x00008067, /* ret */
        0x40c306b3, /* sub	a3,t1,a2 */
        0x00269693, /* sll	a3,a3,0x2 */
        0x00000297, /* auipc	t0,0x0 */
        0x005686b3, /* add	a3,a3,t0 */
        0x00c68067, /* jr	12(a3) */
        0x00b70723, /* sb	a1,14(a4) */
        0x00b706a3, /* sb	a1,13(a4) */
        0x00b70623, /* sb	a1,12(a4) */
        0x00b705a3, /* sb	a1,11(a4) */
        0x00b70523, /* sb	a1,10(a4) */
        0x00b704a3, /* sb	a1,9(a4) */
        0x00b70423, /* sb	a1,8(a4) */
        0x00b703a3, /* sb	a1,7(a4) */
        0x00b70323, /* sb	a1,6(a4) */
        0x00b702a3, /* sb	a1,5(a4) */
        0x00b70223, /* sb	a1,4(a4) */
        0x00b701a3, /* sb	a1,3(a4) */
        0x00b70123, /* sb	a1,2(a4) */
        0x00b700a3, /* sb	a1,1(a4) */
        0x00b70023, /* sb	a1,0(a4) */
        0x00008067, /* ret */
        0x0ff5f593, /* zext.b	a1,a1 */
        0x00859693, /* sll	a3,a1,0x8 */
        0x00d5e5b3, /* or	a1,a1,a3 */
        0x01059693, /* sll	a3,a1,0x10 */
        0x00d5e5b3, /* or	a1,a1,a3 */
        0xf6dff06f, /* j	0x117fc */
        0x00279693, /* sll	a3,a5,0x2 */
        0x00000297, /* auipc	t0,0x0 */
        0x005686b3, /* add	a3,a3,t0 */
        0x00008293, /* mv	t0,ra */
        0xfa0680e7, /* jalr	-96(a3) */
        0x00028093, /* mv	ra,t0 */
        0xff078793, /* add	a5,a5,-16 */
        0x40f70733, /* sub	a4,a4,a5 */
        0x00f60633, /* add	a2,a2,a5 */
        0xf6c378e3, /* bgeu	t1,a2,0x11828 */
        0xf3dff06f, /* j	0x117f8 */
    };
    static const uint32_t memset2_insn[] = {
        0x00050313, /* mv	t1,a0 */
        0x00060a63, /* beqz	a2, 0x18 */
        0x00b30023, /* sb	a1,0(t1) */
        0xfff60613, /* add	a2,a2,-1 */
        0x00130313, /* add	t1,t1,1 */
        0xfe061ae3, /* bnez	a2, 0x8 */
        0x00008067, /* ret */
    };
    uint32_t tmp_pc = rv->PC;
    if (lib == 1) {
        for (uint32_t i = 0; i < ARRAYS_SIZE(memset_insn); i++) {
            const uint32_t insn = rv->io.mem_ifetch(tmp_pc);
            if (insn != memset_insn[i])
                return false;
            tmp_pc += 4;
        }
    } else {
        for (uint32_t i = 0; i < ARRAYS_SIZE(memset2_insn); i++) {
            const uint32_t insn = rv->io.mem_ifetch(tmp_pc);
            if (insn != memset2_insn[i])
                return false;
            tmp_pc += 4;
        }
    }
    return true;
}

static bool detect_memcpy(riscv_t *rv, int lib)
{
    static const uint32_t memcpy_insn[] = {
        0x00b547b3, /* xor	a5,a0,a1 */
        0x0037f793, /* and	a5,a5,3 */
        0x00c508b3, /* add	a7,a0,a2 */
        0x06079463, /* bnez	a5,0x21428 */
        0x00300793, /* li	a5,3 */
        0x06c7f063, /* bgeu	a5,a2,0x21428 */
        0x00357793, /* and	a5,a0,3 */
        0x00050713, /* mv	a4,a0 */
        0x06079a63, /* bnez	a5,0x21448 */
        0xffc8f613, /* and	a2,a7,-4 */
        0x40e606b3, /* sub	a3,a2,a4 */
        0x02000793, /* li	a5,32 */
        0x08d7ce63, /* blt	a5,a3,0x21480 */
        0x00058693, /* mv	a3,a1 */
        0x00070793, /* mv	a5,a4 */
        0x02c77863, /* bgeu	a4,a2,0x21420 */
        0x0006a803, /* lw	a6,0(a3) */
        0x00478793, /* add	a5,a5,4 */
        0x00468693, /* add	a3,a3,4 */
        0xff07ae23, /* sw	a6,-4(a5) */
        0xfec7e8e3, /* bltu	a5,a2,0x213f4 */
        0xfff60793, /* add	a5,a2,-1 */
        0x40e787b3, /* sub	a5,a5,a4 */
        0xffc7f793, /* and	a5,a5,-4 */
        0x00478793, /* add	a5,a5,4 */
        0x00f70733, /* add	a4,a4,a5 */
        0x00f585b3, /* add	a1,a1,a5 */
        0x01176863, /* bltu	a4,a7,0x21430 */
        0x00008067, /* ret */
        0x00050713, /* mv	a4,a0 */
        0x05157863, /* bgeu	a0,a7,0x2147c */
        0x0005c783, /* lbu	a5,0(a1) */
        0x00170713, /* add	a4,a4,1 */
        0x00158593, /* add	a1,a1,1 */
        0xfef70fa3, /* sb	a5,-1(a4) */
        0xfee898e3, /* bne	a7,a4,0x21430 */
        0x00008067, /* ret */
        0x0005c683, /* lbu	a3,0(a1) */
        0x00170713, /* add	a4,a4,1 */
        0x00377793, /* and	a5,a4,3 */
        0xfed70fa3, /* sb	a3,-1(a4) */
        0x00158593, /* add	a1,a1,1 */
        0xf6078ee3, /* beqz	a5,0x213d8 */
        0x0005c683, /* lbu	a3,0(a1) */
        0x00170713, /* add	a4,a4,1 */
        0x00377793, /* and	a5,a4,3 */
        0xfed70fa3, /* sb	a3,-1(a4) */
        0x00158593, /* add	a1,a1,1 */
        0xfc079ae3, /* bnez	a5,0x21448 */
        0xf61ff06f, /* j	0x213d8 */
        0x00008067, /* ret */
        0xff010113, /* add	sp,sp,-16 */
        0x00812623, /* sw	s0,12(sp) */
        0x02000413, /* li	s0,32 */
        0x0005a383, /* lw	t2,0(a1) */
        0x0045a283, /* lw	t0,4(a1) */
        0x0085af83, /* lw	t6,8(a1) */
        0x00c5af03, /* lw	t5,12(a1) */
        0x0105ae83, /* lw	t4,16(a1) */
        0x0145ae03, /* lw	t3,20(a1) */
        0x0185a303, /* lw	t1,24(a1) */
        0x01c5a803, /* lw	a6,28(a1) */
        0x0205a683, /* lw	a3,32(a1) */
        0x02470713, /* add	a4,a4,36 */
        0x40e607b3, /* sub	a5,a2,a4 */
        0xfc772e23, /* sw	t2,-36(a4) */
        0xfe572023, /* sw	t0,-32(a4) */
        0xfff72223, /* sw	t6,-28(a4) */
        0xffe72423, /* sw	t5,-24(a4) */
        0xffd72623, /* sw	t4,-20(a4) */
        0xffc72823, /* sw	t3,-16(a4) */
        0xfe672a23, /* sw	t1,-12(a4) */
        0xff072c23, /* sw	a6,-8(a4) */
        0xfed72e23, /* sw	a3,-4(a4) */
        0x02458593, /* add	a1,a1,36 */
        0xfaf446e3, /* blt	s0,a5,0x2148c */
        0x00058693, /* mv	a3,a1 */
        0x00070793, /* mv	a5,a4 */
        0x02c77863, /* bgeu	a4,a2,0x2151c */
        0x0006a803, /* lw	a6,0(a3) */
        0x00478793, /* add	a5,a5,4 */
        0x00468693, /* add	a3,a3,4 */
        0xff07ae23, /* sw	a6,-4(a5) */
        0xfec7e8e3, /* bltu	a5,a2,0x214f0 */
        0xfff60793, /* add	a5,a2,-1 */
        0x40e787b3, /* sub	a5,a5,a4 */
        0xffc7f793, /* and	a5,a5,-4 */
        0x00478793, /* add	a5,a5,4 */
        0x00f70733, /* add	a4,a4,a5 */
        0x00f585b3, /* add	a1,a1,a5 */
        0x01176863, /* bltu	a4,a7,0x2152c */
        0x00c12403, /* lw	s0,12(sp) */
        0x01010113, /* add	sp,sp,16 */
        0x00008067, /* ret */
        0x0005c783, /* lbu	a5,0(a1) */
        0x00170713, /* add	a4,a4,1 */
        0x00158593, /* add	a1,a1,1 */
        0xfef70fa3, /* sb	a5,-1(a4) */
        0xfee882e3, /* beq	a7,a4,0x21520 */
        0x0005c783, /* lbu	a5,0(a1) */
        0x00170713, /* add	a4,a4,1 */
        0x00158593, /* add	a1,a1,1 */
        0xfef70fa3, /* sb	a5,-1(a4) */
        0xfce89ee3, /* bne	a7,a4,0x2152c */
        0xfcdff06f, /* j	0x21520 */
    };
    static const uint32_t memcpy2_insn[] = {
        0x00050313, /* mv	t1,a0 */
        0x00060e63, /* beqz	a2,44d18 */
        0x00058383, /* lb	t2,0(a1) */
        0x00730023, /* sb	t2,0(t1) */
        0xfff60613, /* add	a2,a2,-1 */
        0x00130313, /* add	t1,t1,1 */
        0x00158593, /* add	a1,a1,1 */
        0xfe0616e3, /* bnez	a2,44d00 */
        0x00008067, /* ret */
    };
    uint32_t tmp_pc = rv->PC;
    if (lib == 1) {
        for (uint32_t i = 0; i < ARRAYS_SIZE(memcpy_insn); i++) {
            const uint32_t insn = rv->io.mem_ifetch(tmp_pc);
            if (insn != memcpy_insn[i])
                return false;
            tmp_pc += 4;
        }
    } else {
        for (uint32_t i = 0; i < ARRAYS_SIZE(memcpy2_insn); i++) {
            const uint32_t insn = rv->io.mem_ifetch(tmp_pc);
            if (insn != memcpy2_insn[i])
                return false;
            tmp_pc += 4;
        }
    }
    return true;
}

/* Check if instructions in a block match a specific pattern. If they do,
 * rewrite them as fused instructions.
 *
 * Strategies are being devised to increase the number of instructions that
 * match the pattern, including possible instruction reordering.
 */
static void match_pattern(riscv_t *rv, block_t *block)
{
    for (uint32_t i = 0; i < block->n_insn - 1; i++) {
        rv_insn_t *ir = block->ir + i, *next_ir = NULL;
        int32_t count = 0, sign = 1;
        switch (ir->opcode) {
        case rv_insn_addi:
            /* Compare the target block with the first basic block of
             * memset/memcpy, if two block is match, we would extract the
             * instruction sequence starting from the pc_start of the basic
             * block and then compare it with the pre-recorded memset/memcpy
             * instruction sequence.
             */
            if (ir->imm == 15 && ir->rd == rv_reg_t1 &&
                ir->rs1 == rv_reg_zero) {
                next_ir = ir + 1;
                if (next_ir->opcode == rv_insn_addi &&
                    next_ir->rd == rv_reg_a4 && next_ir->rs1 == rv_reg_a0 &&
                    next_ir->rs2 == rv_reg_zero) {
                    next_ir = next_ir + 1;
                    if (next_ir->opcode == rv_insn_bgeu && next_ir->imm == 60 &&
                        next_ir->rs1 == rv_reg_t1 &&
                        next_ir->rs2 == rv_reg_a2) {
                        if (detect_memset(rv, 1)) {
                            ir->opcode = rv_insn_fuse6;
                            ir->impl = dispatch_table[ir->opcode];
                            ir->tailcall = true;
                        };
                    }
                }
            } else if (ir->imm == 0 && ir->rd == rv_reg_t1 &&
                       ir->rs1 == rv_reg_a0) {
                next_ir = ir + 1;
                if (next_ir->opcode == rv_insn_beq &&
                    next_ir->rs1 == rv_reg_a2 && next_ir->rs2 == rv_reg_zero) {
                    if (next_ir->imm == 20 && detect_memset(rv, 2)) {
                        ir->opcode = rv_insn_fuse6;
                        ir->impl = dispatch_table[ir->opcode];
                        ir->tailcall = true;
                    } else if (next_ir->imm == 28 && detect_memcpy(rv, 2)) {
                        ir->opcode = rv_insn_fuse7;
                        ir->impl = dispatch_table[ir->opcode];
                        ir->tailcall = true;
                    };
                }
            }
            break;
        case rv_insn_xor:
            /* Compare the target block with the first basic block of memcpy, if
             * two block is match, we would extract the instruction sequence
             * starting from the pc_start of the basic block and then compare
             * it with the pre-recorded memcpy instruction sequence.
             */
            if (ir->rd == rv_reg_a5 && ir->rs1 == rv_reg_a0 &&
                ir->rs2 == rv_reg_a1) {
                next_ir = ir + 1;
                if (next_ir->opcode == rv_insn_andi && next_ir->imm == 3 &&
                    next_ir->rd == rv_reg_a5 && next_ir->rs1 == rv_reg_a5) {
                    next_ir = next_ir + 1;
                    if (next_ir->opcode == rv_insn_add &&
                        next_ir->rd == rv_reg_a7 && next_ir->rs1 == rv_reg_a0 &&
                        next_ir->rs2 == rv_reg_a2) {
                        next_ir = next_ir + 1;
                        if (next_ir->opcode == rv_insn_bne &&
                            next_ir->imm == 104 && next_ir->rs1 == rv_reg_a5 &&
                            next_ir->rs2 == rv_reg_zero) {
                            if (detect_memcpy(rv, 1)) {
                                ir->opcode = rv_insn_fuse7;
                                ir->impl = dispatch_table[ir->opcode];
                                ir->tailcall = true;
                            };
                        }
                    }
                }
            }
            break;
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
            match_pattern(rv, next);

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
            } else if (last_ir->opcode == rv_insn_jal
#if RV32_HAS(EXT_C)
                       || last_ir->opcode == rv_insn_cj ||
                       last_ir->opcode == rv_insn_cjal
#endif
            ) {
                if (!last_ir->branch_taken)
                    last_ir->branch_taken = block->ir;
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
