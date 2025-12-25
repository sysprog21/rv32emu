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

#if RV32_HAS(EXT_F)
#include <math.h>
#include "softfp.h"
#endif /* RV32_HAS(EXT_F) */

#if RV32_HAS(GDBSTUB)
extern struct target_ops gdbstub_ops;
#endif

#if defined(__EMSCRIPTEN__)
#include "em_runtime.h"
#endif

#include "decode.h"
#include "io.h"
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

#if RV32_HAS(SYSTEM)
#if !RV32_HAS(JIT)
static bool need_clear_block_map = false;
#endif
static uint32_t reloc_enable_mmu_jalr_addr;
static bool reloc_enable_mmu = false;
bool need_retranslate = false;
bool need_handle_signal = false;
#endif

/* Emulate misaligned load operation.
 * Fast-path: Use halfword operations for 2-byte aligned word accesses.
 * Slow-path: Fall back to byte-level operations for odd addresses.
 */
static bool emulate_misaligned_load(riscv_t *rv,
                                    const rv_insn_t *ir,
                                    uint32_t addr)
{
    uint32_t value = 0;

    switch (ir->opcode) {
    case rv_insn_lw:
#if RV32_HAS(EXT_C)
    case rv_insn_clw:
    case rv_insn_clwsp:
#endif
        /* Load word: fast-path for 2-byte aligned, slow-path for odd */
        if ((addr & 1) == 0) {
            /* 2-byte aligned: use two halfword reads */
            value = (uint32_t) rv->io.mem_read_s(rv, addr);
            value |= ((uint32_t) rv->io.mem_read_s(rv, addr + 2)) << 16;
        } else {
            /* Odd address: use four byte reads */
            for (int i = 0; i < 4; i++)
                value |= ((uint32_t) rv->io.mem_read_b(rv, addr + i))
                         << (i * 8);
        }
        rv->X[ir->rd] = value;
        break;

    case rv_insn_lh:
        /* Load halfword (signed): 2 bytes - always use byte reads for odd */
        for (int i = 0; i < 2; i++)
            value |= ((uint32_t) rv->io.mem_read_b(rv, addr + i)) << (i * 8);
        rv->X[ir->rd] = sign_extend_h(value);
        break;

    case rv_insn_lhu:
        /* Load halfword unsigned: 2 bytes - always use byte reads for odd */
        for (int i = 0; i < 2; i++)
            value |= ((uint32_t) rv->io.mem_read_b(rv, addr + i)) << (i * 8);
        rv->X[ir->rd] = value;
        break;

    default:
        return false;
    }

    return true;
}

/* Emulate misaligned store operation.
 * Fast-path: Use halfword operations for 2-byte aligned word accesses.
 * Slow-path: Fall back to byte-level operations for odd addresses.
 */
static bool emulate_misaligned_store(riscv_t *rv,
                                     const rv_insn_t *ir,
                                     uint32_t addr)
{
    uint32_t value;

    switch (ir->opcode) {
    case rv_insn_sw:
#if RV32_HAS(EXT_C)
    case rv_insn_csw:
    case rv_insn_cswsp:
#endif
        /* Store word: fast-path for 2-byte aligned, slow-path for odd */
        value = rv->X[ir->rs2];
        if ((addr & 1) == 0) {
            /* 2-byte aligned: use two halfword writes */
            rv->io.mem_write_s(rv, addr, value & 0xFFFF);
            rv->io.mem_write_s(rv, addr + 2, (value >> 16) & 0xFFFF);
        } else {
            /* Odd address: use four byte writes */
            for (int i = 0; i < 4; i++)
                rv->io.mem_write_b(rv, addr + i, (value >> (i * 8)) & 0xFF);
        }
        break;

    case rv_insn_sh:
        /* Store halfword: 2 bytes - always use byte writes for odd */
        value = rv->X[ir->rs2];
        for (int i = 0; i < 2; i++)
            rv->io.mem_write_b(rv, addr + i, (value >> (i * 8)) & 0xFF);
        break;

    default:
        return false;
    }

    return true;
}

/* Default trap handler for userspace simulation without a configured trap
 * vector. When misaligned memory operations occur, this handler emulates them
 * using byte-level accesses instead of simply skipping the instruction.
 */
static void rv_trap_default_handler(riscv_t *rv)
{
    uint32_t cause = rv->csr_mcause;
    uint32_t tval = rv->csr_mtval; /* Contains the misaligned address */
    uint32_t insn_addr = rv->csr_mepc;

    /* Handle misaligned load/store by emulating with byte operations */
    if (cause == LOAD_MISALIGNED || cause == STORE_MISALIGNED) {
        /* Fetch the faulting instruction */
        uint32_t insn = rv->io.mem_ifetch(rv, insn_addr);
        if (!insn)
            goto skip_insn;

        /* Decode the instruction to determine operation type and registers */
        rv_insn_t ir;
        memset(&ir, 0, sizeof(rv_insn_t));
        if (!rv_decode(&ir, insn))
            goto skip_insn;

        /* Emulate the misaligned operation */
        bool handled = false;
        if (cause == LOAD_MISALIGNED)
            handled = emulate_misaligned_load(rv, &ir, tval);
        else
            handled = emulate_misaligned_store(rv, &ir, tval);

        if (handled) {
            /* Advance PC past the handled instruction */
            rv->csr_mepc += rv->compressed ? 2 : 4;
            rv->PC = rv->csr_mepc;
            return;
        }
    }

skip_insn:
    /* For other exceptions or if emulation failed, skip the instruction */
    rv->csr_mepc += rv->compressed ? 2 : 4;
    rv->PC = rv->csr_mepc; /* mret */
}

#if RV32_HAS(SYSTEM)
static void __trap_handler(riscv_t *rv);
#endif /* RV32_HAS(SYSTEM) */

#if RV32_HAS(ARCH_TEST)
/* Check if the write is to tohost and halt emulation if so.
 * Compliance tests write to tohost and then enter an infinite loop.
 * The emulator should detect this and exit gracefully.
 */
static inline void check_tohost_write(riscv_t *rv,
                                      uint32_t addr,
                                      uint32_t value)
{
    if (rv->tohost_addr && addr == rv->tohost_addr && value != 0) {
        /* Non-zero write to tohost means test wants to exit */
        rv->halt = true;
        /* Extract exit code from tohost value (value >> 1) */
        vm_attr_t *attr = PRIV(rv);
        attr->exit_code = (value >> 1);
    }
}
#endif /* RV32_HAS(ARCH_TEST) */

/* wrap load/store and insn misaligned handler
 * @mask_or_pc: mask for load/store and pc for insn misaligned handler.
 * @type: type of misaligned handler
 * @compress: compressed instruction or not
 * @IO: whether the misaligned handler is for load/store or insn.
 */
#define RV_EXC_MISALIGN_HANDLER(mask_or_pc, type, compress, IO)              \
    IIF(IO)(if (!PRIV(rv)->allow_misalign && unlikely(addr & (mask_or_pc))), \
            if (unlikely(insn_is_misaligned(PC))))                           \
    {                                                                        \
        rv->compressed = compress;                                           \
        rv->csr_cycle = cycle;                                               \
        rv->PC = PC;                                                         \
        SET_CAUSE_AND_TVAL_THEN_TRAP(rv, type##_MISALIGNED,                  \
                                     IIF(IO)(addr, mask_or_pc));             \
        return false;                                                        \
    }

/* FIXME: use more precise methods for updating time, e.g., RTC */
#if RV32_HAS(Zicsr)
static inline void update_time(riscv_t *rv)
{
#if !RV32_HAS(SYSTEM)
    struct timeval tv;

    rv_gettimeofday(&tv);
    rv->timer = (uint64_t) tv.tv_sec * 1e6 + (uint32_t) tv.tv_usec;
#endif
    rv->csr_time[0] = rv->timer & 0xFFFFFFFF;
    rv->csr_time[1] = rv->timer >> 32;
}

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
    case CSR_MEDELEG: /* Machine Exception Delegation Register */
        return (uint32_t *) (&rv->csr_medeleg);
    case CSR_MIDELEG: /* Machine Interrupt Delegation Register */
        return (uint32_t *) (&rv->csr_mideleg);
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
    case CSR_INSTRETH: /* Upper 32 bits of instructions retired */
        return &((uint32_t *) &rv->csr_cycle)[1];
#if RV32_HAS(EXT_F)
    case CSR_FFLAGS:
        return (uint32_t *) (&rv->csr_fcsr);
    case CSR_FCSR:
        return (uint32_t *) (&rv->csr_fcsr);
#endif
    case CSR_SSTATUS:
        return (uint32_t *) (&rv->csr_sstatus);
    case CSR_SIE:
        return (uint32_t *) (&rv->csr_sie);
    case CSR_STVEC:
        return (uint32_t *) (&rv->csr_stvec);
    case CSR_SCOUNTEREN:
        return (uint32_t *) (&rv->csr_scounteren);
    case CSR_SSCRATCH:
        return (uint32_t *) (&rv->csr_sscratch);
    case CSR_SEPC:
        return (uint32_t *) (&rv->csr_sepc);
    case CSR_SCAUSE:
        return (uint32_t *) (&rv->csr_scause);
    case CSR_STVAL:
        return (uint32_t *) (&rv->csr_stval);
    case CSR_SIP:
        return (uint32_t *) (&rv->csr_sip);
    case CSR_SATP:
        return (uint32_t *) (&rv->csr_satp);
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
static uint32_t csr_csrrw(riscv_t *rv,
                          uint32_t csr,
                          uint32_t val,
                          uint64_t cycle)
{
    /* Sync cycle counter for cycle-related CSRs only */
    switch (csr & 0xFFF) {
    case CSR_CYCLE:
    case CSR_CYCLEH:
    case CSR_INSTRET:
    case CSR_INSTRETH:
        if (rv->csr_cycle != cycle)
            rv->csr_cycle = cycle;
        break;
    }

    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    uint32_t out = *c;
#if RV32_HAS(EXT_F)
    if (csr == CSR_FFLAGS)
        out &= FFLAG_MASK;
#endif

#if RV32_HAS(SYSTEM)
    uint32_t old_satp = *c;
#endif
    *c = val;

#if RV32_HAS(SYSTEM)
    /* Flush TLB when SATP actually changes: address space changed */
    if (c == &rv->csr_satp && *c != old_satp)
        mmu_tlb_flush_all(rv);
#if !RV32_HAS(JIT)
    /*
     * guestOS's process might have same VA, so block map cannot be reused
     *
     * Instead of calling block_map_clear() directly here,
     * a flag is set to indicate that the block map should be cleared,
     * and the clearing occurs after the corresponding 'code' of RVOP
     * has executed. This prevents the 'code' of RVOP from potentially
     * accessing a NULL ir.
     */
    if (c == &rv->csr_satp)
        need_clear_block_map = true;
#endif
#endif

    return out;
}

/* perform csrrs (atomic read and set) */
static uint32_t csr_csrrs(riscv_t *rv,
                          uint32_t csr,
                          uint32_t val,
                          uint64_t cycle)
{
    /* Sync cycle counter for cycle-related CSRs only */
    switch (csr & 0xFFF) {
    case CSR_CYCLE:
    case CSR_CYCLEH:
    case CSR_INSTRET:
    case CSR_INSTRETH:
        if (rv->csr_cycle != cycle)
            rv->csr_cycle = cycle;
        break;
    }
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    uint32_t out = *c;
#if RV32_HAS(EXT_F)
    if (csr == CSR_FFLAGS)
        out &= FFLAG_MASK;
#endif

#if RV32_HAS(SYSTEM)
    uint32_t old_satp = *c;
#endif
    *c |= val;

#if RV32_HAS(SYSTEM)
    /* Flush TLB when SATP actually changes */
    if (c == &rv->csr_satp && *c != old_satp)
        mmu_tlb_flush_all(rv);
#endif

    return out;
}

/* perform csrrc (atomic read and clear)
 * Read old value of CSR, zero-extend to XLEN bits, write to rd.
 * Read value from rs1, use as bit mask to clear bits in CSR.
 */
static uint32_t csr_csrrc(riscv_t *rv,
                          uint32_t csr,
                          uint32_t val,
                          uint64_t cycle)
{
    /* Sync cycle counter for cycle-related CSRs only */
    switch (csr & 0xFFF) {
    case CSR_CYCLE:
    case CSR_CYCLEH:
    case CSR_INSTRET:
    case CSR_INSTRETH:
        if (rv->csr_cycle != cycle)
            rv->csr_cycle = cycle;
        break;
    }

    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    uint32_t out = *c;
#if RV32_HAS(EXT_F)
    if (csr == CSR_FFLAGS)
        out &= FFLAG_MASK;
#endif

#if RV32_HAS(SYSTEM)
    uint32_t old_satp = *c;
#endif
    *c &= ~val;

#if RV32_HAS(SYSTEM)
    /* Flush TLB when SATP actually changes */
    if (c == &rv->csr_satp && *c != old_satp)
        mmu_tlb_flush_all(rv);
#endif

    return out;
}
#endif

#if RV32_HAS(GDBSTUB)
void rv_debug(riscv_t *rv)
{
    if (!gdbstub_init(&rv->gdbstub, &gdbstub_ops,
                      (arch_info_t) {
                          .reg_num = 33,
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
    if (unlikely(!block))
        return NULL;
    assert(block);
    block->n_insn = 0;
#if RV32_HAS(SYSTEM_MMIO) && RV32_HAS(MOP_FUSION)
    block->n_lazy_candidates = 0;
    block->lazy_fusion_done = false;
#endif
#if RV32_HAS(JIT)
    block->translatable = true;
    block->hot = false;
    block->hot2 = false;
    block->has_loops = false;
    block->n_invoke = 0;
    INIT_LIST_HEAD(&block->list);
#if RV32_HAS(T2C)
    block->compiled = false;
#endif
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
#define RVOP_NO_NEXT(ir) \
    (!ir->next | rv->debug_mode IIF(RV32_HAS(SYSTEM))(| rv->is_trapped, ))
#else
#define RVOP_NO_NEXT(ir) (!ir->next IIF(RV32_HAS(SYSTEM))(| rv->is_trapped, ))
#endif

/* record whether the branch is taken or not during emulation */
static bool is_branch_taken = false;

/* record the program counter of the previous block */
static uint32_t last_pc = 0;

#if RV32_HAS(JIT)
static set_t pc_set;
static bool has_loops = false;
#endif

#if RV32_HAS(SYSTEM_MMIO)
extern void emu_update_uart_interrupts(riscv_t *rv);
extern void emu_update_rtc_interrupts(riscv_t *rv);
static uint32_t peripheral_update_ctr = 64;
#endif

/* Interpreter-based execution path */
#if RV32_HAS(SYSTEM)
#define RVOP_SYNC_PC(rv, PC) \
    do {                     \
        (rv)->PC = (PC);     \
    } while (0)
#else
#define RVOP_SYNC_PC(rv, PC) \
    do {                     \
    } while (0)
#endif

#define RVOP(inst, code, asm)                                             \
    static PRESERVE_NONE bool do_##inst(riscv_t *rv, const rv_insn_t *ir, \
                                        uint64_t cycle, uint32_t PC)      \
    {                                                                     \
        RVOP_SYNC_PC(rv, PC);                                             \
        IIF(RV32_HAS(SYSTEM))(rv->timer++;, ) cycle++;                    \
        code;                                                             \
        IIF(RV32_HAS(SYSTEM))(                                            \
            if (need_handle_signal) {                                     \
                need_handle_signal = false;                               \
                return true;                                              \
            }, ) nextop : PC += __rv_insn_##inst##_len;                   \
        IIF(RV32_HAS(SYSTEM))(IIF(RV32_HAS(JIT))(                         \
                                  , if (unlikely(need_clear_block_map)) { \
                                      block_map_clear(rv);                \
                                      need_clear_block_map = false;       \
                                      rv->csr_cycle = cycle;              \
                                      rv->PC = PC;                        \
                                      return false;                       \
                                  }), );                                  \
        if (unlikely(RVOP_NO_NEXT(ir)))                                   \
            goto end_op;                                                  \
        const rv_insn_t *next = ir->next;                                 \
        MUST_TAIL return next->impl(rv, next, cycle, PC);                 \
    end_op:                                                               \
        rv->csr_cycle = cycle;                                            \
        rv->PC = PC;                                                      \
        return true;                                                      \
    }

#include "rv32_template.c"
#undef RVOP

/* Helper for fused instruction tail: continue to next or stop.
 * Matches RVOP macro signal handling and block map clearing logic.
 * Note: RVOP returns without saving cycle/PC on signal handling, so we do too.
 */
static inline bool fuse_next_or_stop(riscv_t *rv,
                                     const rv_insn_t *ir,
                                     uint64_t cycle,
                                     uint32_t PC)
{
#if RV32_HAS(SYSTEM)
    if (need_handle_signal) {
        need_handle_signal = false;
        /* Match RVOP: return without saving cycle/PC. The signal handler
         * will determine the appropriate PC from rv->PC (unchanged).
         */
        return true;
    }
#if !RV32_HAS(JIT)
    if (unlikely(need_clear_block_map)) {
        block_map_clear(rv);
        need_clear_block_map = false;
        rv->csr_cycle = cycle;
        rv->PC = PC;
        return false;
    }
#endif
#endif
    if (unlikely(RVOP_NO_NEXT(ir))) {
        rv->csr_cycle = cycle;
        rv->PC = PC;
        return true;
    }
    const rv_insn_t *next = ir->next;
    MUST_TAIL return next->impl(rv, next, cycle, PC);
}

/* multiple LUI */
static PRESERVE_NONE bool do_fuse1(riscv_t *rv,
                                   const rv_insn_t *ir,
                                   uint64_t cycle,
                                   uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    cycle += ir->imm2;
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++)
        rv->X[fuse[i].rd] = fuse[i].imm;
    PC += ir->imm2 * 4;
    return fuse_next_or_stop(rv, ir, cycle, PC);
}

/* LUI + ADD */
static PRESERVE_NONE bool do_fuse2(riscv_t *rv,
                                   const rv_insn_t *ir,
                                   uint64_t cycle,
                                   uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    cycle += 2;
    rv->X[ir->rd] = ir->imm;
    rv->X[ir->rs2] = rv->X[ir->rd] + rv->X[ir->rs1];
    PC += 8;
    return fuse_next_or_stop(rv, ir, cycle, PC);
}

/* multiple SW */
static PRESERVE_NONE bool do_fuse3(riscv_t *rv,
                                   const rv_insn_t *ir,
                                   uint64_t cycle,
                                   uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    cycle += ir->imm2;
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        uint32_t addr = rv->X[fuse[i].rs1] + fuse[i].imm;
        RV_EXC_MISALIGN_HANDLER(3, STORE, false, 1);
        uint32_t value = rv->X[fuse[i].rs2];
        MEM_WRITE_W(rv, addr, value);
#if RV32_HAS(ARCH_TEST)
        check_tohost_write(rv, addr, value);
#endif
    }
    PC += ir->imm2 * 4;
    return fuse_next_or_stop(rv, ir, cycle, PC);
}

/* multiple LW */
static PRESERVE_NONE bool do_fuse4(riscv_t *rv,
                                   const rv_insn_t *ir,
                                   uint64_t cycle,
                                   uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    cycle += ir->imm2;
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        uint32_t addr = rv->X[fuse[i].rs1] + fuse[i].imm;
        RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
        rv->X[fuse[i].rd] = MEM_READ_W(rv, addr);
    }
    PC += ir->imm2 * 4;
    return fuse_next_or_stop(rv, ir, cycle, PC);
}

/* Execute shift operation from fused instruction data.
 * This avoids the unsafe cast from opcode_fuse_t* to rv_insn_t*.
 */
static inline void fuse_shift_exec(riscv_t *rv, const opcode_fuse_t *f)
{
    switch (f->opcode) {
    case rv_insn_slli:
        rv->X[f->rd] = rv->X[f->rs1] << (f->imm & 0x1f);
        break;
    case rv_insn_srli:
        rv->X[f->rd] = rv->X[f->rs1] >> (f->imm & 0x1f);
        break;
    case rv_insn_srai:
        rv->X[f->rd] = ((int32_t) rv->X[f->rs1]) >> (f->imm & 0x1f);
        break;
    default:
        __UNREACHABLE;
        break;
    }
}

/* multiple shift immediate */
static PRESERVE_NONE bool do_fuse5(riscv_t *rv,
                                   const rv_insn_t *ir,
                                   uint64_t cycle,
                                   uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    cycle += ir->imm2;
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++)
        fuse_shift_exec(rv, &fuse[i]);
    PC += ir->imm2 * 4;
    return fuse_next_or_stop(rv, ir, cycle, PC);
}

/* fused LI + ECALL: li a7, imm; ecall
 * This fusion is only available in standard RV32I/M/A/F/C since RV32E
 * uses a different syscall convention (t0 instead of a7).
 */
#if !RV32_HAS(RV32E)
static PRESERVE_NONE bool do_fuse6(riscv_t *rv,
                                   const rv_insn_t *ir,
                                   uint64_t cycle,
                                   uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    cycle += 2;
    rv->X[rv_reg_a7] = ir->imm;
    rv->compressed = false;
    rv->csr_cycle = cycle;
    /* ECALL is at PC+4 (second instruction in fused pair).
     * on_ecall expects rv->PC to be the ECALL address for trap handling.
     */
    rv->PC = PC + 4;
    rv->io.on_ecall(rv);
    return true;
}
#else
/* RV32E stub: fuse6 pattern is never generated for RV32E.
 * Defensive fallback in case of unexpected dispatch.
 */
static PRESERVE_NONE bool do_fuse6(riscv_t *rv UNUSED,
                                   const rv_insn_t *ir UNUSED,
                                   uint64_t cycle UNUSED,
                                   uint32_t PC UNUSED)
{
    assert(!"fuse6 should not be called in RV32E mode");
    return false;
}
#endif

/* fused multiple ADDI */
static PRESERVE_NONE bool do_fuse7(riscv_t *rv,
                                   const rv_insn_t *ir,
                                   uint64_t cycle,
                                   uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    cycle += ir->imm2;
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++)
        /* Use unsigned arithmetic to avoid signed overflow UB.
         * The cast of imm to uint32_t preserves two's complement semantics.
         */
        rv->X[fuse[i].rd] =
            (uint32_t) rv->X[fuse[i].rs1] + (uint32_t) fuse[i].imm;
    PC += ir->imm2 * 4;
    return fuse_next_or_stop(rv, ir, cycle, PC);
}

/* fused LUI + ADDI: lui rd, imm20; addi rd, rd, imm12
 * This is the standard pattern for loading 32-bit constants (li pseudo-op).
 * ir->imm = lui immediate (already shifted << 12)
 * ir->imm2 = addi immediate (sign-extended 12-bit)
 * ir->rd = destination register
 */
static PRESERVE_NONE bool do_fuse8(riscv_t *rv,
                                   const rv_insn_t *ir,
                                   uint64_t cycle,
                                   uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    cycle += 2;
    /* Cast to uint32_t to avoid signed overflow UB */
    rv->X[ir->rd] = (uint32_t) ir->imm + (uint32_t) ir->imm2;
    PC += 8;
    return fuse_next_or_stop(rv, ir, cycle, PC);
}

/* fused LUI + LW: lui rd, imm20; lw rd2, imm12(rd)
 * Common pattern for absolute/PC-relative loads (after AUIPC->LUI constopt).
 * ir->imm = lui immediate (already shifted << 12)
 * ir->imm2 = lw offset (sign-extended 12-bit)
 * ir->rd = lui destination (used as base)
 * ir->rs2 = lw destination register
 */
static PRESERVE_NONE bool do_fuse9(riscv_t *rv,
                                   const rv_insn_t *ir,
                                   uint64_t cycle,
                                   uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    cycle += 2;
    /* Write LUI result to rd - required when rd != LW destination.
     * LUI completes before LW, so this write happens even if LW faults.
     */
    rv->X[ir->rd] = ir->imm;
    /* Cast to uint32_t to avoid signed overflow UB */
    uint32_t addr = (uint32_t) ir->imm + (uint32_t) ir->imm2;
    RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
    rv->X[ir->rs2] = MEM_READ_W(rv, addr);
    PC += 8;
    return fuse_next_or_stop(rv, ir, cycle, PC);
}

/* fused LUI + SW: lui rd, imm20; sw rs2, imm12(rd)
 * Common pattern for absolute/PC-relative stores.
 * ir->imm = lui immediate (already shifted << 12)
 * ir->imm2 = sw offset (sign-extended 12-bit)
 * ir->rd = lui destination (used as base, dead after)
 * ir->rs1 = sw source register (data to store)
 */
static PRESERVE_NONE bool do_fuse10(riscv_t *rv,
                                    const rv_insn_t *ir,
                                    uint64_t cycle,
                                    uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    cycle += 2;
    /* Write LUI result to rd - SW doesn't write registers, so rd may be
     * used later. LUI completes before SW, so this write happens even if
     * SW faults.
     */
    rv->X[ir->rd] = ir->imm;
    /* Cast to uint32_t to avoid signed overflow UB */
    uint32_t addr = (uint32_t) ir->imm + (uint32_t) ir->imm2;
    RV_EXC_MISALIGN_HANDLER(3, STORE, false, 1);
    uint32_t value = rv->X[ir->rs1];
    MEM_WRITE_W(rv, addr, value);
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, value);
#endif
    PC += 8;
    return fuse_next_or_stop(rv, ir, cycle, PC);
}

/* fused LW + ADDI (post-increment): lw rd, 0(rs1); addi rs1, rs1, step
 * Common loop pattern for pointer walking (memcpy, string ops).
 * ir->rd = load destination
 * ir->rs1 = base register (also incremented)
 * ir->imm = load offset
 * ir->imm2 = increment step
 */
static PRESERVE_NONE bool do_fuse11(riscv_t *rv,
                                    const rv_insn_t *ir,
                                    uint64_t cycle,
                                    uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    cycle += 2;
    uint32_t addr = rv->X[ir->rs1] + ir->imm;
    RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
    rv->X[ir->rd] = MEM_READ_W(rv, addr);
    /* Only increment rs1 if load succeeded (no trap in SYSTEM mode).
     * In non-SYSTEM mode, RAM access never faults so this always executes.
     */
#if RV32_HAS(SYSTEM)
    if (!rv->is_trapped)
#endif
        rv->X[ir->rs1] = rv->X[ir->rs1] + ir->imm2;
    PC += 8;
    return fuse_next_or_stop(rv, ir, cycle, PC);
}

/* fused ADDI + BNE: addi rd, rs1, imm; bne rd, x0, offset
 * Common loop counter pattern (countdown loops).
 * ir->rd = counter register (written by addi, tested by bne)
 * ir->rs1 = source register for addi
 * ir->imm = addi immediate (usually -1 for countdown)
 * ir->imm2 = branch offset
 */
static PRESERVE_NONE bool do_fuse12(riscv_t *rv,
                                    const rv_insn_t *ir,
                                    uint64_t cycle,
                                    uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    cycle += 2;
    rv->X[ir->rd] = rv->X[ir->rs1] + ir->imm;

    if (rv->X[ir->rd] != 0) {
        /* Branch taken */
        is_branch_taken = true;
        PC += 4 + ir->imm2; /* ADDI len + branch offset */
        struct rv_insn *taken = ir->branch_taken;
        if (taken) {
#if RV32_HAS(SYSTEM)
            if (!rv->is_trapped) {
                last_pc = PC;
                MUST_TAIL return taken->impl(rv, taken, cycle, PC);
            }
#else
            last_pc = PC;
            MUST_TAIL return taken->impl(rv, taken, cycle, PC);
#endif
        }
    } else {
        /* Branch not taken */
        is_branch_taken = false;
        PC += 8; /* Skip both ADDI and BNE */
        struct rv_insn *untaken = ir->branch_untaken;
        if (untaken) {
#if RV32_HAS(SYSTEM)
            if (!rv->is_trapped) {
                last_pc = PC;
                MUST_TAIL return untaken->impl(rv, untaken, cycle, PC);
            }
#else
            last_pc = PC;
            MUST_TAIL return untaken->impl(rv, untaken, cycle, PC);
#endif
        }
    }

    rv->csr_cycle = cycle;
    rv->PC = PC;
    return true;
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
    IIF(can_branch)(case rv_insn_##inst:, )
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
    IIF(translatable)(case rv_insn_##inst:, )
        RV_INSN_LIST
#undef _
        return true;
    }
    return false;
}
#endif

#if RV32_HAS(BLOCK_CHAINING)
FORCE_INLINE bool insn_is_unconditional_branch(uint8_t opcode)
{
    switch (opcode) {
    case rv_insn_ecall:
    case rv_insn_ebreak:
    case rv_insn_jal:
    case rv_insn_jalr:
    case rv_insn_mret:
#if RV32_HAS(Zicsr)
    case rv_insn_csrrw:
#endif
#if RV32_HAS(SYSTEM)
    case rv_insn_sret:
#endif
#if RV32_HAS(EXT_C)
    case rv_insn_cj:
    case rv_insn_cjalr:
    case rv_insn_cjal:
    case rv_insn_cjr:
    case rv_insn_cebreak:
#endif
        return true;
    default:
        return false;
    }
}

FORCE_INLINE bool insn_is_direct_branch(uint8_t opcode)
{
    switch (opcode) {
    case rv_insn_jal:
#if RV32_HAS(EXT_C)
    case rv_insn_cjal:
    case rv_insn_cj:
#endif
        return true;
    default:
        return false;
    }
}
#endif

FORCE_INLINE bool insn_is_indirect_branch(uint8_t opcode)
{
    switch (opcode) {
    case rv_insn_jalr:
#if RV32_HAS(EXT_C)
    case rv_insn_cjalr:
    case rv_insn_cjr:
#endif
        return true;
    default:
        return false;
    }
}

static bool block_translate(riscv_t *rv, block_t *block)
{
retranslate:
    block->pc_start = block->pc_end = rv->PC;

    rv_insn_t *prev_ir = NULL;
    rv_insn_t *ir = mpool_calloc(rv->block_ir_mp);
    if (unlikely(!ir))
        return false;
    block->ir_head = ir;

    /* translate the basic block */
    while (true) {
        if (prev_ir)
            prev_ir->next = ir;

        /* fetch the next instruction */
        uint32_t insn = rv->io.mem_ifetch(rv, block->pc_end);

#if RV32_HAS(SYSTEM)
        if (!insn && need_retranslate) {
            memset(block, 0, sizeof(block_t));
            need_retranslate = false;
            goto retranslate;
        }
#endif

        /* If instruction fetch failed due to trap (page fault, etc.), break.
         * The caller checks rv->is_trapped and invokes trap handler.
         * Note: insn==0 alone is ambiguous; we verify trap state explicitly.
         */
        if (!insn) {
#if RV32_HAS(SYSTEM)
            assert(rv->is_trapped &&
                   "insn fetch returned 0 without setting trap state");
#endif
            break;
        }

        /* decode the instruction */
        if (!rv_decode(ir, insn)) {
            rv->compressed = is_compressed(insn);
            SET_CAUSE_AND_TVAL_THEN_TRAP(rv, ILLEGAL_INSN, insn);
            break;
        }
        ir->impl = dispatch_table[ir->opcode];
        ir->pc = block->pc_end; /* compute the end of pc */
        block->pc_end += is_compressed(insn) ? 2 : 4;
        block->n_insn++;
        prev_ir = ir;
#if RV32_HAS(JIT)
        if (!insn_is_translatable(ir->opcode))
            block->translatable = false;
#endif
        /* stop on branch */
        if (insn_is_branch(ir->opcode)) {
            if (insn_is_indirect_branch(ir->opcode)) {
                ir->branch_table = calloc(1, sizeof(branch_history_table_t));
                if (unlikely(!ir->branch_table))
                    return false;
                assert(ir->branch_table);
                memset(ir->branch_table->PC, -1,
                       sizeof(uint32_t) * HISTORY_SIZE);
            }
            break;
        }

        ir = mpool_calloc(rv->block_ir_mp);
        if (unlikely(!ir))
            return false;
    }

    /* If no instructions were successfully decoded (e.g., first instruction
     * was illegal), free the allocated IR and return failure.
     */
    if (unlikely(!prev_ir)) {
        mpool_free(rv->block_ir_mp, block->ir_head);
        return false;
    }

    block->ir_tail = prev_ir;
    block->ir_tail->next = NULL;
    return true;
}

#if RV32_HAS(MOP_FUSION)
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

/* Count consecutive instructions with same opcode */
static inline int count_consecutive_insn(rv_insn_t *ir, uint8_t opcode)
{
    int count = 1;
    rv_insn_t *next = ir->next;
    while (next && next->opcode == opcode) {
        count++;
        if (!next->next)
            break;
        next = next->next;
    }
    return count;
}

/* Check if instruction is a shift immediate */
static inline bool is_shift_imm(const rv_insn_t *ir)
{
    return IF_insn(ir, slli) || IF_insn(ir, srli) || IF_insn(ir, srai);
}

/* Count consecutive shift immediate instructions */
static inline int count_consecutive_shift(rv_insn_t *ir)
{
    int count = 1;
    rv_insn_t *next = ir->next;
    while (next && is_shift_imm(next)) {
        count++;
        if (!next->next)
            break;
        next = next->next;
    }
    return count;
}

/* Allocate and rewrite a fused sequence.
 * Returns true on success, false on malloc failure (graceful degradation).
 */
static inline bool try_fuse_sequence(riscv_t *rv,
                                     block_t *block,
                                     rv_insn_t *ir,
                                     int count,
                                     uint8_t fuse_opcode)
{
    if (count <= 1)
        return false;

    /* Overflow check for allocation size */
    if (unlikely((size_t) count > SIZE_MAX / sizeof(opcode_fuse_t)))
        return false;

    opcode_fuse_t *fuse_data = malloc((size_t) count * sizeof(opcode_fuse_t));
    if (unlikely(!fuse_data))
        return false;

    ir->fuse = fuse_data;
    /* Copy original instruction BEFORE changing opcode (preserves original
     * opcode in fuse[0] for handlers like shift_func that need it) */
    memcpy(ir->fuse, ir, sizeof(opcode_fuse_t));
    ir->opcode = fuse_opcode;
    ir->imm2 = count;
    ir->impl = dispatch_table[ir->opcode];

    rv_insn_t *next_ir = ir->next;
    for (int j = 1; j < count; j++, next_ir = next_ir->next)
        memcpy(ir->fuse + j, next_ir, sizeof(opcode_fuse_t));

    remove_next_nth_ir(rv, ir, block, count - 1);
    return true;
}

#if RV32_HAS(SYSTEM_MMIO)
/* Function argument registers (a0-a7) - likely to change between calls.
 * Lazy fusion verified once may become invalid if these registers
 * point to MMIO on subsequent invocations.
 */
#define ARG_REG_MASK                                                  \
    ((1u << 10) | (1u << 11) | (1u << 12) | (1u << 13) | (1u << 14) | \
     (1u << 15) | (1u << 16) | (1u << 17))

/* Check if a LW/SW sequence is safe for lazy fusion verification.
 *
 * Safety requirements:
 * 1. No preceding instruction in the block writes to any rs1 used by sequence
 *    (checked via modified_regs_before - O(1) lookup instead of O(N) scan)
 * 2. No intra-sequence dependency: instruction i's rd != instruction j's rs1
 *    for any j > i (prevents pointer chasing patterns like:
 *    lw x10, 0(x11); lw x12, 0(x10) where x10 changes mid-sequence)
 * 3. No function argument registers (a0-a7) used as base - these may point to
 *    different memory regions (RAM vs MMIO) across function invocations
 *
 * Returns true if safe, false otherwise.
 */
static bool lazy_fusion_safe_base_regs(uint32_t modified_regs_before,
                                       rv_insn_t *seq_start,
                                       int count,
                                       bool is_load)
{
    /* Collect all rs1 registers used by the sequence */
    uint32_t rs1_mask = 0;
    rv_insn_t *ir = seq_start;
    for (int i = 0; i < count && ir; i++, ir = ir->next) {
        if (ir->rs1 < 32)
            rs1_mask |= (1u << ir->rs1);
    }

    /* x0 is never written, remove from mask */
    rs1_mask &= ~1u;

    if (rs1_mask == 0)
        return true; /* Only uses x0, always safe */

    /* Reject sequences using function argument registers (a0-a7).
     * These may point to RAM in one call and MMIO in another,
     * making cached verification unsafe across invocations.
     */
    if (rs1_mask & ARG_REG_MASK)
        return false;

    /* O(1) check: any rs1 written before this sequence? */
    if (rs1_mask & modified_regs_before)
        return false;

    /* Check for intra-sequence dependencies (LW only - SW doesn't write rd).
     * If instruction i writes to rd, and instruction j (j > i) uses rd as rs1,
     * the verification would compute wrong addresses (pointer chasing).
     */
    if (is_load) {
        uint32_t written_mask = 0;
        ir = seq_start;
        for (int i = 0; i < count && ir; i++, ir = ir->next) {
            /* Check if this instruction's rs1 was written by earlier insn */
            if (ir->rs1 != 0 && (written_mask & (1u << ir->rs1))) {
                return false; /* Intra-sequence dependency detected */
            }
            /* Track this instruction's write */
            if (ir->rd != 0)
                written_mask |= (1u << ir->rd);
        }
    }

    return true;
}
#endif

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
#if RV32_HAS(SYSTEM_MMIO)
    /* Track registers modified so far for O(1) lazy fusion safety check */
    uint32_t modified_regs = 0;
#endif
    for (i = 0, ir = block->ir_head; i < block->n_insn - 1;
         i++, ir = ir->next) {
        assert(ir);
        rv_insn_t *next_ir = NULL;
        int32_t count = 0;
        switch (ir->opcode) {
        case rv_insn_lui:
            next_ir = ir->next;
            if (!next_ir)
                break;
            switch (next_ir->opcode) {
            case rv_insn_add:
                /* LUI + ADD fusion (fuse2) */
                if (ir->rd == next_ir->rs2 || ir->rd == next_ir->rs1) {
#if RV32_HAS(SYSTEM_MMIO)
                    /* Track both LUI's rd and ADD's rd before fusion */
                    if (ir->rd != 0)
                        modified_regs |= (1u << ir->rd);
                    if (next_ir->rd != 0)
                        modified_regs |= (1u << next_ir->rd);
#endif
                    ir->opcode = rv_insn_fuse2;
                    ir->rs2 = next_ir->rd;
                    ir->rs1 =
                        (ir->rd == next_ir->rs2) ? next_ir->rs1 : next_ir->rs2;
                    ir->impl = dispatch_table[ir->opcode];
                    remove_next_nth_ir(rv, ir, block, 1);
                }
                break;
            case rv_insn_addi:
                /* LUI + ADDI fusion (fuse8): lui rd, imm; addi rd, rd, imm
                 * This is the standard 32-bit constant load (li pseudo-op).
                 * Skip if rd == x0: LUI x0 produces 0, not imm << 12.
                 */
                if (ir->rd != rv_reg_zero && ir->rd == next_ir->rs1 &&
                    ir->rd == next_ir->rd) {
                    /* ir->imm already has lui's upper immediate (shifted)
                     * Store addi's immediate in imm2 for the handler
                     */
                    ir->imm2 = next_ir->imm;
                    ir->opcode = rv_insn_fuse8;
                    ir->impl = dispatch_table[ir->opcode];
                    remove_next_nth_ir(rv, ir, block, 1);
                }
                break;
            case rv_insn_lw:
                /* LUI + LW fusion (fuse9): lui rd, imm20; lw rd2, imm12(rd)
                 * Common pattern for absolute/PC-relative loads.
                 * The lui result is used as base address for lw.
                 * Skip if rd == x0: LUI x0 produces 0, not imm << 12.
                 *
                 * Note: In JIT+SYSTEM mode, skip this fusion because JIT
                 * computes host addresses at compile time, bypassing MMU.
                 * The interpreter handles SYSTEM mode correctly via io
                 * callbacks.
                 */
#if !(RV32_HAS(JIT) && RV32_HAS(SYSTEM))
                if (ir->rd != rv_reg_zero && ir->rd == next_ir->rs1) {
                    ir->imm2 = next_ir->imm; /* lw offset */
                    ir->rs2 = next_ir->rd;   /* lw destination */
                    ir->opcode = rv_insn_fuse9;
                    ir->impl = dispatch_table[ir->opcode];
                    remove_next_nth_ir(rv, ir, block, 1);
#if RV32_HAS(SYSTEM_MMIO)
                    /* Track rs2 (lw dest) for lazy fusion safety */
                    if (ir->rs2 != 0)
                        modified_regs |= (1u << ir->rs2);
#endif
                }
#endif /* !(JIT && SYSTEM) */
                break;
            case rv_insn_sw:
                /* LUI + SW fusion (fuse10): lui rd, imm20; sw rs2, imm12(rd)
                 * Common pattern for absolute/PC-relative stores.
                 * The lui result is used as base address for sw.
                 * Skip if rd == x0: LUI x0 produces 0, not imm << 12.
                 *
                 * Note: In JIT+SYSTEM mode, skip this fusion because JIT
                 * computes host addresses at compile time, bypassing MMU.
                 */
#if !(RV32_HAS(JIT) && RV32_HAS(SYSTEM))
                if (ir->rd != rv_reg_zero && ir->rd == next_ir->rs1) {
                    ir->imm2 = next_ir->imm; /* sw offset */
                    ir->rs1 = next_ir->rs2;  /* sw source (data to store) */
                    ir->opcode = rv_insn_fuse10;
                    ir->impl = dispatch_table[ir->opcode];
                    remove_next_nth_ir(rv, ir, block, 1);
                }
#endif /* !(JIT && SYSTEM) */
                break;
            case rv_insn_lui:
                /* Multiple LUI fusion (fuse1) */
                count = count_consecutive_insn(ir, rv_insn_lui);
#if RV32_HAS(SYSTEM_MMIO)
                /* Track all rd values before fusion removes instructions */
                {
                    rv_insn_t *tmp = ir;
                    for (int j = 0; j < count && tmp; j++, tmp = tmp->next) {
                        if (tmp->rd != 0)
                            modified_regs |= (1u << tmp->rd);
                    }
                }
#endif
                try_fuse_sequence(rv, block, ir, count, rv_insn_fuse1);
                break;
            }
            break;
            /* Fuse consecutive SW or LW instructions to reduce dispatch
             * overhead. Memory addresses are not required to be contiguous;
             * each fused instruction's address is calculated independently at
             * runtime.
             */
        case rv_insn_sw:
            /* Multiple SW fusion (fuse3) */
            count = count_consecutive_insn(ir, rv_insn_sw);
#if RV32_HAS(SYSTEM_MMIO)
            /* In SYSTEM_MMIO mode, mark as lazy fusion candidate.
             * Fusion will be performed after verifying all addresses are RAM.
             * Skip if base registers are modified before this sequence.
             */
            if (count > 1 && block->n_lazy_candidates < MAX_LAZY_CANDIDATES &&
                lazy_fusion_safe_base_regs(modified_regs, ir, count, false)) {
                lazy_fusion_candidate_t *cand =
                    &block->lazy_candidates[block->n_lazy_candidates++];
                cand->ir = ir;
                cand->count = (uint8_t) count;
                cand->opcode = rv_insn_sw;
                cand->verified = false;
                cand->failed = false;
                /* Skip past sequence to avoid overlapping candidates.
                 * SW doesn't write rd, so no modified_regs update needed.
                 */
                for (int skip = 1; skip < count && ir->next; skip++) {
                    ir = ir->next;
                    i++;
                }
            }
#else
            try_fuse_sequence(rv, block, ir, count, rv_insn_fuse3);
#endif
            break;
        case rv_insn_lw:
            /* Check for LW + ADDI post-increment fusion (fuse11) first.
             * In JIT+SYSTEM mode, skip this fusion because JIT computes
             * host addresses directly, bypassing MMU translation.
             */
#if !(RV32_HAS(JIT) && RV32_HAS(SYSTEM))
            next_ir = ir->next;
            if (next_ir && IF_insn(next_ir, addi) && ir->rs1 == next_ir->rs1 &&
                next_ir->rs1 == next_ir->rd && ir->rd != ir->rs1) {
                /* Pattern: lw rd, imm(rs1); addi rs1, rs1, step
                 * Constraint: rd != rs1 to avoid clobbering base before use
                 */
                ir->imm2 = next_ir->imm; /* increment step */
                ir->opcode = rv_insn_fuse11;
                ir->impl = dispatch_table[ir->opcode];
                remove_next_nth_ir(rv, ir, block, 1);
#if RV32_HAS(SYSTEM_MMIO)
                /* Track both rd (lw dest) and rs1 (post-increment) */
                if (ir->rd != 0)
                    modified_regs |= (1u << ir->rd);
                if (ir->rs1 != 0)
                    modified_regs |= (1u << ir->rs1);
#endif
                break;
            }
#endif /* !(JIT && SYSTEM) */
            /* Multiple LW fusion (fuse4) */
            count = count_consecutive_insn(ir, rv_insn_lw);
#if RV32_HAS(SYSTEM_MMIO)
            /* In SYSTEM_MMIO mode, mark as lazy fusion candidate.
             * Fusion will be performed after verifying all addresses are RAM.
             * Skip if base registers are modified before or within sequence.
             */
            if (count > 1 && block->n_lazy_candidates < MAX_LAZY_CANDIDATES &&
                lazy_fusion_safe_base_regs(modified_regs, ir, count, true)) {
                lazy_fusion_candidate_t *cand =
                    &block->lazy_candidates[block->n_lazy_candidates++];
                cand->ir = ir;
                cand->count = (uint8_t) count;
                cand->opcode = rv_insn_lw;
                cand->verified = false;
                cand->failed = false;
                /* Skip past sequence to avoid overlapping candidates.
                 * Track all rd writes for subsequent safety checks.
                 */
                for (int skip = 1; skip < count && ir->next; skip++) {
                    if (ir->rd != 0)
                        modified_regs |= (1u << ir->rd);
                    ir = ir->next;
                    i++;
                }
            }
#else
            try_fuse_sequence(rv, block, ir, count, rv_insn_fuse4);
#endif
            break;
            /* TODO: mixture of SW and LW */
            /* TODO: reorder instruction to match pattern */
        case rv_insn_slli:
        case rv_insn_srli:
        case rv_insn_srai:
            /* Multiple shift immediate fusion (fuse5) */
            count = count_consecutive_shift(ir);
#if RV32_HAS(SYSTEM_MMIO)
            /* Track all rd values before fusion removes instructions */
            {
                rv_insn_t *tmp = ir;
                for (int j = 0; j < count && tmp; j++, tmp = tmp->next) {
                    if (tmp->rd != 0)
                        modified_regs |= (1u << tmp->rd);
                }
            }
#endif
            try_fuse_sequence(rv, block, ir, count, rv_insn_fuse5);
            break;
        case rv_insn_addi:
            next_ir = ir->next;
#if !RV32_HAS(RV32E)
            /* LI a7 + ECALL fusion (fuse6): li a7, imm; ecall */
            if (ir->rd == rv_reg_a7 && ir->rs1 == rv_reg_zero && next_ir &&
                IF_insn(next_ir, ecall)) {
                ir->opcode = rv_insn_fuse6;
                ir->impl = dispatch_table[ir->opcode];
                remove_next_nth_ir(rv, ir, block, 1);
                break;
            }
#endif
            /* ADDI + BNE loop counter fusion (fuse12):
             * addi rd, rs1, imm; bne rd, x0, offset
             * Common pattern for countdown loops.
             * Skip if rd == x0: ADDI x0 produces 0, breaking branch logic.
             */
            if (next_ir && IF_insn(next_ir, bne) && ir->rd != rv_reg_zero &&
                ir->rd == next_ir->rs1 && next_ir->rs2 == rv_reg_zero) {
                ir->imm2 = next_ir->imm; /* branch offset */
                ir->opcode = rv_insn_fuse12;
                ir->impl = dispatch_table[ir->opcode];
                /* Copy branch targets for block chaining */
                ir->branch_taken = next_ir->branch_taken;
                ir->branch_untaken = next_ir->branch_untaken;
                remove_next_nth_ir(rv, ir, block, 1);
                break;
            }
            /* Multiple ADDI fusion (fuse7) */
            count = count_consecutive_insn(ir, rv_insn_addi);
#if RV32_HAS(SYSTEM_MMIO)
            /* Track all rd values before fusion removes instructions */
            {
                rv_insn_t *tmp = ir;
                for (int j = 0; j < count && tmp; j++, tmp = tmp->next) {
                    if (tmp->rd != 0)
                        modified_regs |= (1u << tmp->rd);
                }
            }
#endif
            try_fuse_sequence(rv, block, ir, count, rv_insn_fuse7);
            break;
        }
#if RV32_HAS(SYSTEM_MMIO)
        /* Track register modification for non-fused instructions.
         * Fused instructions track their writes explicitly above.
         */
        if (ir->rd != 0)
            modified_regs |= (1u << ir->rd);
#endif
    }
}

#if RV32_HAS(SYSTEM_MMIO)
/* Minimum block executions before attempting lazy fusion.
 * Avoids verification overhead on cold blocks.
 */
#define LAZY_FUSION_HOTNESS_THRESHOLD 8

/* Verify addresses for lazy fusion candidates and fuse if all are RAM.
 * Called on cache hit when lazy_fusion_done is false.
 * Uses current register values to compute addresses.
 *
 * Safety guards:
 * - Disabled when MMU is active (virtual addresses unreliable)
 * - Only runs after block is "hot" (executed multiple times)
 * - Base register mutation checked at candidate marking time
 */
static void try_lazy_fusion(riscv_t *rv, block_t *block)
{
    if (block->lazy_fusion_done || block->n_lazy_candidates == 0)
        return;

#if RV32_HAS(SYSTEM)
    /* Disable lazy fusion when MMU is active.
     * Virtual addresses may not correspond to physical RAM regions,
     * and we cannot do side-effect-free address translation here.
     */
    if (rv->csr_satp != 0) {
        block->lazy_fusion_done = true; /* Never retry with MMU */
        return;
    }
#endif

#if RV32_HAS(JIT)
    /* Only attempt fusion for hot blocks to avoid cold block overhead */
    if (block->n_invoke < LAZY_FUSION_HOTNESS_THRESHOLD)
        return;
#endif

    /* Check if all candidates have reached a final state */
    bool all_finalized = true;

    for (uint8_t i = 0; i < block->n_lazy_candidates; i++) {
        lazy_fusion_candidate_t *cand = &block->lazy_candidates[i];
        if (cand->failed || cand->verified)
            continue;

        /* Verify all addresses in this candidate are RAM (not MMIO) */
        bool all_ram = true;
        rv_insn_t *ir = cand->ir;
        for (int j = 0; j < cand->count && ir; j++, ir = ir->next) {
            /* Compute address: base + offset */
            uint32_t addr = rv->X[ir->rs1] + (uint32_t) ir->imm;

            /* Check if address is within RAM bounds */
            if (!GUEST_RAM_CONTAINS(PRIV(rv)->mem, addr, 4)) {
                all_ram = false;
                cand->failed = true;
                break;
            }
        }

        if (all_ram) {
            /* Attempt fusion - may fail due to allocation */
            uint8_t fuse_opcode =
                (cand->opcode == rv_insn_lw) ? rv_insn_fuse4 : rv_insn_fuse3;
            if (try_fuse_sequence(rv, block, cand->ir, cand->count,
                                  fuse_opcode)) {
                cand->verified = true;
            } else {
                /* Allocation failed, can retry later */
                all_finalized = false;
            }
        }
    }

    /* Only mark done when all candidates have final state */
    if (all_finalized)
        block->lazy_fusion_done = true;
}
#endif /* RV32_HAS(SYSTEM_MMIO) */
#endif /* RV32_HAS(MOP_FUSION) */

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
    block_t *next_blk = block_find(map, rv->PC);
#else
    /* lookup the next block in the block cache */
    block_t *next_blk = (block_t *) cache_get(rv->block_cache, rv->PC, true);
#if RV32_HAS(SYSTEM)
    /* discard cache if satp mismatch or block was invalidated by SFENCE.VMA */
    if (next_blk && (next_blk->satp != rv->csr_satp || next_blk->invalidated))
        next_blk = NULL;
#endif
#endif

    if (next_blk) {
#if RV32_HAS(SYSTEM_MMIO) && RV32_HAS(MOP_FUSION)
        /* On cache hit (second execution onwards), attempt lazy fusion
         * for LW/SW sequences after verifying addresses are RAM.
         */
        try_lazy_fusion(rv, next_blk);
#endif
        return next_blk;
    }

#if !RV32_HAS(JIT)
    /* clear block list if it is going to be filled */
    if (map->size * 1.25 > map->block_capacity) {
        block_map_clear(rv);
        prev = NULL;
    }
#endif
    /* allocate a new block */
    next_blk = block_alloc(rv);
    if (unlikely(!next_blk))
        return NULL;

    if (unlikely(!block_translate(rv, next_blk)))
        return NULL;

#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
    /*
     * May be an ifetch fault which changes satp, Do not do this
     * in "block_alloc()"
     */
    next_blk->satp = rv->csr_satp;
    next_blk->invalidated = false;
#endif

    optimize_constant(rv, next_blk);
#if RV32_HAS(MOP_FUSION)
    /* macro operation fusion */
    match_pattern(rv, next_blk);
#endif

#if !RV32_HAS(JIT)
    /* insert the block into block map */
    block_insert(&rv->block_map, next_blk);
#else
    list_add(&next_blk->list, &rv->block_list);

#if RV32_HAS(T2C)
    pthread_mutex_lock(&rv->cache_lock);
#endif

    /* insert the block into block cache */
    block_t *replaced_blk = cache_put(rv->block_cache, rv->PC, next_blk);

    if (!replaced_blk) {
#if RV32_HAS(T2C)
        pthread_mutex_unlock(&rv->cache_lock);
#endif
        return next_blk;
    }

    if (prev == replaced_blk)
        prev = NULL;

    /* remove the connection from parents */
    rv_insn_t *replaced_blk_entry = replaced_blk->ir_head;

    /* TODO: record parents of each block to avoid traversing all blocks */
    block_t *entry;
    list_for_each_entry (entry, &rv->block_list, list) {
        rv_insn_t *taken = entry->ir_tail->branch_taken,
                  *untaken = entry->ir_tail->branch_untaken;

        if (taken == replaced_blk_entry) {
            entry->ir_tail->branch_taken = NULL;
        }
        if (untaken == replaced_blk_entry) {
            entry->ir_tail->branch_untaken = NULL;
        }

        /* upadte JALR LUT */
        if (!entry->ir_tail->branch_table) {
            continue;
        }

        /**
         * TODO: upadate all JALR instructions which references to this
         * basic block as the destination.
         */
    }

    /* free IRs in replaced block */
    for (rv_insn_t *ir = replaced_blk->ir_head, *next_ir; ir != NULL;
         ir = next_ir) {
        next_ir = ir->next;

        if (ir->fuse)
            free(ir->fuse);

        mpool_free(rv->block_ir_mp, ir);
    }

    list_del_init(&replaced_blk->list);
    mpool_free(rv->block_mp, replaced_blk);
#if RV32_HAS(T2C)
    pthread_mutex_unlock(&rv->cache_lock);
#endif
#endif

    assert(next_blk);
    return next_blk;
}

/* We disable profiler to make sure every guest instructions be translated by
 * JIT compiler in architecture test.
 */
#if RV32_HAS(JIT) && !RV32_HAS(ARCH_TEST)
static bool runtime_profiler(riscv_t *rv, block_t *block)
{
#if RV32_HAS(SYSTEM)
    if (block->satp != rv->csr_satp)
        return false;
#endif
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
    if (unlikely(freq >= THRESHOLD))
        return true;
    return false;
}
#endif

#if RV32_HAS(SYSTEM_MMIO)
static bool rv_has_plic_trap(riscv_t *rv)
{
    return ((rv->csr_sstatus & SSTATUS_SIE || !rv->priv_mode) &&
            (rv->csr_sip & rv->csr_sie));
}

static void rv_check_interrupt(riscv_t *rv)
{
    vm_attr_t *attr = PRIV(rv);
    if (peripheral_update_ctr-- == 0) {
        peripheral_update_ctr = 64;

#if defined(__EMSCRIPTEN__)
    escape_seq:
#endif
        u8250_check_ready(PRIV(rv)->uart);
        if (PRIV(rv)->uart->in_ready)
            emu_update_uart_interrupts(rv);

#if RV32_HAS(GOLDFISH_RTC)
        if (PRIV(rv)->rtc->irq_enabled) {
            uint64_t now_nsec = rtc_get_now_nsec(PRIV(rv)->rtc);
            if (rtc_alarm_fire(PRIV(rv)->rtc, now_nsec)) {
                PRIV(rv)->rtc->alarm_status = 1;
                PRIV(rv)->rtc->interrupt_status = 1;
                emu_update_rtc_interrupts(rv);
            }
        }
#endif /* RV32_HAS(GOLDFISH_RTC) */
    }

    if (rv->timer > attr->timer)
        rv->csr_sip |= RV_INT_STI;
    else
        rv->csr_sip &= ~RV_INT_STI;

    if (rv_has_plic_trap(rv)) {
        uint32_t intr_applicable = rv->csr_sip & rv->csr_sie;
        uint8_t intr_idx = ilog2(intr_applicable);
        switch (intr_idx) {
        case (SUPERVISOR_SW_INTR & 0xf):
            SET_CAUSE_AND_TVAL_THEN_TRAP(rv, SUPERVISOR_SW_INTR, 0);
            break;
        case (SUPERVISOR_TIMER_INTR & 0xf):
            SET_CAUSE_AND_TVAL_THEN_TRAP(rv, SUPERVISOR_TIMER_INTR, 0);
            break;
        case (SUPERVISOR_EXTERNAL_INTR & 0xf):
            SET_CAUSE_AND_TVAL_THEN_TRAP(rv, SUPERVISOR_EXTERNAL_INTR, 0);
#if defined(__EMSCRIPTEN__)
            /* escape sequence has more than 1 byte */
            if (input_buf_size)
                goto escape_seq;
#endif
            break;
        default:
            break;
        }
    }
}
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
#if RV32_HAS(SYSTEM_MMIO)
        /* check for any interrupt after every block emulation */
        rv_check_interrupt(rv);
#endif

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
        if (unlikely(!block)) {
#if RV32_HAS(SYSTEM)
            /* Check if a trap is pending (page fault during translation).
             * If so, invoke trap handler and continue instead of halting.
             */
            if (rv->is_trapped) {
                trap_handler(rv);
                prev = NULL;
                continue;
            }
#endif
            rv_log_fatal("Failed to allocate or translate block at PC=0x%08x",
                         rv->PC);
            rv->halt = true;
            return;
        }
        assert(block);

#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
        assert(block->satp == rv->csr_satp && !block->invalidated);
#endif

#if !RV32_HAS(SYSTEM)
        /* on exit */
        if (unlikely(block->ir_head->pc == PRIV(rv)->exit_addr))
            PRIV(rv)->on_exit = true;
#endif

        /* After emulating the previous block, it is determined whether the
         * branch is taken or not. The IR array of the current block is then
         * assigned to either the branch_taken or branch_untaken pointer of
         * the previous block.
         */

#if RV32_HAS(BLOCK_CHAINING)
        if (prev
#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
            && prev->satp == rv->csr_satp && !prev->invalidated
#endif
        ) {
            rv_insn_t *last_ir = prev->ir_tail;
            /* chain block */
            if (!insn_is_unconditional_branch(last_ir->opcode)) {
                if (is_branch_taken && !last_ir->branch_taken) {
                    last_ir->branch_taken = block->ir_head;
                } else if (!is_branch_taken && !last_ir->branch_untaken) {
                    last_ir->branch_untaken = block->ir_head;
                }
            } else if (insn_is_direct_branch(last_ir->opcode)) {
                if (!last_ir->branch_taken) {
                    last_ir->branch_taken = block->ir_head;
                }
            }
        }
#endif
        last_pc = rv->PC;
#if RV32_HAS(JIT)
#if RV32_HAS(T2C)
        /* executed through the tier-2 JIT compiler */
        if (__atomic_load_n(&block->hot2, __ATOMIC_ACQUIRE)) {
            /* Atomic load-acquire pairs with store-release in t2c_compile().
             * Ensures we see the updated block->func after observing hot2=true.
             */
#if defined(__aarch64__)
            /* Ensure instruction cache coherency before executing T2C code */
            __asm__ volatile("isb" ::: "memory");
#endif
            ((exec_t2c_func_t) block->func)(rv);
            prev = NULL;
            continue;
        } /* check if invoking times of t1 generated code exceed threshold */
        else if (!block->compiled && block->n_invoke >= THRESHOLD) {
            block->compiled = true;
            queue_entry_t *entry = malloc(sizeof(queue_entry_t));
            if (unlikely(!entry)) {
                /* Malloc failed - reset compiled flag to allow retry later */
                block->compiled = false;
                continue;
            }
            entry->block = block;
            pthread_mutex_lock(&rv->wait_queue_lock);
            list_add(&entry->list, &rv->wait_queue);
            pthread_mutex_unlock(&rv->wait_queue_lock);
        }
#endif
        /* executed through the tier-1 JIT compiler */
        struct jit_state *state = rv->jit_state;
        /*
         * TODO: We do not explicitly need the translated block. We only need
         *       the program counter as a key for searching the corresponding
         *       entry in compiled binary buffer.
         */
        if (block->hot) {
            block->n_invoke++;
#if defined(__aarch64__)
            /* Ensure instruction cache coherency before executing JIT code */
            __asm__ volatile("isb" ::: "memory");
#endif
            ((exec_block_func_t) state->buf)(
                rv, (uintptr_t) (state->buf + block->offset));
            prev = NULL;
            continue;
        } /* check if the execution path is potential hotspot */
        if (block->translatable
#if !RV32_HAS(ARCH_TEST)
            && runtime_profiler(rv, block)
#endif
        ) {
            jit_translate(rv, block);
#if defined(__aarch64__)
            /* Ensure instruction cache coherency before executing JIT code */
            __asm__ volatile("isb" ::: "memory");
#endif
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

    /* Incremental memory maintenance: reclaim unused pages periodically.
     * Using a 16-bit counter, this runs every 65536 rv_step() calls.
     */
    static uint16_t gc_counter = 0;
    if (unlikely(++gc_counter == 0))
        memory_gc();

#ifdef __EMSCRIPTEN__
    if (rv_has_halted(rv)) {
        emscripten_cancel_main_loop();
        rv_delete(rv); /* clean up and reuse memory */
        rv_log_info("RISC-V emulator is destroyed");
        enable_run_button();
    }
#endif
}

void rv_step_debug(void *arg)
{
    assert(arg);
    riscv_t *rv = arg;

#if RV32_HAS(SYSTEM_MMIO)
    rv_check_interrupt(rv);
#endif

#if !RV32_HAS(SYSTEM)
    /* on exit */
    if (unlikely(rv->PC == PRIV(rv)->exit_addr))
        PRIV(rv)->on_exit = true;
#endif

    rv_insn_t ir;

retranslate:
    memset(&ir, 0, sizeof(rv_insn_t));

    /* fetch the next instruction */
    uint32_t insn = rv->io.mem_ifetch(rv, rv->PC);
#if RV32_HAS(SYSTEM)
    if (!insn && need_retranslate) {
        need_retranslate = false;
        goto retranslate;
    }
#endif

    /* If instruction fetch failed (page fault, etc.), invoke trap handler.
     * In SYSTEM mode, insn==0 should always coincide with trap state.
     */
    if (!insn) {
#if RV32_HAS(SYSTEM)
        assert(rv->is_trapped &&
               "insn fetch returned 0 without setting trap state");
        trap_handler(rv);
#endif
        return;
    }

    /* decode the instruction */
    if (!rv_decode(&ir, insn)) {
        rv->compressed = is_compressed(insn);
        SET_CAUSE_AND_TVAL_THEN_TRAP(rv, ILLEGAL_INSN, insn);
        return;
    }

    ir.impl = dispatch_table[ir.opcode];
    ir.pc = rv->PC;
    ir.next = NULL;
    ir.impl(rv, &ir, rv->csr_cycle, rv->PC);
    return;
}

#if RV32_HAS(SYSTEM)
static void __trap_handler(riscv_t *rv)
{
    rv_insn_t *ir = mpool_calloc(rv->block_ir_mp);
    assert(ir);

    /* set to false by sret implementation */
    while (rv->is_trapped && !rv_has_halted(rv)) {
        uint32_t insn = rv->io.mem_ifetch(rv, rv->PC);
        assert(insn);

        rv_decode(ir, insn);
        reloc_enable_mmu_jalr_addr = rv->PC;

        ir->impl = dispatch_table[ir->opcode];
        rv->compressed = is_compressed(insn);
        ir->impl(rv, ir, rv->csr_cycle, rv->PC);
    }

    prev = NULL;
}
#endif /* RV32_HAS(SYSTEM) */

/* When a trap occurs in M-mode/S-mode, m/stval is either initialized to zero or
 * populated with exception-specific details to assist software in managing
 * the trap. Otherwise, the implementation never modifies m/stval, although
 * software can explicitly write to it. The hardware platform will define
 * which exceptions are required to informatively set mtval and which may
 * consistently set it to zero.
 *
 * When a hardware breakpoint is triggered or an exception like address
 * misalignment, access fault, or page fault occurs during an instruction
 * fetch, load, or store operation, m/stval is updated with the virtual address
 * that caused the fault. In the case of an illegal instruction trap, m/stval
 * might be updated with the first XLEN or ILEN bits of the offending
 * instruction. For all other traps, m/stval is simply set to zero. However,
 * it is worth noting that a future standard could redefine how m/stval is
 * handled for different types of traps.
 *
 */
static void _trap_handler(riscv_t *rv)
{
    /* m/stvec (Machine/Supervisor Trap-Vector Base Address Register)
     * m/stvec[MXLEN-1:2]: vector base address
     * m/stvec[1:0] : vector mode
     * m/sepc  (Machine/Supervisor Exception Program Counter)
     * m/stval (Machine/Supervisor Trap Value Register)
     * m/scause (Machine/Supervisor Cause Register): store exception code
     * m/sstatus (Machine/Supervisor Status Register): keep track of and
     * controls the hart’s current operating state
     *
     * m/stval and m/scause are set in SET_CAUSE_AND_TVAL_THEN_TRAP
     */
    uint32_t base;
    uint32_t mode;
    uint32_t cause;
    /* user or supervisor */
    if (RV_PRIV_IS_U_OR_S_MODE()) {
        const uint32_t sstatus_sie =
            (rv->csr_sstatus & SSTATUS_SIE) >> SSTATUS_SIE_SHIFT;
        rv->csr_sstatus |= (sstatus_sie << SSTATUS_SPIE_SHIFT);
        rv->csr_sstatus &= ~(SSTATUS_SIE);
        rv->csr_sstatus |= (rv->priv_mode << SSTATUS_SPP_SHIFT);
        rv->priv_mode = RV_PRIV_S_MODE;
        base = rv->csr_stvec & ~0x3;
        mode = rv->csr_stvec & 0x3;
        cause = rv->csr_scause;
        rv->csr_sepc = rv->PC;
#if RV32_HAS(SYSTEM)
        rv->last_csr_sepc = rv->csr_sepc;
#endif
    } else { /* machine */
        const uint32_t mstatus_mie =
            (rv->csr_mstatus & MSTATUS_MIE) >> MSTATUS_MIE_SHIFT;
        rv->csr_mstatus |= (mstatus_mie << MSTATUS_MPIE_SHIFT);
        rv->csr_mstatus &= ~(MSTATUS_MIE);
        rv->csr_mstatus |= (rv->priv_mode << MSTATUS_MPP_SHIFT);
        rv->priv_mode = RV_PRIV_M_MODE;
        base = rv->csr_mtvec & ~0x3;
        mode = rv->csr_mtvec & 0x3;
        cause = rv->csr_mcause;
        rv->csr_mepc = rv->PC;
        if (!rv->csr_mtvec) { /* in case CSR is not configured */
            rv_trap_default_handler(rv);
            return;
        }
    }
    switch (mode) {
    /* DIRECT: All traps set PC to base */
    case 0:
        rv->PC = base;
        break;
    /* VECTORED: Asynchronous traps set PC to base + 4 * code */
    case 1:
        /* MSB of code is used to indicate whether the trap is interrupt
         * or exception, so it is not considered as the 'real' code */
        rv->PC = base + 4 * (cause & MASK(31));
        break;
    }
    IIF(RV32_HAS(SYSTEM))(if (rv->is_trapped) __trap_handler(rv);, )
}

void trap_handler(riscv_t *rv)
{
    assert(rv);
    _trap_handler(rv);
}

void ebreak_handler(riscv_t *rv)
{
    assert(rv);
    SET_CAUSE_AND_TVAL_THEN_TRAP(rv, BREAKPOINT, rv->PC);
}

void ecall_handler(riscv_t *rv)
{
    assert(rv);

#if RV32_HAS(ELF_LOADER)
    rv->PC += 4;
    syscall_handler(rv);
#elif RV32_HAS(SYSTEM)
    if (rv->priv_mode == RV_PRIV_U_MODE) {
        uint32_t reg_a7 = rv_get_reg(rv, rv_reg_a7);
        switch (reg_a7) { /* trap guestOS's SDL-oriented application syscall */
        case 0xBEEF:
        case 0xC0DE:
        case 0xFEED:
        case 0xBABE:
        case 0xD00D:
            syscall_handler(rv);
            rv->PC += 4;
            break;
        default:
#if RV32_HAS(SDL) && RV32_HAS(SYSTEM_MMIO)
            /*
             * The guestOS may repeatedly open and close the SDL window,
             * and the user could close the application using the application’s
             * built-in exit function. Need to trap the built-in exit and
             * ensure the SDL window and SDL mixer are destroyed properly.
             */
            {
                extern void sdl_video_audio_cleanup();
                if (unlikely(PRIV(rv)->running_sdl && reg_a7 == 93)) {
                    sdl_video_audio_cleanup();
                    PRIV(rv)->running_sdl = false;
                }
            }
#endif
            SET_CAUSE_AND_TVAL_THEN_TRAP(rv, ECALL_U, 0);
            break;
        }
    } else if (rv->priv_mode ==
               RV_PRIV_S_MODE) { /* trap to SBI syscall handler */
        rv->PC += 4;
        syscall_handler(rv);
    }
#else
    SET_CAUSE_AND_TVAL_THEN_TRAP(rv, ECALL_M, 0);
    syscall_handler(rv);
#endif
}

void memset_handler(riscv_t *rv)
{
    memory_t *m = PRIV(rv)->mem;
    uint32_t dest = rv->X[rv_reg_a0];
    uint32_t value = rv->X[rv_reg_a1];
    uint32_t count = rv->X[rv_reg_a2];

    /* Bounds checking to prevent buffer overflow */
    if (dest >= m->mem_size || count > m->mem_size - dest) {
        SET_CAUSE_AND_TVAL_THEN_TRAP(rv, STORE_MISALIGNED, dest);
        return;
    }

    memset((char *) m->mem_base + dest, value, count);
    rv->PC = rv->X[rv_reg_ra] & ~1U;
}

void memcpy_handler(riscv_t *rv)
{
    memory_t *m = PRIV(rv)->mem;
    uint32_t dest = rv->X[rv_reg_a0];
    uint32_t src = rv->X[rv_reg_a1];
    uint32_t count = rv->X[rv_reg_a2];

    /* Bounds checking to prevent buffer overflow */
    if (dest >= m->mem_size || count > m->mem_size - dest) {
        SET_CAUSE_AND_TVAL_THEN_TRAP(rv, STORE_MISALIGNED, dest);
        return;
    }
    if (src >= m->mem_size || count > m->mem_size - src) {
        SET_CAUSE_AND_TVAL_THEN_TRAP(rv, LOAD_MISALIGNED, src);
        return;
    }

    memcpy((char *) m->mem_base + dest, (char *) m->mem_base + src, count);
    rv->PC = rv->X[rv_reg_ra] & ~1U;
}

void dump_registers(riscv_t *rv, char *out_file_path)
{
    FILE *f = out_file_path[0] == '-' ? stdout : fopen(out_file_path, "w");
    if (!f) {
        rv_log_error("Cannot open registers output file");
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
