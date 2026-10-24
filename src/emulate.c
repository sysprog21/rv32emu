/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
#include "syscall_sdl.h"
#include "trace_match.h"
#include "utils.h"
#if RV32_HAS(SYSTEM)
#include "system.h"
#endif

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

#if !RV32_HAS(JIT)
static bool need_clear_block_map = false;
#endif

#if RV32_HAS(SYSTEM)
static uint32_t reloc_enable_mmu_jalr_addr;
static bool reloc_enable_mmu = false;
bool need_retranslate = false;
bool need_handle_signal = false;
#endif

/* Call the usual instruction-fetch target directly while keeping replacement
 * I/O callbacks observable. The iTLB hit path inlines from system.h; the page
 * walker stays out of line.
 */
static inline uint32_t fetch_instruction(riscv_t *rv, uint32_t pc)
{
#if RV32_HAS(SYSTEM)
    if (likely(rv->io.mem_ifetch == rv_mmu_ifetch))
        return mmu_ifetch(rv, pc);
#endif
    return rv->io.mem_ifetch(rv, pc);
}

#if RV32_HAS(T2C) && RV32_HAS(SYSTEM)
/* Return the native entry for PC, binding it from the artifact on a cache
 * miss. Binding fetches the instruction before hashing the page: the fetch
 * validates execute permission, and any fault handler it runs has settled the
 * mapping. Sets *fault when that fetch traps without resolving.
 */
static void *aot_native_entry(riscv_t *rv, bool *fault)
{
    *fault = false;
    const struct jit_cache *cache = rv->jit_cache;
    /* Kernel bindings on global pages serve every address space. */
    if (rv->aot_kernel_shared && rv->priv_mode == RV_PRIV_S_MODE &&
        (rv->csr_satp >> 31)) {
        const uint64_t key = rv->PC | ((uint64_t) SATP_SHARED << 32);
        const struct jit_cache *slot =
            &cache[jit_cache_slot(rv->PC, SATP_SHARED)];
        if (slot->key == key && slot->entry && slot->priv_mode == rv->priv_mode)
            return slot->entry;
    }
    uint64_t key = rv->PC | ((uint64_t) rv->csr_satp << 32);
    const struct jit_cache *slot = &cache[jit_cache_slot(rv->PC, rv->csr_satp)];
    if (slot->key == key && slot->entry && slot->priv_mode == rv->priv_mode)
        return slot->entry;
    const struct aot_entry *first = t2c_aot_has_pc(rv);
    if (!first)
        return NULL;
    /* Zero is also a fetched (illegal) instruction; only a trap is a fault.
     * The interpreter path decodes it and raises the exception.
     */
    const uint64_t traps = rv->aot_trap_epoch;
    if (!fetch_instruction(rv, rv->PC)) {
        *fault = rv->aot_trap_epoch != traps;
        return NULL;
    }
    void *entry = t2c_aot_lookup(rv, first);
    if (entry) {
        /* A fault handler run by the fetch may have switched SATP. Record the
         * page under the address space it belongs to, the current one or
         * SATP_SHARED, and bind it there, so that a fence remapping the page
         * retires the binding.
         */
        const uint32_t space = mmu_note_code(rv, rv->PC, rv->PC + 2);
        key = rv->PC | ((uint64_t) space << 32);
        if (space == SATP_SHARED)
            rv->aot_kernel_shared = true;
        jit_cache_update(rv->jit_cache, key, entry, rv->priv_mode);
    }
    return entry;
}
#endif

#if !RV32_HAS(JIT)
/* Retire blocks left pending by an instruction fence once no trap handler
 * is running their instructions. Returns true if it cleared the map.
 */
static inline bool retire_pending_blocks(riscv_t *rv)
{
    if (likely(!need_clear_block_map)
#if RV32_HAS(SYSTEM)
        || rv->trap_cnt
#endif
    )
        return false;
    block_map_clear(rv);
    need_clear_block_map = false;
    return true;
}

#endif

/* Only system instructions can request a clear and then reach nextop.
 * User FENCE.I returns directly to the dispatcher, which consumes its clear.
 */
#if RV32_HAS(SYSTEM) && !RV32_HAS(JIT)
/* The deferred clear that ends a handler: resume at the next instruction. */
#define RVOP_RETIRE_PENDING()                      \
    do {                                           \
        if (unlikely(retire_pending_blocks(rv))) { \
            rv->csr_cycle = cycle;                 \
            rv->PC = PC;                           \
            return false;                          \
        }                                          \
    } while (0)
#else
#define RVOP_RETIRE_PENDING() \
    do {                      \
    } while (0)
#endif

/* Emulate misaligned load/store operations.
 * Used where a program runs without an operating system: user-mode builds,
 * and system builds that load an ELF program directly. Under a guest OS,
 * misaligned access traps are handled by the guest.
 */
#if !RV32_HAS(SYSTEM) || RV32_HAS(ELF_LOADER)
/* Whether a part of an emulated access faulted, and the handler that ran for
 * the fault already moved past the instruction. The remaining parts must be
 * abandoned: each would fault again and skip one more instruction.
 */
#if RV32_HAS(SYSTEM)
#define MISALIGN_PART_FAULTED() unlikely(need_handle_signal)
#else
#define MISALIGN_PART_FAULTED() false
#endif

/* Read a misaligned word for the integer and floating-point loads.
 * Fast-path: Use halfword operations for 2-byte aligned word accesses.
 * Slow-path: Fall back to byte-level operations for odd addresses.
 */
static uint32_t misaligned_read_w(riscv_t *rv, uint32_t addr)
{
    uint32_t value = 0;

    if ((addr & 1) == 0) {
        value = (uint32_t) rv->io.mem_read_s(rv, addr);
        if (!MISALIGN_PART_FAULTED())
            value |= ((uint32_t) rv->io.mem_read_s(rv, addr + 2)) << 16;
    } else {
        for (int i = 0; i < 4 && !MISALIGN_PART_FAULTED(); i++)
            value |= ((uint32_t) rv->io.mem_read_b(rv, addr + i)) << (i * 8);
    }
    return value;
}

/* Write a misaligned word, splitting it the same way as misaligned_read_w */
static void misaligned_write_w(riscv_t *rv, uint32_t addr, uint32_t value)
{
    if ((addr & 1) == 0) {
        rv->io.mem_write_s(rv, addr, value & 0xFFFF);
        if (!MISALIGN_PART_FAULTED())
            rv->io.mem_write_s(rv, addr + 2, (value >> 16) & 0xFFFF);
    } else {
        for (int i = 0; i < 4 && !MISALIGN_PART_FAULTED(); i++)
            rv->io.mem_write_b(rv, addr + i, (value >> (i * 8)) & 0xFF);
    }
}

/* Emulate misaligned load operation. */
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
        value = misaligned_read_w(rv, addr);
        if (!MISALIGN_PART_FAULTED() && ir->rd)
            rv->X[ir->rd] = value;
        break;

#if RV32_HAS(EXT_F)
    case rv_insn_flw:
#if RV32_HAS(EXT_C)
    case rv_insn_cflw:
    case rv_insn_cflwsp:
#endif
        value = misaligned_read_w(rv, addr);
        if (!MISALIGN_PART_FAULTED())
            rv->F[ir->rd].v = value;
        break;
#endif

    case rv_insn_lh:
        /* Load halfword (signed): 2 bytes - always use byte reads for odd */
        for (int i = 0; i < 2 && !MISALIGN_PART_FAULTED(); i++)
            value |= ((uint32_t) rv->io.mem_read_b(rv, addr + i)) << (i * 8);
        if (!MISALIGN_PART_FAULTED() && ir->rd)
            rv->X[ir->rd] = sign_extend_h(value);
        break;

    case rv_insn_lhu:
        /* Load halfword unsigned: 2 bytes - always use byte reads for odd */
        for (int i = 0; i < 2 && !MISALIGN_PART_FAULTED(); i++)
            value |= ((uint32_t) rv->io.mem_read_b(rv, addr + i)) << (i * 8);
        if (!MISALIGN_PART_FAULTED() && ir->rd)
            rv->X[ir->rd] = value;
        break;

    default:
        return false;
    }

    return true;
}

/* Emulate misaligned store operation. */
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
        misaligned_write_w(rv, addr, rv->X[ir->rs2]);
        break;

#if RV32_HAS(EXT_F)
    case rv_insn_fsw:
#if RV32_HAS(EXT_C)
    case rv_insn_cfsw:
    case rv_insn_cfswsp:
#endif
        misaligned_write_w(rv, addr, rv->F[ir->rs2].v);
        break;
#endif

    case rv_insn_sh:
        /* Store halfword: 2 bytes - always use byte writes for odd */
        value = rv->X[ir->rs2];
        for (int i = 0; i < 2 && !MISALIGN_PART_FAULTED(); i++)
            rv->io.mem_write_b(rv, addr + i, (value >> (i * 8)) & 0xFF);
        break;

    default:
        return false;
    }

    return true;
}

/* Default handler for an exception taken without a configured trap vector, for
 * a program running without an operating system. A misaligned memory operation
 * is emulated with byte-level accesses; any other exception skips the
 * instruction. cause and tval are the trap's, and *epc the exception PC
 * register of the privilege level that took it.
 */
static void rv_trap_default_handler(riscv_t *rv,
                                    uint32_t cause,
                                    uint32_t tval,
                                    uint32_t *epc)
{
#if RV32_HAS(SYSTEM)
    /* An instruction that cannot be fetched cannot be skipped either, and with
     * no OS to map its page, the program cannot go on.
     */
    if (cause == PAGEFAULT_INSN) {
        rv_log_fatal("Instruction page fault at 0x%08x without a trap vector",
                     tval);
        PRIV(rv)->exit_code = EXIT_FAILURE;
        rv->halt = true;
        return;
    }
#endif

    /* Take the length from the instruction itself: rv->compressed describes the
     * last instruction to set it, which need not be this one.
     */
    uint32_t insn = fetch_instruction(rv, *epc);
    const uint32_t len = (insn ? is_compressed(insn) : rv->compressed) ? 2 : 4;

    /* Handle misaligned load/store by emulating with byte operations */
    if (cause == LOAD_MISALIGNED || cause == STORE_MISALIGNED) {
        if (!insn)
            goto skip_insn;

        /* Decode the instruction to determine operation type and registers */
        rv_insn_t ir;
        memset(&ir, 0, sizeof(rv_insn_t));
        if (!rv_decode(&ir, insn))
            goto skip_insn;

        /* Emulate the misaligned operation. Its byte accesses may fault in
         * turn, and a handler that ran for such a fault has already moved past
         * this instruction, so leave the PC to it.
         */
        if (cause == LOAD_MISALIGNED)
            emulate_misaligned_load(rv, &ir, tval);
        else
            emulate_misaligned_store(rv, &ir, tval);
        if (MISALIGN_PART_FAULTED()) {
#if RV32_HAS(SYSTEM)
            /* The fault is settled here; left set, the flag would make the next
             * instruction return early and run again.
             */
            need_handle_signal = false;
#endif
            return;
        }
    }

skip_insn:
    /* Whether it was emulated or not, move past the instruction */
    *epc += len;
    rv->PC = *epc; /* xret */
}
#endif /* !RV32_HAS(SYSTEM) || RV32_HAS(ELF_LOADER) */

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

        /* Only 1 denotes success. Returning the test number directly can turn
         * failures into shell success when its low eight bits are zero.
         */
        vm_attr_t *attr = PRIV(rv);
        attr->exit_code = value == 1 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
}
#endif /* RV32_HAS(ARCH_TEST) */

/* wrap load/store and insn misaligned handler
 * @mask_or_pc: mask for load/store and pc for insn misaligned handler.
 * @type: type of misaligned handler
 * @compress: compressed instruction or not
 * @IO: whether the misaligned handler is for load/store or insn.
 */
/* Atomic instructions require natural alignment here. The option that
 * relaxes ordinary loads and stores must not permit atomic accesses that
 * span two words; misaligned atomicity granules are not implemented.
 */
#define RV_EXC_ALIGNED_ONLY_HANDLER(mask, type)                    \
    if (unlikely(addr & (mask))) {                                 \
        rv->compressed = false;                                    \
        rv->csr_cycle = cycle;                                     \
        rv->PC = PC;                                               \
        SET_CAUSE_AND_TVAL_THEN_TRAP(rv, type##_MISALIGNED, addr); \
        return false;                                              \
    }

#define RV_EXC_MISALIGN_HANDLER(mask_or_pc, type, compress, IO)        \
    IIF(IO)(if (!rv->allow_misalign && unlikely(addr & (mask_or_pc))), \
            if (unlikely(insn_is_misaligned(PC))))                     \
    {                                                                  \
        rv->compressed = compress;                                     \
        rv->csr_cycle = cycle;                                         \
        rv->PC = PC;                                                   \
        SET_CAUSE_AND_TVAL_THEN_TRAP(rv, type##_MISALIGNED,            \
                                     IIF(IO)(addr, mask_or_pc));       \
        return false;                                                  \
    }

/* FIXME: use more precise methods for updating time, e.g., RTC */
#if RV32_HAS(Zicsr)
static inline void update_time(riscv_t *rv)
{
#if !RV32_HAS(SYSTEM)
    struct timeval tv;

    rv_gettimeofday(&tv);
    rv->timer = (uint64_t) tv.tv_sec * 1e6 + (uint32_t) tv.tv_usec;
#else
    /* SYSTEM mode: derive timer from cycle counter.
     * Timer is computed on-demand rather than incremented per-instruction.
     */
    rv->timer = rv->csr_cycle + rv->timer_offset;
#endif
    rv->csr_time[0] = rv->timer & 0xFFFFFFFF;
    rv->csr_time[1] = rv->timer >> 32;
}

#if RV32_HAS(EXT_V)
static inline uint32_t rvv_csr_vcsr_value(const riscv_t *rv)
{
    return (rv->csr_vxrm & 0x3) | ((rv->csr_vxsat & 0x1) << 2);
}

static inline void rvv_csr_sync_vcsr(riscv_t *rv)
{
    rv->csr_vcsr = rvv_csr_vcsr_value(rv);
}

static inline bool rvv_csr_read(riscv_t *rv, uint32_t csr, uint32_t *out)
{
    switch (csr & 0xFFF) {
    case CSR_VSTART:
        *out = rv->csr_vstart;
        return true;
    case CSR_VXSAT:
        *out = rv->csr_vxsat & 0x1;
        return true;
    case CSR_VXRM:
        *out = rv->csr_vxrm & 0x3;
        return true;
    case CSR_VCSR:
        rvv_csr_sync_vcsr(rv);
        *out = rv->csr_vcsr;
        return true;
    case CSR_VL:
        *out = rv->csr_vl;
        return true;
    case CSR_VTYPE:
        *out = rv->csr_vtype;
        return true;
    case CSR_VLENB:
        *out = rv->csr_vlenb;
        return true;
    default:
        return false;
    }
}

/* vstart is architecturally ceil(log2(VLEN)) bits wide; high bits read as
 * zero. VLEN is a power of two, so masking with (VLEN - 1) is exact.
 */
#define RVV_VSTART_MASK ((uint32_t) (VLEN - 1))

static inline bool rvv_csr_is_read_only(uint32_t csr)
{
    switch (csr & 0xFFF) {
    case CSR_VL:
    case CSR_VTYPE:
    case CSR_VLENB:
        return true;
    default:
        return false;
    }
}

static inline void rvv_csr_write(riscv_t *rv, uint32_t csr, uint32_t val)
{
    switch (csr & 0xFFF) {
    case CSR_VSTART:
        rv->csr_vstart = val & RVV_VSTART_MASK;
        break;
    case CSR_VXSAT:
        rv->csr_vxsat = val & 0x1;
        rvv_csr_sync_vcsr(rv);
        break;
    case CSR_VXRM:
        rv->csr_vxrm = val & 0x3;
        rvv_csr_sync_vcsr(rv);
        break;
    case CSR_VCSR:
        rv->csr_vxrm = val & 0x3;
        rv->csr_vxsat = (val >> 2) & 0x1;
        rvv_csr_sync_vcsr(rv);
        break;
    default:
        /* CSR_VL, CSR_VTYPE, CSR_VLENB are read-only; the caller must use
         * rvv_csr_is_read_only() to gate the write attempt and raise an
         * illegal-instruction exception per Zicsr. The default case here
         * is reached only for the zero-mask csrrs/csrrc path that the
         * dispatcher already short-circuits on val=0.
         */
        break;
    }
}
#endif

#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
static void retire_stale_shared(riscv_t *rv);
#endif

#if RV32_HAS(SYSTEM)
/* Copy the bits sstatus shares with mstatus from src into *dst. */
static inline void status_copy_visible(uint32_t *dst, uint32_t src)
{
    *dst = (*dst & ~SSTATUS_VISIBLE_MASK) | (src & SSTATUS_VISIBLE_MASK);
}
#endif

/* get a pointer to a CSR */
static uint32_t *csr_get_ptr(riscv_t *rv, uint32_t csr)
{
    /* csr & 0xFFF prevent sign-extension in decode stage */
    switch (csr & 0xFFF) {
    case CSR_MSTATUS: /* Machine Status: sstatus is the shared view. */
#if RV32_HAS(SYSTEM)
        status_copy_visible(&rv->csr_mstatus, rv->csr_sstatus);
#endif
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

/* Sync cycle counter for cycle/time CSR reads.
 * TIME CSRs need cycle synced so update_time() derives correct timer.
 */
static inline void csr_sync_cycle(riscv_t *rv, uint32_t csr, uint64_t cycle)
{
    switch (csr & 0xFFF) {
    case CSR_CYCLE:
    case CSR_CYCLEH:
    case CSR_INSTRET:
    case CSR_INSTRETH:
    case CSR_TIME:
    case CSR_TIMEH:
        if (rv->csr_cycle != cycle)
            rv->csr_cycle = cycle;
        break;
    }
}

#if RV32_HAS(SYSTEM)
static void csr_status_written(riscv_t *rv, uint32_t *csr)
{
    if (csr == &rv->csr_mstatus) {
        if ((rv->csr_mstatus & MSTATUS_MPP) == (2U << MSTATUS_MPP_SHIFT))
            rv->csr_mstatus &= ~MSTATUS_MPP;
        status_copy_visible(&rv->csr_sstatus, rv->csr_mstatus);
    } else if (csr == &rv->csr_sstatus) {
        status_copy_visible(&rv->csr_mstatus, rv->csr_sstatus);
    } else {
        return;
    }
    mmu_access_context_changed(rv);
}
#endif

/* Apply the side effects of writing CSR c, which held old. A write that
 * leaves the value unchanged has none: a status view is already in sync.
 * A changed SATP switches address space: flush the TLB and retire what the
 * old mappings decoded. The interpreter's block map is cleared after the
 * instruction, since its code may still use the IR.
 */
static void csr_written(riscv_t *rv UNUSED,
                        uint32_t *c UNUSED,
                        uint32_t old UNUSED)
{
#if RV32_HAS(SYSTEM)
    if (*c == old)
        return;
    csr_status_written(rv, c);
    if (c == &rv->csr_satp) {
        T1C_COUNT(satp_writes, 1);
        mmu_tlb_flush_all(rv);
#if RV32_HAS(JIT)
        retire_stale_shared(rv);
        /* Only CSRRW ends a block. Chain edges were learned in the old
         * space, so stop here rather than follow one after CSRRS or CSRRC.
         */
        rv->block_interrupted = true;
#else
        need_clear_block_map = true;
#endif
    }
#endif
}

#if RV32_HAS(EXT_F)
/* fflags and frm are fields of fcsr, so all three bypass csr_get_ptr(). */
static inline bool csr_is_fcsr(uint32_t csr)
{
    return csr == CSR_FFLAGS || csr == CSR_FRM || csr == CSR_FCSR;
}

static inline uint32_t fcsr_read(riscv_t *rv, uint32_t csr)
{
    switch (csr) {
    case CSR_FFLAGS:
        return rv->csr_fcsr & FFLAG_MASK;
    case CSR_FRM:
        return (rv->csr_fcsr >> FRM_SHIFT) & FRM_MASK;
    case CSR_FCSR:
        return rv->csr_fcsr & FCSR_MASK;
    default:
        __UNREACHABLE;
        return 0;
    }
}

static inline void fcsr_write(riscv_t *rv, uint32_t csr, uint32_t val)
{
    switch (csr) {
    case CSR_FFLAGS:
        rv->csr_fcsr = (rv->csr_fcsr & ~FFLAG_MASK) | (val & FFLAG_MASK);
        break;
    case CSR_FRM:
        rv->csr_fcsr = (rv->csr_fcsr & ~(FRM_MASK << FRM_SHIFT)) |
                       ((val & FRM_MASK) << FRM_SHIFT);
        break;
    case CSR_FCSR:
        rv->csr_fcsr = val & FCSR_MASK;
        break;
    default:
        __UNREACHABLE;
        break;
    }
}
#endif

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
    csr_sync_cycle(rv, csr, cycle);
#if RV32_HAS(EXT_V)
    uint32_t rvv_out;
    if (rvv_csr_read(rv, csr, &rvv_out)) {
        /* csrrw/csrrwi always writes (even with rs1=x0). Trap when the
         * target is a read-only V CSR. */
        if (rvv_csr_is_read_only(csr)) {
            SET_CAUSE_AND_TVAL_THEN_TRAP(rv, ILLEGAL_INSN, 0);
            return 0;
        }
        rvv_csr_write(rv, csr, val);
        return rvv_out;
    }
#endif
#if RV32_HAS(EXT_F)
    if (csr_is_fcsr(csr)) {
        const uint32_t out = fcsr_read(rv, csr);
        fcsr_write(rv, csr, val);
        return out;
    }
#endif
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    const uint32_t old = *c;
    *c = val;
    csr_written(rv, c, old);

    return old;
}

/* perform csrrs (atomic read and set) */
static uint32_t csr_csrrs(riscv_t *rv,
                          uint32_t csr,
                          uint32_t val,
                          uint64_t cycle)
{
    csr_sync_cycle(rv, csr, cycle);
#if RV32_HAS(EXT_V)
    uint32_t rvv_out;
    if (rvv_csr_read(rv, csr, &rvv_out)) {
        /* Per Zicsr, csrrs with rs1=x0 (val == 0) must not perform any of
         * the write side effects. The csrrs/csrrsi RVOPs already pass val=0
         * in that case; skip the write so VCSR-mirroring side effects
         * (clobbering vxrm/vxsat) do not fire on a pure read. When val !=
         * 0, an actual write is requested - trap on read-only V CSRs.
         */
        if (val) {
            if (rvv_csr_is_read_only(csr)) {
                SET_CAUSE_AND_TVAL_THEN_TRAP(rv, ILLEGAL_INSN, 0);
                return 0;
            }
            rvv_csr_write(rv, csr, rvv_out | val);
        }
        return rvv_out;
    }
#endif
#if RV32_HAS(EXT_F)
    if (csr_is_fcsr(csr)) {
        const uint32_t out = fcsr_read(rv, csr);
        if (val) /* rs1 = x0 reads without writing, as for V above */
            fcsr_write(rv, csr, out | val);
        return out;
    }
#endif
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    const uint32_t old = *c;
    *c |= val;
    csr_written(rv, c, old);

    return old;
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
    csr_sync_cycle(rv, csr, cycle);
#if RV32_HAS(EXT_V)
    uint32_t rvv_out;
    if (rvv_csr_read(rv, csr, &rvv_out)) {
        /* Same Zicsr no-write-on-x0 rule as csrrs above. */
        if (val) {
            if (rvv_csr_is_read_only(csr)) {
                SET_CAUSE_AND_TVAL_THEN_TRAP(rv, ILLEGAL_INSN, 0);
                return 0;
            }
            rvv_csr_write(rv, csr, rvv_out & ~val);
        }
        return rvv_out;
    }
#endif
#if RV32_HAS(EXT_F)
    if (csr_is_fcsr(csr)) {
        const uint32_t out = fcsr_read(rv, csr);
        if (val) /* rs1 = x0 reads without writing, as for V above */
            fcsr_write(rv, csr, out & ~val);
        return out;
    }
#endif
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    const uint32_t old = *c;
    *c &= ~val;
    csr_written(rv, c, old);

    return old;
}

#if JIT_SYSTEM_HELPERS
uint32_t rv_csr_insn(riscv_t *rv,
                     uint32_t opcode,
                     uint32_t csr,
                     uint32_t val,
                     uint64_t cycle)
{
    switch (opcode) {
    case rv_insn_csrrw:
    case rv_insn_csrrwi:
        return csr_csrrw(rv, csr, val, cycle);
    case rv_insn_csrrs:
    case rv_insn_csrrsi:
        return csr_csrrs(rv, csr, val, cycle);
    case rv_insn_csrrc:
    case rv_insn_csrrci:
        return csr_csrrc(rv, csr, val, cycle);
    default:
        __UNREACHABLE;
    }
}
#endif
#endif

#if RV32_HAS(GDBSTUB)
void rv_debug(riscv_t *rv)
{
#if RV32_HAS_HART_CORO
    if (!rv_coroutine_start(rv)) {
        rv_log_fatal("Failed to initialize hart coroutine");
        return;
    }
#endif
    if (!gdbstub_init(&rv->gdbstub, &gdbstub_ops,
                      (arch_info_t) {
                          .reg_num = 33,
                          .target_desc = TARGET_RV32,
                      },
                      GDBSTUB_COMM))
        goto out;

    rv->debug_mode = true;
    rv->debug_continue = false;
    rv->breakpoint_map = breakpoint_map_new();
    rv->is_interrupted = false;

#ifdef SIGPIPE
    /* A GDB that hangs up without waiting for the reply (its "kill" command, or
     * a dropped connection) must end the session, not the emulator: the stub's
     * next write would otherwise raise SIGPIPE.
     */
    struct sigaction ignore_pipe = {.sa_handler = SIG_IGN}, prev_pipe;
    sigemptyset(&ignore_pipe.sa_mask);
    sigaction(SIGPIPE, &ignore_pipe, &prev_pipe);
#endif
    gdbstub_run(&rv->gdbstub, (void *) rv);
#ifdef SIGPIPE
    sigaction(SIGPIPE, &prev_pipe, NULL);
#endif
    breakpoint_map_destroy(rv->breakpoint_map);
    rv->breakpoint_map = NULL;
    gdbstub_close(&rv->gdbstub);

out:
#if RV32_HAS_HART_CORO
    rv_coroutine_stop();
#endif
    return;
}
#endif /* RV32_HAS(GDBSTUB) */

#if !RV32_HAS(JIT)
/* hash function for the block map */
HASH_FUNC_IMPL(map_hash, BLOCK_MAP_CAPACITY_BITS, 1 << BLOCK_MAP_CAPACITY_BITS)
#endif

/* Initialize a fresh, unpublished block, including after a fetch retry. */
static void block_reset(block_t *block)
{
    block->n_insn = 0;
    block->ir_head = block->ir_tail = NULL;
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
    block->func = NULL;
    INIT_LIST_HEAD(&block->list);
#if RV32_HAS(BLOCK_CHAINING)
    block_init_edge_lists(block);
#endif
#if RV32_HAS(T2C)
    block->compiled = false;
    block->aot_saved = false;
    block->aot_translatable = true;
    block->aot_page_hash = 0;
#endif
#endif
}

/* allocate a basic block */
static block_t *block_alloc(riscv_t *rv)
{
    block_t *block = mpool_alloc(rv->block_mp);
    if (unlikely(!block))
        return NULL;
    block_reset(block);
    return block;
}

#if !RV32_HAS(JIT)
/* Update L1 direct-mapped block cache.
 * Called after block insertion to enable fast lookup for hot paths.
 */
static inline void block_l1_update(riscv_t *rv, block_t *block)
{
    uint32_t idx = (block->pc_start >> BLOCK_L1_INDEX_SHIFT) & BLOCK_L1_MASK;
    rv->block_l1.tags[idx] = block->pc_start;
    rv->block_l1.ptrs[idx] = block;
}

/* insert a block into block map */
static void block_insert(block_map_t *map, riscv_t *rv, const block_t *block)
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

    /* update L1 cache for fast subsequent lookups */
    block_l1_update(rv, (block_t *) block);
}

/* try to locate an already translated block in the block map */
static block_t *block_find(const block_map_t *map,
                           const riscv_t *rv,
                           const uint32_t addr)
{
    assert(map);
    uint32_t index = map_hash(addr);
    const uint32_t mask = map->block_capacity - 1;

    /* find block in block map */
    for (;; index++) {
        block_t *block = map->map[index & mask];
        if (!block)
            return NULL;

        if (block->pc_start == addr && block_matches_context(rv, block))
            return block;
    }
    return NULL;
}

/* Fast block lookup using L1 direct-mapped cache.
 * Falls back to hash table on L1 miss.
 * This is the hot path - optimized for tight loops.
 *
 * Separated arrays: tag array checked first (1KB), pointer loaded on hit.
 * Benchmarked faster than interleaved on x86-64.
 */
static inline block_t *block_lookup_or_find(riscv_t *rv, uint32_t pc)
{
    /* L1 cache lookup - check tag first (avoids loading pointer on miss) */
    uint32_t idx = (pc >> BLOCK_L1_INDEX_SHIFT) & BLOCK_L1_MASK;
    if (likely(rv->block_l1.tags[idx] == pc) &&
        block_matches_context(rv, rv->block_l1.ptrs[idx]))
        return rv->block_l1.ptrs[idx];

    /* L1 miss - fall back to hash table lookup */
    block_t *block = block_find(&rv->block_map, rv, pc);

    /* Populate L1 cache on hash table hit for future lookups */
    if (block) {
        rv->block_l1.tags[idx] = pc;
        rv->block_l1.ptrs[idx] = block;
    }

    return block;
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

#if RV32_HAS_PACKED_TAIL
#define RVOP_NO_NEXT(ir) (!(ir)->next_impl)
#define RVOP_NEXT_IR(ir) ((ir) + 1)
#define RVOP_NEXT_IMPL(ir) ((ir)->next_impl)
#else
#define RVOP_NEXT_IR(ir) ((ir)->next)
#define RVOP_NEXT_IMPL(ir) ((ir)->next->impl)
#if RV32_HAS(GDBSTUB)
#define RVOP_NO_NEXT(ir) \
    (!ir->next |         \
     rv->debug_mode IIF(RV32_HAS(SYSTEM))(| (rv->chain_stop != 0), ))
#else
#define RVOP_NO_NEXT(ir) \
    (!ir->next IIF(RV32_HAS(SYSTEM))(| (rv->chain_stop != 0), ))
#endif
#endif

/* record whether the branch is taken or not during emulation. Only block
 * chaining and fusion read it, so other configurations only set it.
 */
static bool is_branch_taken UNUSED = false;

/* record the program counter of the previous block */
static uint32_t last_pc = 0;
static block_t *prev = NULL;

#if RV32_HAS(JIT)
static set_t pc_set;
static bool has_loops = false;
#endif

#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
/* Under a guest OS, much of the code runs interpreted and briefly: a process
 * lives a short while, and its blocks are discarded at each full SFENCE.VMA.
 * Profiling every branch the interpreter chains through then costs more than
 * the JIT recovers, so RVOP_PROBE_TARGET profiles one in PROBE_INTERVAL of them
 * and scales the hotness threshold to match. Its threshold only ends the chain:
 * runtime_profiler() still decides, with its own thresholds, whether the block
 * is translated. Counting each sample as PROBE_INTERVAL uses instead, with one
 * threshold for both, measured slower on Arm64.
 */
#define PROBE_INTERVAL 32
static uint32_t probe_tick;
#endif

#if RV32_HAS(SYSTEM_MMIO)
void rv_reset_dispatcher_state(void)
{
#if !RV32_HAS(JIT)
    need_clear_block_map = false;
#endif
    is_branch_taken = false;
    reloc_enable_mmu_jalr_addr = 0;
    reloc_enable_mmu = false;
    need_retranslate = false;
    need_handle_signal = false;
    prev = NULL;
    last_pc = 0;
#if RV32_HAS(JIT)
    set_reset(&pc_set);
    has_loops = false;
#endif
}
#endif

/* Wasm helpers execute detached instructions outside rv_step. Start a fresh
 * interruption scope without clearing a marker between an AMO's accesses.
 */
#if defined(__EMSCRIPTEN__)
void rv_begin_wasm_helper(riscv_t *rv UNUSED)
{
#if RV32_HAS(SYSTEM)
    rv->block_interrupted = false;
#endif
}

void reset_rv_run_state(void)
{
    prev = NULL;
    is_branch_taken = false;
    last_pc = 0;
#if RV32_HAS(JIT)
    set_reset(&pc_set);
    has_loops = false;
#endif
#if RV32_HAS(SYSTEM)
    need_retranslate = false;
    need_handle_signal = false;
#endif
}
#endif /* __EMSCRIPTEN__ */

#if RV32_HAS(SYSTEM_MMIO)
extern void emu_update_uart_interrupts(riscv_t *rv);
extern void emu_update_rtc_interrupts(riscv_t *rv);
static uint32_t peripheral_update_ctr = 64;
#endif

#if RV32_T1C_STATS_ENABLED
static inline void t1c_count_block_entry(const rv_insn_t *ir, uint32_t pc)
{
    const block_t *block = ir->stats_owner;
    if (!block || ir != block->ir_head || pc != block->pc_start)
        return;
    const unsigned reason = block->stats_blocker;
    rv_t1c_blockers_entries[reason]++;
    rv_t1c_blockers_insns[reason] += block->cycle_cost;
}
#else
#define t1c_count_block_entry(ir, pc) ((void) 0)
#endif

/* Interpreter-based execution path.
 * Each handler increments the cycle count it carries in a register, so a
 * trap or counter read anywhere in a block sees an exact value. Counting per
 * block instead, as libriscv does, measured no faster. The timer is derived
 * from the cycle count at interrupt check points (rv_check_interrupt) rather
 * than per instruction.
 */
#if RV32_HAS(SYSTEM)
#if RV32_T1C_STATS_ENABLED
#define T1C_INTERPRET(ir)                                \
    do {                                                 \
        T1C_COUNT(interp_pc_syncs, 1);                   \
        if ((ir)->stats_owner && (ir)->stats_owner->hot) \
            T1C_COUNT(hot_interp_pc_syncs, 1);           \
    } while (0)
#else
#define T1C_INTERPRET(ir) ((void) 0)
#endif
#define RVOP_SYNC_PC_ACCESS(rv, PC) \
    do {                            \
        T1C_INTERPRET(ir);          \
        (rv)->PC = (PC);            \
    } while (0)
#else
#define RVOP_SYNC_PC_ACCESS(rv, PC) ((void) 0)
#endif
/* Both macros use the handler's ir. Access syncs inside a fused group must
 * preserve fault PCs without counting its first instruction a second time.
 */
#define RVOP_SYNC_PC(rv, PC)           \
    do {                               \
        t1c_count_block_entry(ir, PC); \
        RVOP_SYNC_PC_ACCESS(rv, PC);   \
    } while (0)

FORCE_INLINE bool insn_is_branch(uint16_t opcode)
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

#ifdef __EMSCRIPTEN__
/* Block-boundary yield strategy to prevent stack overflow in WASM.
 * Two-tier dispatch:
 * - INTRA: Fast path for sequential instructions within a block (no check)
 * - INTER: Checked path for block boundaries (yield point)
 * This preserves kernel atomic operations within blocks while preventing
 * unbounded tail-call recursion in WASM.
 */

/* Instrumentation helpers */
#ifdef WASM_DEBUG_BLOCKS
#define WASM_DEBUG_YIELD_HARD(rv, depth)                        \
    do {                                                        \
        (rv)->yield_hard_count++;                               \
        (rv)->total_depth_at_yield +=                           \
            WASM_BLOCK_LIMIT + WASM_BLOCK_HARD_LIMIT - (depth); \
        if ((depth) < (rv)->max_block_depth_seen)               \
            (rv)->max_block_depth_seen = (depth);               \
    } while (0)
#define WASM_DEBUG_YIELD_SOFT(rv, depth)                          \
    do {                                                          \
        (rv)->yield_soft_count++;                                 \
        (rv)->total_depth_at_yield += WASM_BLOCK_LIMIT - (depth); \
    } while (0)
#else
#define WASM_DEBUG_YIELD_HARD(rv, depth) ((void) 0)
#define WASM_DEBUG_YIELD_SOFT(rv, depth) ((void) 0)
#endif

/* Fast path: Intra-block sequential instructions.
 * Only check hard limit. Let depth go negative to track total stack usage.
 * This allows kernel atomic operations to complete within blocks. */
#define RVOP_TAIL_INTRA(rv, target, cycle, PC)                  \
    do {                                                        \
        int depth = --(rv)->wasm_block_depth;                   \
        /* Only yield at hard limit to protect atomicity */     \
        if (unlikely(depth <= -WASM_BLOCK_HARD_LIMIT)) {        \
            WASM_DEBUG_YIELD_HARD(rv, depth);                   \
            (rv)->wasm_block_depth = WASM_BLOCK_LIMIT;          \
            (rv)->next_insn = (target);                         \
            (rv)->csr_cycle = (cycle);                          \
            (rv)->PC = (PC);                                    \
            return true;                                        \
        }                                                       \
        MUST_TAIL return (target)->impl(rv, target, cycle, PC); \
    } while (0)

/* Checked path: Inter-block transitions (block boundaries) */
#define RVOP_TAIL_INTER(rv, target, cycle, PC)                         \
    do {                                                               \
        int depth = --(rv)->wasm_block_depth;                          \
        /* Yield at soft limit (0) because we're at a safe boundary */ \
        if (unlikely(depth <= 0)) {                                    \
            WASM_DEBUG_YIELD_SOFT(rv, depth);                          \
            (rv)->wasm_block_depth = WASM_BLOCK_LIMIT;                 \
            (rv)->next_insn = (target);                                \
            (rv)->csr_cycle = (cycle);                                 \
            (rv)->PC = (PC);                                           \
            return true;                                               \
        }                                                              \
        MUST_TAIL return (target)->impl(rv, target, cycle, PC);        \
    } while (0)

/* Backward compatibility: RVOP_TAIL maps to INTER for block boundaries */
#define RVOP_TAIL(rv, target, cycle, PC) RVOP_TAIL_INTER(rv, target, cycle, PC)

#else
/* Native builds: no yield checks needed */
#define RVOP_TAIL_INTRA(rv, target, cycle, PC) \
    MUST_TAIL return (target)->impl(rv, target, cycle, PC)
#define RVOP_TAIL_INTER(rv, target, cycle, PC) \
    MUST_TAIL return (target)->impl(rv, target, cycle, PC)
#define RVOP_TAIL(rv, target, cycle, PC) \
    MUST_TAIL return (target)->impl(rv, target, cycle, PC)
#endif

#if RV32_HAS_PACKED_TAIL
#define RVOP_TAIL_INTRA_IMPL(rv, impl, target, cycle, PC) \
    MUST_TAIL return (impl) (rv, target, cycle, PC)
#else
#define RVOP_TAIL_INTRA_IMPL(rv, impl, target, cycle, PC) \
    RVOP_TAIL_INTRA(rv, target, cycle, PC)
#endif

/* Learned edges can stay in a tail-call chain until rv_step's cycle budget
 * expires. SYSTEM follows them only outside a synchronous trap; its dispatcher
 * must regain control to poll interrupts and devices. Packed JIT blocks that
 * T1 can compile return through do_enter_dispatch for profiling instead.
 * Unpacked Wasm also retains its browser-yield check in RVOP_TAIL_INTER.
 */
#if RV32_HAS_PACKED_TAIL
#define RVOP_NATIVE_BRANCH_TAIL(rv, target, cycle, PC)              \
    do {                                                            \
        if (likely((cycle) < (rv)->branch_chain_cycle_target)) {    \
            last_pc = (PC);                                         \
            MUST_TAIL return (target)->impl(rv, target, cycle, PC); \
        }                                                           \
    } while (0)
#define RVOP_CHAIN_TAIL(rv, target, cycle, PC) \
    RVOP_NATIVE_BRANCH_TAIL(rv, target, cycle, PC)
#else
#define RVOP_NATIVE_BRANCH_TAIL(rv, target, cycle, PC) \
    do {                                               \
    } while (0)
/* A system chain also leaves for the dispatcher at a block with live native
 * code, which it then runs natively. Without this, a hot kernel path kept
 * chaining through the interpreter around the blocks T1 had compiled for it,
 * since the profiling probe samples only one transition in PROBE_INTERVAL.
 */
#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
#define RVOP_CHAIN_NATIVE(rv, target) \
    ((target)->native_gen == (uint32_t) jit_code_generation(rv))
#else
#define RVOP_CHAIN_NATIVE(rv, target) false
#endif
/* clang-format off */
#define RVOP_CHAIN_TAIL(rv, target, cycle, PC)                       \
    do {                                                             \
        IIF(RV32_HAS(SYSTEM))(                                       \
            if ((cycle) >= (rv)->branch_chain_cycle_target ||        \
                RVOP_CHAIN_NATIVE(rv, target)) break;, )             \
        last_pc = (PC);                                              \
        RVOP_TAIL_INTER(rv, target, cycle, PC);                      \
    } while (0)
/* clang-format on */
#endif

/* The interpreter's handlers end in indirect jumps to the next handler. At
 * the default function alignment those jumps collide in the host branch
 * predictor depending on where the linker places each handler: two builds of
 * the same interpreter retired the same host instructions on Dhrystone, yet
 * one took 19% more cycles, with 29 times the branch misses. Aligning every
 * handler to 64 bytes keeps the jumps apart. Link-time optimization drops
 * per-file alignment flags, so the attribute goes on the handlers.
 */
#if defined(__EMSCRIPTEN__)
#define RVOP_ALIGN
#else
#define RVOP_ALIGN __attribute__((aligned(64)))
#endif

#define RVOP(inst, code)                                                 \
    static PRESERVE_NONE RVOP_ALIGN bool do_##inst(                      \
        riscv_t *rv, const rv_insn_t *ir, uint64_t cycle, uint32_t PC)   \
    {                                                                    \
        RVOP_SYNC_PC(rv, PC);                                            \
        cycle++;                                                         \
        code;                                                            \
        IIF(RV32_HAS(SYSTEM))(                                           \
            if (need_handle_signal) {                                    \
                need_handle_signal = false;                              \
                rv->block_interrupted = true;                            \
                return true;                                             \
            }, ) nextop : PC += __rv_insn_##inst##_len;                  \
        RVOP_RETIRE_PENDING();                                           \
        if (unlikely(RVOP_NO_NEXT(ir)))                                  \
            goto end_op;                                                 \
        const rv_insn_t *next = RVOP_NEXT_IR(ir);                        \
        RVOP_TAIL_INTRA_IMPL(rv, RVOP_NEXT_IMPL(ir), next, cycle, PC);   \
        /* Fast path: intra-block */                                     \
    end_op:                                                              \
        IIF(RV32_HAS(BLOCK_CHAINING))(                                   \
            {                                                            \
                /* Page-terminated block fallthrough: if branch_taken is \
                 * set AND this is NOT a branch instruction, tail-call   \
                 * to next block. Branch instructions use branch_taken   \
                 * for the taken path, not fallthrough.                  \
                 */                                                      \
                if (!insn_is_branch(ir->opcode)) {                       \
                    struct rv_insn *taken = ir->branch_taken;            \
                    if (taken) {                                         \
                        IIF(RV32_HAS(SYSTEM))(                           \
                            if (!rv->chain_stop) {                       \
                                RVOP_CHAIN_TAIL(rv, taken, cycle, PC);   \
                            },                                           \
                            {                                            \
                                last_pc = PC;                            \
                                RVOP_TAIL(rv, taken, cycle, PC);         \
                            });                                          \
                    }                                                    \
                }                                                        \
            }, );                                                        \
        rv->csr_cycle = cycle;                                           \
        rv->PC = PC;                                                     \
        return true;                                                     \
    }

#if RV32_HAS(JIT) && RV32_HAS_PACKED_TAIL
/* The head handler of a block T1 may compile. A chain reaching it returns to
 * rv_step(), which profiles the block, compiles it once hot and then runs it
 * natively; rv_step() itself interprets the block through @head_impl.
 */
static PRESERVE_NONE RVOP_ALIGN bool do_enter_dispatch(riscv_t *rv,
                                                       const rv_insn_t *ir
                                                       UNUSED,
                                                       uint64_t cycle,
                                                       uint32_t PC)
{
    /* The chain set last_pc to this block, which has not run. Clear it so
     * rv_step() links no edge: the one that led here already exists.
     */
    last_pc = 0;
    rv->csr_cycle = cycle;
    rv->PC = PC;
    return true;
}
#endif
#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
/* Whether code was retired, so native code that called into the MMU must
 * leave and revalidate its bindings. Without T2C nothing reads the flag.
 */
static bool retired_any UNUSED;

static void retire_space(riscv_t *rv, uint32_t satp)
{
    cache_invalidate_satp(rv->block_cache, satp);
    jit_retire_space(rv->jit_state, satp);
    retired_any = true;
#if RV32_HAS(T2C)
    /* Bindings record the address space whose code map holds their page. */
    jit_cache_retire_space(rv->jit_cache, rv->inline_cache, satp);
#endif
    if (satp == SATP_SHARED)
        T1C_COUNT(shared_retires, 1);
}

/* SFENCE.VMA orders page-table updates before later translations. Decoded
 * code depends only on the translations of its pages, so retire the address
 * spaces in which one of those changed, keeping the rest: Linux fences all
 * mappings several times per process, and the kernel's code would otherwise
 * be decoded and compiled again each time. Native links never cross address
 * spaces, so a space retires on its own.
 */
static void retire_code(riscv_t *rv, bool switched, uint32_t vpn)
{
    retired_any = false;
    if (!mmu_check_code_maps(rv, retire_space, switched, vpn)) {
        cache_invalidate_all(rv->block_cache);
        jit_retire_all(rv->jit_state);
        mmu_forget_code_maps(rv);
        retired_any = true;
        T1C_COUNT(shared_retires, 1); /* shared code retires too */
#if RV32_HAS(T2C)
        jit_cache_clear(rv->jit_cache);
        inline_cache_clear(rv->inline_cache);
#endif
    }
#if RV32_HAS(T2C)
    /* Native code that called into the MMU leaves at its next instruction
     * once the epoch moves, and its bindings are verified again on entry.
     */
    if (retired_any)
        rv->aot_code_epoch++;
#endif
}

static void retire_stale_code(riscv_t *rv)
{
    retire_code(rv, false, CODE_MAP_ALL_PAGES);
}

/* A fence for one page orders only that page's leaf entry. */
static void retire_stale_page(riscv_t *rv, uint32_t va)
{
    retire_code(rv, false, va >> RV_PG_SHIFT);
}

/* Shared code is valid only while the current address space maps it alike:
 * check it against each space switched to.
 */
static void retire_stale_shared(riscv_t *rv)
{
    retire_code(rv, true, CODE_MAP_ALL_PAGES);
}
#endif

#include "rv32_template.c"

/* An integer load into x0, which insn_handler() routes here: perform the
 * access, for its trap and any device side effect, and discard the value. The
 * ordinary load handlers thus write rd without testing it.
 */
enum { __rv_insn_load_x0_len = 4 };
RVOP(load_x0, {
    const uint32_t addr = rv->X[ir->rs1] + ir->imm;
    switch (ir->opcode) {
    case rv_insn_lw:
        RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
        (void) MEM_READ_W(rv, addr);
        break;
    case rv_insn_lh:
    case rv_insn_lhu:
        RV_EXC_MISALIGN_HANDLER(1, LOAD, false, 1);
        (void) MEM_READ_S(rv, addr);
        break;
    default:
        (void) MEM_READ_B(rv, addr);
        break;
    }
})
#undef RVOP

#if RV32_HAS_PACKED_TAIL
/* Decode-time specialization for the common uncompressed loop-induction
 * form, addi rd, rd, imm.  Keeping the opcode as ADDI preserves all block
 * and branch metadata; only the native packed handler avoids decoding rs1 a
 * second time.  Other execution modes continue to use do_addi. */
static PRESERVE_NONE RVOP_ALIGN bool do_addi_self(riscv_t *rv,
                                                  const rv_insn_t *ir,
                                                  uint64_t cycle,
                                                  uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    cycle++;
    rv->X[ir->rd] += (uint32_t) ir->imm;
    PC += 4;
    if (unlikely(RVOP_NO_NEXT(ir)))
        goto end_op;
    const rv_insn_t *next = RVOP_NEXT_IR(ir);
    RVOP_TAIL_INTRA_IMPL(rv, RVOP_NEXT_IMPL(ir), next, cycle, PC);

end_op:
#if RV32_HAS(BLOCK_CHAINING)
{
    struct rv_insn *taken = ir->branch_taken;
    if (taken) {
        last_pc = PC;
        RVOP_TAIL(rv, taken, cycle, PC);
    }
}
#endif
    rv->csr_cycle = cycle;
    rv->PC = PC;
    return true;
}

#endif

#if RV32_HAS_TRACE_FUSION && RV32_HAS(MOP_FUSION) && RV32_HAS(EXT_M) && \
    !RV32_HAS(RV32E)
/* IDEA's multiply-mod-65537 round opens with this exact 16-instruction graph
 * (trace_idea_round_prefix).  The three LHU operations retain their original
 * alignment traps; the following BEQ stays generic for branch learning. */
static PRESERVE_NONE RVOP_ALIGN bool do_idea_round_prefix_trace(
    riscv_t *rv,
    const rv_insn_t *ir,
    uint64_t cycle,
    uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);

    uint32_t a4 = rv->X[rv_reg_a4] * rv->X[rv_reg_a6];
    uint32_t a6 = a4 << 16;
    a6 >>= 16;
    a4 >>= 16;
    uint32_t a5 = a6 - a4;
    a6 = (a6 < a4) + a5;
    a6 = (a6 << 16) >> 16;
    cycle += 9;
    PC += 9 * 4;
    rv->X[rv_reg_a4] = a4;
    rv->X[rv_reg_a5] = a5;
    rv->X[rv_reg_a6] = a6;

    uint32_t addr = rv->X[rv_reg_a0] + 4;
    cycle++;
    RV_EXC_MISALIGN_HANDLER(1, LOAD, false, 1);
    rv->X[rv_reg_t1] = MEM_READ_S(rv, addr);
    PC += 4;

    addr = rv->X[rv_reg_a0] + 2;
    cycle++;
    RV_EXC_MISALIGN_HANDLER(1, LOAD, false, 1);
    rv->X[rv_reg_a5] = MEM_READ_S(rv, addr);
    PC += 4;

    addr = rv->X[rv_reg_a0] + 6;
    cycle++;
    RV_EXC_MISALIGN_HANDLER(1, LOAD, false, 1);
    rv->X[rv_reg_a7] = MEM_READ_S(rv, addr);
    PC += 4;

    uint32_t t1 = rv->X[rv_reg_t5] + rv->X[rv_reg_t1];
    t1 = (t1 << 16) >> 16;
    rv->X[rv_reg_t1] = t1;
    rv->X[rv_reg_t5] = rv->X[rv_reg_t0] + rv->X[rv_reg_a5];
    cycle += 4;
    PC += 4 * 4;

    /* Fusion leaves the BEQ as the next packed record. */
    const rv_insn_t *next = ir + 1;
    MUST_TAIL return next->impl(rv, next, cycle, PC);
}

/* The all-ALU IDEA round suffix (trace_idea_alu_suffix).  The following BEQ
 * stays generic for branch learning. */
static PRESERVE_NONE RVOP_ALIGN bool do_idea_alu_suffix_trace(
    riscv_t *rv,
    const rv_insn_t *ir,
    uint64_t cycle,
    uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);

    uint32_t a4 = rv->X[rv_reg_a4] * rv->X[rv_reg_a5];
    uint32_t a5 = (a4 << 16) >> 16;
    a4 >>= 16;
    uint32_t t6 = a5 - a4;
    a5 = (a5 < a4) + t6;
    a5 = (a5 << 16) >> 16;
    uint32_t a3 = (rv->X[rv_reg_a3] + a5) << 16;
    a3 >>= 16;
    rv->X[rv_reg_t0] = rv->X[rv_reg_t1] ^ a5;
    rv->X[rv_reg_a4] = rv->X[rv_reg_a6] ^ a5;
    rv->X[rv_reg_t5] ^= a3;
    rv->X[rv_reg_t6] = rv->X[rv_reg_a7] ^ a3;
    rv->X[rv_reg_a3] = a3;
    rv->X[rv_reg_a5] = a5;
    cycle += 16;
    PC += 16 * 4;
    const rv_insn_t *terminal = ir + 1;
    MUST_TAIL return terminal->impl(rv, terminal, cycle, PC);
}

#endif

/* Helper for fused instruction tail: continue to next or stop.
 * Matches RVOP macro signal handling and block map clearing logic.
 * Note: RVOP returns without saving cycle/PC on signal handling, so we do too.
 *
 * PRESERVE_NONE matches the callers (do_fuse[1..13]) and the indirect
 * call target ir->impl, so musttail's calling-convention check passes
 * under clang-20+.
 */
static PRESERVE_NONE bool fuse_next_or_stop(riscv_t *rv,
                                            const rv_insn_t *ir,
                                            uint64_t cycle,
                                            uint32_t PC)
{
#if RV32_HAS(SYSTEM)
    if (need_handle_signal) {
        need_handle_signal = false;
        rv->block_interrupted = true;
        /* Match RVOP: return without saving cycle/PC. The signal handler
         * will determine the appropriate PC from rv->PC (unchanged).
         */
        return true;
    }
#endif
    RVOP_RETIRE_PENDING();
    if (unlikely(RVOP_NO_NEXT(ir))) {
        rv->csr_cycle = cycle;
        rv->PC = PC;
        return true;
    }
    const rv_insn_t *next = RVOP_NEXT_IR(ir);
#ifdef WASM_DEBUG_BLOCKS
    /* Validate: fused sequences shouldn't have branch targets.
     * fuse6 (ECALL) and fuse12 (ADDI+BNE) bypass this path entirely. */
    assert(!ir->branch_taken && !ir->branch_untaken &&
           "Fused ops in fuse_next_or_stop must be intra-block");
#endif
    /* Fused operations are atomic sequences detected during block analysis.
     * Use INTRA path (hard limit 15K) instead of INTER (soft limit 5K)
     * to avoid unnecessary yields within fused sequences. */
    RVOP_TAIL_INTRA_IMPL(rv, RVOP_NEXT_IMPL(ir), next, cycle, PC);
}

/* multiple LUI */
static PRESERVE_NONE RVOP_ALIGN bool do_fuse1(riscv_t *rv,
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
static PRESERVE_NONE RVOP_ALIGN bool do_fuse2(riscv_t *rv,
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
static PRESERVE_NONE RVOP_ALIGN bool do_fuse3(riscv_t *rv,
                                              const rv_insn_t *ir,
                                              uint64_t cycle,
                                              uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        cycle++;
        uint32_t addr = rv->X[fuse[i].rs1] + fuse[i].imm;
        RVOP_SYNC_PC_ACCESS(rv, PC); /* a page fault names this access */
        RV_EXC_MISALIGN_HANDLER(3, STORE, false, 1);
        uint32_t value = rv->X[fuse[i].rs2];
        MEM_WRITE_W(rv, addr, value);
        /* A fault whose handler resumed elsewhere ends the group here, as it
         * would end an unfused sequence; fuse_next_or_stop() returns.
         */
        if (MEM_ACCESS_FAULTED())
            break;
#if RV32_HAS(ARCH_TEST)
        check_tohost_write(rv, addr, value);
#endif
        PC += 4;
#if RV32_HAS(SYSTEM)
        if (rv->block_interrupted)
            break;
#endif
    }
    return fuse_next_or_stop(rv, ir, cycle, PC);
}

/* multiple LW */
static PRESERVE_NONE RVOP_ALIGN bool do_fuse4(riscv_t *rv,
                                              const rv_insn_t *ir,
                                              uint64_t cycle,
                                              uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        cycle++;
        uint32_t addr = rv->X[fuse[i].rs1] + fuse[i].imm;
        RVOP_SYNC_PC_ACCESS(rv, PC); /* a page fault names this access */
        RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
        const uint32_t value = MEM_READ_W(rv, addr);
        /* Commit nothing past a faulting member: later members may overwrite
         * registers that the redirected handler or a retry still reads.
         */
        if (MEM_ACCESS_FAULTED())
            break;
        if (fuse[i].rd)
            rv->X[fuse[i].rd] = value;
        PC += 4;
#if RV32_HAS(SYSTEM)
        if (rv->block_interrupted)
            break;
#endif
    }
    return fuse_next_or_stop(rv, ir, cycle, PC);
}

#if RV32_HAS_TRACE_FUSION && RV32_HAS(MOP_FUSION)
static PRESERVE_NONE RVOP_ALIGN bool do_fuse_lhu(riscv_t *rv,
                                                 const rv_insn_t *ir,
                                                 uint64_t cycle,
                                                 uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        cycle++;
        uint32_t addr = rv->X[fuse[i].rs1] + fuse[i].imm;
        RV_EXC_MISALIGN_HANDLER(1, LOAD, false, 1);
        const uint32_t value = MEM_READ_S(rv, addr);
        if (fuse[i].rd)
            rv->X[fuse[i].rd] = value;
        PC += 4;
    }
    return fuse_next_or_stop(rv, ir, cycle, PC);
}

static PRESERVE_NONE RVOP_ALIGN bool do_fuse_sh(riscv_t *rv,
                                                const rv_insn_t *ir,
                                                uint64_t cycle,
                                                uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    opcode_fuse_t *fuse = ir->fuse;
    for (int i = 0; i < ir->imm2; i++) {
        cycle++;
        uint32_t addr = rv->X[fuse[i].rs1] + fuse[i].imm;
        RV_EXC_MISALIGN_HANDLER(1, STORE, false, 1);
        uint32_t value = rv->X[fuse[i].rs2];
        MEM_WRITE_S(rv, addr, value);
#if RV32_HAS(ARCH_TEST)
        check_tohost_write(rv, addr, value);
#endif
        PC += 4;
    }
    return fuse_next_or_stop(rv, ir, cycle, PC);
}

#endif

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
static PRESERVE_NONE RVOP_ALIGN bool do_fuse5(riscv_t *rv,
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
static PRESERVE_NONE RVOP_ALIGN bool do_fuse6(riscv_t *rv,
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
static PRESERVE_NONE RVOP_ALIGN bool do_fuse6(riscv_t *rv UNUSED,
                                              const rv_insn_t *ir UNUSED,
                                              uint64_t cycle UNUSED,
                                              uint32_t PC UNUSED)
{
    assert(!"fuse6 should not be called in RV32E mode");
    return false;
}
#endif

/* fused multiple ADDI */
static PRESERVE_NONE RVOP_ALIGN bool do_fuse7(riscv_t *rv,
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
static PRESERVE_NONE RVOP_ALIGN bool do_fuse8(riscv_t *rv,
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
static PRESERVE_NONE RVOP_ALIGN bool do_fuse9(riscv_t *rv,
                                              const rv_insn_t *ir,
                                              uint64_t cycle,
                                              uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    cycle++;
    /* Write LUI result to rd - required when rd != LW destination.
     * LUI completes before LW, so this write happens even if LW faults.
     */
    rv->X[ir->rd] = ir->imm;
    PC += 4;
    RVOP_SYNC_PC_ACCESS(rv, PC); /* a page fault names the LW or SW */
    cycle++;
    /* Cast to uint32_t to avoid signed overflow UB */
    uint32_t addr = (uint32_t) ir->imm + (uint32_t) ir->imm2;
    RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
    MEM_LOAD_X(ir->rs2, MEM_READ_W(rv, addr));
    PC += 4;
    return fuse_next_or_stop(rv, ir, cycle, PC);
}

/* fused LUI + SW: lui rd, imm20; sw rs2, imm12(rd)
 * Common pattern for absolute/PC-relative stores.
 * ir->imm = lui immediate (already shifted << 12)
 * ir->imm2 = sw offset (sign-extended 12-bit)
 * ir->rd = lui destination (used as base, dead after)
 * ir->rs1 = sw source register (data to store)
 */
static PRESERVE_NONE RVOP_ALIGN bool do_fuse10(riscv_t *rv,
                                               const rv_insn_t *ir,
                                               uint64_t cycle,
                                               uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    cycle++;
    /* Write LUI result to rd - SW doesn't write registers, so rd may be
     * used later. LUI completes before SW, so this write happens even if
     * SW faults.
     */
    rv->X[ir->rd] = ir->imm;
    PC += 4;
    RVOP_SYNC_PC_ACCESS(rv, PC); /* a page fault names the LW or SW */
    cycle++;
    /* Cast to uint32_t to avoid signed overflow UB */
    uint32_t addr = (uint32_t) ir->imm + (uint32_t) ir->imm2;
    RV_EXC_MISALIGN_HANDLER(3, STORE, false, 1);
    uint32_t value = rv->X[ir->rs1];
    MEM_WRITE_W(rv, addr, value);
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, value);
#endif
    PC += 4;
    return fuse_next_or_stop(rv, ir, cycle, PC);
}

/* fused LW + ADDI (post-increment): lw rd, 0(rs1); addi rs1, rs1, step
 * Common loop pattern for pointer walking (memcpy, string ops).
 * ir->rd = load destination
 * ir->rs1 = base register (also incremented)
 * ir->imm = load offset
 * ir->imm2 = increment step
 */
static PRESERVE_NONE RVOP_ALIGN bool do_fuse11(riscv_t *rv,
                                               const rv_insn_t *ir,
                                               uint64_t cycle,
                                               uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    cycle++;
    uint32_t addr = rv->X[ir->rs1] + ir->imm;
    RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
    const uint32_t value = MEM_READ_W(rv, addr);
    PC += 4;
#if RV32_HAS(SYSTEM)
    if (rv->block_interrupted) {
        if (!MEM_ACCESS_FAULTED() && ir->rd)
            rv->X[ir->rd] = value;
        return fuse_next_or_stop(rv, ir, cycle, PC);
    }
#endif
    cycle++;
    /* Commit the load and the increment only if the load completed. The
     * guest handler runs inside the access, so trap_cnt is back to its
     * previous value by now; a handler that resumed elsewhere is signaled
     * through MEM_ACCESS_FAULTED(). User-mode RAM accesses never fault.
     */
    if (!MEM_ACCESS_FAULTED()) {
        if (ir->rd)
            rv->X[ir->rd] = value;
        rv->X[ir->rs1] = rv->X[ir->rs1] + ir->imm2;
    }
    PC += 4;
    return fuse_next_or_stop(rv, ir, cycle, PC);
}

/* fused ADDI + BNE: addi rd, rs1, imm; bne rd, x0, offset
 * Common loop counter pattern (countdown loops).
 * ir->rd = counter register (written by addi, tested by bne)
 * ir->rs1 = source register for addi
 * ir->imm = addi immediate (usually -1 for countdown)
 * ir->imm2 = branch offset
 */
static PRESERVE_NONE RVOP_ALIGN bool do_fuse12(riscv_t *rv,
                                               const rv_insn_t *ir,
                                               uint64_t cycle,
                                               uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    cycle += 2;
    rv->X[ir->rd] = rv->X[ir->rs1] + ir->imm;

    struct rv_insn *target;
    if (rv->X[ir->rd] != 0) {
        is_branch_taken = true;
        PC += 4 + ir->imm2; /* ADDI len + branch offset */
        target = ir->branch_taken;
    } else {
        is_branch_taken = false;
        PC += 8; /* Skip both ADDI and BNE */
        target = ir->branch_untaken;
    }
    if (target) {
#if RV32_HAS(JIT)
        /* Probe as BRANCH_FUNC does, so T1 sees countdown loops. */
        RVOP_PROBE_TARGET(target, PC, fuse12_exit);
#endif
#if RV32_HAS(SYSTEM)
        if (!rv->trap_cnt)
#endif
            RVOP_CHAIN_TAIL(rv, target, cycle, PC);
    }
#if RV32_HAS(JIT)
fuse12_exit:
#endif
    rv->csr_cycle = cycle;
    rv->PC = PC;
    return true;
}

/* fused SW + ADDI (post-increment store)
 *
 * The store must observe both source registers before the base register is
 * incremented. This is especially important for "sw rs1, off(rs1)". This fusion
 * is enabled only for the direct-RAM execution path: SYSTEM mode retains its
 * per-instruction MMU and MMIO fault boundaries.
 */
static PRESERVE_NONE RVOP_ALIGN bool do_fuse13(riscv_t *rv,
                                               const rv_insn_t *ir,
                                               uint64_t cycle,
                                               uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    cycle++;
    const uint32_t addr = rv->X[ir->rs1] + ir->imm;
    const uint32_t value = rv->X[ir->rs2];
    RV_EXC_MISALIGN_HANDLER(3, STORE, false, 1);
    MEM_WRITE_W(rv, addr, value);
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, value);
#endif
    /* The ADDI retires only after a successfully completed store. */
    cycle++;
    rv->X[ir->rs1] += ir->imm2;
    PC += 8;
    return fuse_next_or_stop(rv, ir, cycle, PC);
}

#if RV32_HAS_TRACE_FUSION && RV32_HAS(MOP_FUSION)
/* lbu; addi; addi; sb; bne is the inner byte-copy loop used by libc and
 * String Sort.  Keep the original instruction records in fuse[] so every
 * register alias observes the same sequential behavior as the five handlers.
 */
static PRESERVE_NONE RVOP_ALIGN bool do_byte_copy_loop(riscv_t *rv,
                                                       const rv_insn_t *ir,
                                                       uint64_t cycle,
                                                       uint32_t PC)
{
    const opcode_fuse_t *fuse = ir->fuse;
    const bool forward_branch_lhs = (ir->imm2 & (1 << 8)) != 0;
    RVOP_SYNC_PC(rv, PC);

    uint32_t addr = rv->X[fuse[0].rs1] + fuse[0].imm;
    rv->X[fuse[0].rd] = MEM_READ_B(rv, addr);
    rv->X[fuse[1].rd] = (uint32_t) rv->X[fuse[1].rs1] + (uint32_t) fuse[1].imm;
    uint32_t branch_lhs;
    if (forward_branch_lhs)
        branch_lhs = rv->X[fuse[1].rd];
    rv->X[fuse[2].rd] = (uint32_t) rv->X[fuse[2].rs1] + (uint32_t) fuse[2].imm;

    addr = rv->X[fuse[3].rs1] + fuse[3].imm;
    uint32_t value = rv->X[fuse[3].rs2];
    MEM_WRITE_B(rv, addr, value);
#if RV32_HAS(ARCH_TEST)
    check_tohost_write(rv, addr, value);
#endif
    cycle += 5;

    struct rv_insn *target;
    if ((forward_branch_lhs ? branch_lhs : rv->X[fuse[4].rs1]) ==
        rv->X[fuse[4].rs2]) {
        is_branch_taken = false;
        PC += 20;
        target = ir->branch_untaken;
    } else {
        is_branch_taken = true;
        PC += 16 + fuse[4].imm;
#if !RV32_HAS(EXT_C)
        /* Like BRANCH_FUNC, report the branch instruction as mtval. */
        RV_EXC_MISALIGN_HANDLER(PC - fuse[4].imm, INSN, false, 0);
#endif
        target = ir->branch_taken;
    }

    if (target && likely(cycle < rv->branch_chain_cycle_target)) {
        last_pc = PC;
        /* The common byte-copy backedge returns to this fused block head.
         * Keep that learned self edge as a direct must-tail call instead of
         * loading the same handler through target->impl on every byte. */
        if (target == ir) MUST_TAIL
            return do_byte_copy_loop(rv, ir, cycle, PC);
        MUST_TAIL return target->impl(rv, target, cycle, PC);
    }

    rv->csr_cycle = cycle;
    rv->PC = PC;
    return true;
}

#if !RV32_HAS(RV32E)
/* Exact newlib strlen word scan.  It owns the word-load fault boundary and
 * terminal backedge, while a structural matcher prevents guest-specific use. */
static PRESERVE_NONE RVOP_ALIGN bool do_strlen_word_trace(riscv_t *rv,
                                                          const rv_insn_t *ir,
                                                          uint64_t cycle,
                                                          uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    cycle++;
    uint32_t addr = rv->X[rv_reg_a4];
    RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
    rv->X[rv_reg_a2] = MEM_READ_W(rv, addr);
    rv->X[rv_reg_a4] += 4;
    rv->X[rv_reg_a5] = rv->X[rv_reg_a2] & rv->X[rv_reg_a3];
    rv->X[rv_reg_a5] += rv->X[rv_reg_a3];
    rv->X[rv_reg_a5] |= rv->X[rv_reg_a2];
    rv->X[rv_reg_a5] |= rv->X[rv_reg_a3];
    cycle += 6;

    struct rv_insn *target;
    if (rv->X[rv_reg_a5] == rv->X[rv_reg_a1]) {
        is_branch_taken = true;
        target = ir->branch_taken;
    } else {
        is_branch_taken = false;
        PC += 28;
        target = ir->branch_untaken;
    }
    if (target && likely(cycle < rv->branch_chain_cycle_target)) {
        last_pc = PC;
        if (target == ir) MUST_TAIL
            return do_strlen_word_trace(rv, ir, cycle, PC);
        MUST_TAIL return target->impl(rv, target, cycle, PC);
    }
    rv->csr_cycle = cycle;
    rv->PC = PC;
    return true;
}
#endif

/* Leave a trace through its terminal branch: continue into a learned edge
 * while the cycle budget lasts, otherwise publish state for rv_step(). */
#define TRACE_BRANCH_EXIT(target)                                        \
    do {                                                                 \
        if ((target) && likely(cycle < rv->branch_chain_cycle_target)) { \
            last_pc = PC;                                                \
            MUST_TAIL return (target)->impl(rv, target, cycle, PC);      \
        }                                                                \
        rv->csr_cycle = cycle;                                           \
        rv->PC = PC;                                                     \
        return true;                                                     \
    } while (0)

/* Reverse a linked list in place, one node per iteration:
 *   mv t, a; lw a, off(t); sw p, off(t); mv p, t; bnez a, loop
 * Any of the five may be compressed. The matcher packs the byte offsets of
 * the load and branch, the branch length and the load's compression into
 * imm2; imm holds the link offset. Three distinct working registers let the
 * loop keep the node and its predecessor in locals, publishing X[] only at a
 * fault or exit. The store shares the load's address and alignment check,
 * so only the load can raise a misaligned-access trap. Guest stores may
 * alias any host memory, so everything the loop reads from rv or ir is
 * loaded into locals first. On ELF hosts, a separate section keeps
 * established hot handlers in place.
 */
#define PREV_LOAD_OFF(layout) ((layout) & 0xff)
#define PREV_BRANCH_OFF(layout) (((layout) >> 8) & 0xff)
#define PREV_BRANCH_LEN(layout) (((layout) >> 16) & 0xff)
#define PREV_LOAD_COMPRESSED(layout) (((layout) >> 24) & 1)

#if defined(__ELF__)
__attribute__((section(".text.pointer_reverse")))
#endif
static PRESERVE_NONE bool
do_pointer_reverse(riscv_t *rv,
                   const rv_insn_t *ir,
                   uint64_t cycle,
                   uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);
    const uint32_t layout = ir->imm2, link = ir->imm, start = PC;
    const uint32_t branch_pc = start + PREV_BRANCH_OFF(layout);
    const uint32_t taken_pc = branch_pc + ir->fuse[4].imm;
    const uint8_t rd = ir->rd, rs1 = ir->rs1, rs2 = ir->rs2;
    const bool check_align = !rv->allow_misalign;
    struct rv_insn *const taken = ir->branch_taken;
    /* Only a self-loop iterates here; any other target ends the first pass,
     * and TRACE_BRANCH_EXIT() then follows it within the real budget.
     */
    const uint64_t budget = taken == ir ? rv->branch_chain_cycle_target : 0;
#if HAVE_MMAP
    uint8_t *const ram = PRIV(rv)->mem->mem_base;
#endif
#define PREV_PUBLISH(copy, list, prev) \
    do {                               \
        rv->X[rd] = (copy);            \
        rv->X[rs1] = (list);           \
        rv->X[rs2] = (prev);           \
    } while (0)
    uint32_t node = rv->X[rs1], previous = rv->X[rs2], next;
    struct rv_insn *target;
    bool looped = false;
    for (;;) {
        /* The copy retires, then the load either traps or completes. */
        cycle += 2;
        const uint32_t addr = node + link;
        if (check_align && unlikely(addr & 3)) {
            PREV_PUBLISH(node, node, previous);
            PC = start + PREV_LOAD_OFF(layout);
            RV_EXC_MISALIGN_HANDLER(3, LOAD, PREV_LOAD_COMPRESSED(layout), 1);
        }
#if HAVE_MMAP
        memcpy(&next, ram + addr, sizeof(next));
        memcpy(ram + addr, &previous, sizeof(previous));
#else
        next = MEM_READ_W(rv, addr);
        MEM_WRITE_W(rv, addr, previous);
#endif
#if RV32_HAS(ARCH_TEST)
        check_tohost_write(rv, addr, previous);
#endif
        /* The store, the second copy and the branch. */
        cycle += 3;
        previous = node;
        if (!next) {
            is_branch_taken = false;
            PC = branch_pc + PREV_BRANCH_LEN(layout);
            target = ir->branch_untaken;
            break;
        }
#if !RV32_HAS(EXT_C)
        if (unlikely(insn_is_misaligned(taken_pc))) {
            PREV_PUBLISH(previous, next, previous);
            is_branch_taken = true;
            PC = taken_pc;
            const uint32_t pc = branch_pc;
            RV_EXC_MISALIGN_HANDLER(pc, INSN, false, 0);
        }
#endif
        if (cycle >= budget) {
            is_branch_taken = true;
            PC = taken_pc;
            target = taken;
            break;
        }
        looped = true;
        node = next;
    }
    if (looped)
        last_pc = taken_pc;
    PREV_PUBLISH(previous, next, previous);
#undef PREV_PUBLISH
    TRACE_BRANCH_EXIT(target);
}

/* GCC emits this exact register graph in NumSift's hot child-index path.
 * Restrict the matcher to every operand and immediate below, so the handler
 * can use fixed register slots instead of decoding a generic fusion payload.
 * The operations are all non-faulting and their destinations are distinct
 * from the live inputs, but keep their guest-program order explicit. */
#if !RV32_HAS(RV32E)
static PRESERVE_NONE RVOP_ALIGN bool do_numsift_index_trace(riscv_t *rv,
                                                            const rv_insn_t *ir,
                                                            uint64_t cycle,
                                                            uint32_t PC)
{
    const opcode_fuse_t *f = ir->fuse;
    RVOP_SYNC_PC(rv, PC);
    uint32_t index = rv->X[rv_reg_a5];
    uint32_t base = rv->X[rv_reg_a0];

    rv->X[rv_reg_a6] = index + 1;
    rv->X[rv_reg_a4] = rv->X[rv_reg_a1] << 3;
    rv->X[rv_reg_a3] = rv->X[rv_reg_a6] << 2;
    rv->X[rv_reg_a3] = base + rv->X[rv_reg_a3];
    rv->X[rv_reg_a4] = base + rv->X[rv_reg_a4];
    rv->X[rv_reg_a7] = index << 2;
    cycle += 7;

    struct rv_insn *target;
    if (index < rv->X[rv_reg_a2]) {
        is_branch_taken = false;
        PC += 28;
        target = ir->branch_untaken;
    } else {
        is_branch_taken = true;
        PC += 24 + f[6].imm;
#if !RV32_HAS(EXT_C)
        /* Like BRANCH_FUNC, report the branch instruction as mtval. */
        RV_EXC_MISALIGN_HANDLER(PC - f[6].imm, INSN, false, 0);
#endif
        target = ir->branch_taken;
    }
    TRACE_BRANCH_EXIT(target);
}

/* Primes' sieve probe has an exact 16-instruction register graph.  It keeps
 * the two faultable loads ordered, then lets the original BNE own its learned
 * edges.  This is deliberately not a generic word-load or branch rewrite. */
static PRESERVE_NONE RVOP_ALIGN bool do_primes_probe_trace(riscv_t *rv,
                                                           const rv_insn_t *ir,
                                                           uint64_t cycle,
                                                           uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);

    rv->X[rv_reg_a5] <<= 3;
    rv->X[rv_reg_a5] = rv->X[rv_reg_a0] + rv->X[rv_reg_a5];

    cycle += 3;
    PC += 8;
    uint32_t addr = rv->X[rv_reg_a5];
    RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
    rv->X[rv_reg_a7] = MEM_READ_W(rv, addr);

    cycle++;
    PC += 4;
    addr = rv->X[rv_reg_a5] + 4;
    RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);
    rv->X[rv_reg_a6] = MEM_READ_W(rv, addr);

    rv->X[rv_reg_t0] = rv->X[rv_reg_t1] << (rv->X[rv_reg_a3] & 0x1f);
    rv->X[rv_reg_s0] = (uint32_t) ((int32_t) rv->X[rv_reg_t0] >> 31);
    rv->X[rv_reg_t6] = rv->X[rv_reg_a7] & rv->X[rv_reg_t0];
    rv->X[rv_reg_s1] = rv->X[rv_reg_a6] & rv->X[rv_reg_s0];
    rv->X[rv_reg_t2] = rv->X[rv_reg_a3] + rv->X[rv_reg_a1];
    rv->X[rv_reg_a3] = rv->X[rv_reg_t2] < rv->X[rv_reg_a3];
    rv->X[rv_reg_a4] += rv->X[rv_reg_a2];
    rv->X[rv_reg_t6] |= rv->X[rv_reg_s1];
    rv->X[rv_reg_a4] += rv->X[rv_reg_a3];
    rv->X[rv_reg_a7] |= rv->X[rv_reg_t0];
    rv->X[rv_reg_a6] |= rv->X[rv_reg_s0];
    cycle += 12;
    PC += 48;

    struct rv_insn *target;
    if (rv->X[rv_reg_t6] != 0) {
        is_branch_taken = true;
        PC += 12;
        target = ir->branch_taken;
    } else {
        is_branch_taken = false;
        PC += 4;
        target = ir->branch_untaken;
    }
    TRACE_BRANCH_EXIT(target);
}
#endif

/* DoBitfieldIteration's set-bit and invert-bit inner loops have this exact
 * register graph and differ only in the seventh instruction (or versus xor).
 * Keep the match deliberately narrow: unlike the rejected generic RMW loop
 * executor, these handlers cannot capture a merely similar loop.  Each
 * operation keeps its own handler so the set path stays branch-free.
 *
 * The load is instruction four, so a fault retires its preceding three
 * instructions and reports the load's PC and cycle exactly; the store is
 * instruction eight and likewise retains its own fault PC.
 */
#if !RV32_HAS(RV32E)
#define BITFIELD_BIT_TRACE(name, op)                                     \
    static PRESERVE_NONE RVOP_ALIGN bool do_bitfield_##name##_bit_trace( \
        riscv_t *rv, const rv_insn_t *ir, uint64_t cycle, uint32_t PC)   \
    {                                                                    \
        RVOP_SYNC_PC(rv, PC);                                            \
                                                                         \
        const uint32_t bit = rv->X[rv_reg_a5];                           \
        rv->X[rv_reg_a4] = bit >> 5;                                     \
        rv->X[rv_reg_a4] <<= 2;                                          \
        rv->X[rv_reg_a4] += rv->X[rv_reg_s0];                            \
                                                                         \
        cycle += 4;                                                      \
        PC += 12;                                                        \
        uint32_t addr = rv->X[rv_reg_a4];                                \
        RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1);                      \
        rv->X[rv_reg_a3] = MEM_READ_W(rv, addr);                         \
                                                                         \
        rv->X[rv_reg_a2] = rv->X[rv_reg_a6] << (bit & 0x1f);             \
        rv->X[rv_reg_a5] = bit + 1;                                      \
        rv->X[rv_reg_a3] op rv->X[rv_reg_a2];                            \
                                                                         \
        cycle += 4;                                                      \
        PC += 16;                                                        \
        addr = rv->X[rv_reg_a4];                                         \
        RV_EXC_MISALIGN_HANDLER(3, STORE, false, 1);                     \
        const uint32_t value = rv->X[rv_reg_a3];                         \
        MEM_WRITE_W(rv, addr, value);                                    \
        IIF(RV32_HAS(ARCH_TEST))(check_tohost_write(rv, addr, value);, ) \
                                                                         \
            cycle++;                                                     \
        struct rv_insn *target;                                          \
        if (rv->X[rv_reg_a5] != rv->X[rv_reg_t4]) {                      \
            is_branch_taken = true;                                      \
            PC += 4 - 32;                                                \
            target = ir->branch_taken;                                   \
        } else {                                                         \
            is_branch_taken = false;                                     \
            PC += 8;                                                     \
            target = ir->branch_untaken;                                 \
        }                                                                \
        TRACE_BRANCH_EXIT(target);                                       \
    }

BITFIELD_BIT_TRACE(set, |=)
BITFIELD_BIT_TRACE(invert, ^=)
#undef BITFIELD_BIT_TRACE
#endif
#undef TRACE_BRANCH_EXIT

/* EmFloat shifts a four-limb mantissa through this ordered halfword loop.
 * Keep every operation in saved-record order: the trace is deliberately
 * alias-safe, and the two faultable memory accesses retain their own PCs. */
static PRESERVE_NONE RVOP_ALIGN bool do_emfloat_halfword_shift_trace(
    riscv_t *rv,
    const rv_insn_t *ir,
    uint64_t cycle,
    uint32_t PC)
{
    const opcode_fuse_t *f = ir->fuse;
    bool self_chained = false;
    RVOP_SYNC_PC(rv, PC);

    for (;;) {
        cycle++;
        uint32_t addr = rv->X[f[0].rs1] + f[0].imm;
        RV_EXC_MISALIGN_HANDLER(1, LOAD, false, 1);
        rv->X[f[0].rd] = MEM_READ_S(rv, addr);
        rv->X[f[1].rd] = rv->X[f[1].rs1] << (f[1].imm & 0x1f);
        rv->X[f[2].rd] = rv->X[f[2].rs1] + f[2].imm;
        rv->X[f[3].rd] = rv->X[f[3].rs1] >> (f[3].imm & 0x1f);
        rv->X[f[4].rd] = rv->X[f[4].rs1] | rv->X[f[4].rs2];

        cycle += 5;
        PC += 5 * 4;
        addr = rv->X[f[5].rs1] + f[5].imm;
        RV_EXC_MISALIGN_HANDLER(1, STORE, false, 1);
        uint32_t value = rv->X[f[5].rs2];
        MEM_WRITE_S(rv, addr, value);
#if RV32_HAS(ARCH_TEST)
        check_tohost_write(rv, addr, value);
#endif
        rv->X[f[6].rd] = rv->X[f[6].rs1] & f[6].imm;
        cycle += 2;

        struct rv_insn *target;
        if (rv->X[f[7].rs1] == rv->X[f[7].rs2]) {
            is_branch_taken = false;
            PC += 12;
            target = ir->branch_untaken;
        } else {
            is_branch_taken = true;
            PC += 8 + f[7].imm;
#if !RV32_HAS(EXT_C)
            /* Like BRANCH_FUNC, report the branch instruction as mtval. */
            RV_EXC_MISALIGN_HANDLER(PC - f[7].imm, INSN, false, 0);
#endif
            target = ir->branch_taken;
        }

        if (target && likely(cycle < rv->branch_chain_cycle_target)) {
            /* The four-limb StickyShiftRightMant loop returns to this exact
             * trace.  Stay in-frame after its direct edge is learned, rather
             * than tail-entering the same handler for each limb. */
            if (is_branch_taken && target == ir) {
                if (!self_chained) {
                    last_pc = PC;
                    self_chained = true;
                }
                continue;
            }
            last_pc = PC;
            MUST_TAIL return target->impl(rv, target, cycle, PC);
        }
        rv->csr_cycle = cycle;
        rv->PC = PC;
        return true;
    }
}

/* GCC emits this ordered record copy for Dhrystone's Proc1.  It is matched
 * structurally (not by guest address), so the straight-line implementation is
 * valid for any guest with the same register and memory-access sequence. */
#if !RV32_HAS(RV32E)
static PRESERVE_NONE RVOP_ALIGN bool do_record_copy_trace(riscv_t *rv,
                                                          const rv_insn_t *ir,
                                                          uint64_t cycle,
                                                          uint32_t PC)
{
    RVOP_SYNC_PC(rv, PC);

#define RECORD_LOAD(reg, base, off)                 \
    do {                                            \
        cycle++;                                    \
        uint32_t addr = rv->X[(base)] + (off);      \
        RV_EXC_MISALIGN_HANDLER(3, LOAD, false, 1); \
        rv->X[(reg)] = MEM_READ_W(rv, addr);        \
        PC += 4;                                    \
    } while (0)
#define RECORD_STORE(base, off, reg)                                         \
    do {                                                                     \
        cycle++;                                                             \
        uint32_t addr = rv->X[(base)] + (off);                               \
        RV_EXC_MISALIGN_HANDLER(3, STORE, false, 1);                         \
        uint32_t value = rv->X[(reg)];                                       \
        MEM_WRITE_W(rv, addr, value);                                        \
        IIF(RV32_HAS(ARCH_TEST))(check_tohost_write(rv, addr, value);) PC += \
            4;                                                               \
    } while (0)

    RECORD_LOAD(rv_reg_a4, rv_reg_gp, -1880);
    RECORD_LOAD(rv_reg_a5, rv_reg_a0, 0);
    RECORD_LOAD(rv_reg_a2, rv_reg_a4, 40);
    RECORD_LOAD(rv_reg_t2, rv_reg_a4, 0);
    RECORD_LOAD(rv_reg_t0, rv_reg_a4, 4);
    RECORD_LOAD(rv_reg_t6, rv_reg_a4, 8);
    RECORD_LOAD(rv_reg_t5, rv_reg_a4, 12);
    RECORD_LOAD(rv_reg_t4, rv_reg_a4, 16);
    RECORD_LOAD(rv_reg_t3, rv_reg_a4, 20);
    RECORD_LOAD(rv_reg_t1, rv_reg_a4, 24);
    RECORD_LOAD(rv_reg_a7, rv_reg_a4, 28);
    RECORD_LOAD(rv_reg_a6, rv_reg_a4, 32);
    RECORD_LOAD(rv_reg_a1, rv_reg_a4, 36);

    RECORD_STORE(rv_reg_a5, 0, rv_reg_t2);
    RECORD_STORE(rv_reg_a5, 12, rv_reg_t5);
    RECORD_STORE(rv_reg_a5, 40, rv_reg_a2);
    RECORD_STORE(rv_reg_a5, 4, rv_reg_t0);
    RECORD_STORE(rv_reg_a5, 8, rv_reg_t6);
    RECORD_STORE(rv_reg_a5, 16, rv_reg_t4);
    RECORD_STORE(rv_reg_a5, 20, rv_reg_t3);
    RECORD_STORE(rv_reg_a5, 24, rv_reg_t1);
    RECORD_STORE(rv_reg_a5, 28, rv_reg_a7);
    RECORD_STORE(rv_reg_a5, 32, rv_reg_a6);
    RECORD_STORE(rv_reg_a5, 36, rv_reg_a1);

#undef RECORD_STORE
#undef RECORD_LOAD

    return fuse_next_or_stop(rv, ir, cycle, PC);
}

#endif

#endif

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

/* The handler for a decoded instruction. An integer load into x0 runs
 * do_load_x0(), so the ordinary load handlers can write rd unconditionally.
 */
static inline rv_insn_impl_t insn_handler(const rv_insn_t *ir)
{
    if (unlikely(ir->rd == rv_reg_zero) &&
        (ir->opcode == rv_insn_lb || ir->opcode == rv_insn_lh ||
         ir->opcode == rv_insn_lw || ir->opcode == rv_insn_lbu ||
         ir->opcode == rv_insn_lhu))
        return do_load_x0;
    return dispatch_table[ir->opcode];
}

#if RV32_HAS(T2C) && RV32_HAS(SYSTEM)
/* Execute one semantic helper without charging the native prefix twice.
 * System regions keep guest registers in rv->X, which the handler reads and
 * writes directly. The original handlers retain all AMO reservation, fault,
 * and CSR behavior.
 */
void t2c_run_outlined_insn(riscv_t *rv, uint32_t operation, uint32_t imm)
{
    rv_insn_t ir = {0};
    ir.opcode = operation & 0xffff;
    ir.rd = (operation >> 16) & 31;
    ir.rs1 = (operation >> 21) & 31;
    ir.rs2 = (operation >> 26) & 31;
    ir.imm = imm;
    ir.pc = rv->PC;
    assert(t2c_outlined_insn(&ir));
    insn_handler (&ir)(rv, &ir, rv->csr_cycle - 1, rv->PC);
}
#endif

#if RV32_HAS(JIT)
FORCE_INLINE bool insn_is_translatable(uint16_t opcode)
{
    switch (opcode) {
#define _(inst, can_branch, insn_len, translatable, reg_mask) \
    IIF(translatable)(case rv_insn_##inst:, )
        RV_INSN_LIST
#undef _
#if JIT_SYSTEM_HELPERS
    JIT_SYSTEM_AMO_CASES:
    JIT_SYSTEM_CSR_CASES:
#endif
        return true;
    }
    return false;
}
#endif

#if RV32_HAS(BLOCK_CHAINING)
FORCE_INLINE bool insn_is_unconditional_branch(uint16_t opcode)
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

FORCE_INLINE bool insn_is_direct_branch(uint16_t opcode)
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

FORCE_INLINE bool insn_is_indirect_branch(uint16_t opcode)
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

/* Release linked decoder storage before packing or cache registration. */
static void free_decoded_irs(riscv_t *rv, block_t *block)
{
    for (rv_insn_t *ir = block->ir_head, *next; ir; ir = next) {
        next = ir->next;
        if (ir->fuse)
            mpool_free(rv->fuse_mp, ir->fuse);
        free(ir->branch_table);
        mpool_free(rv->block_ir_mp, ir);
    }
    block->ir_head = block->ir_tail = NULL;
}

#if RV32_HAS(T2C) && RV32_HAS(SYSTEM)
/* Whether AOT code can lower ir. T1 calls into C for every AMO and CSR
 * instruction; AOT lowers counter reads and outlines only the forms below.
 */
static bool aot_can_lower_insn(const rv_insn_t *ir)
{
    if (t2c_counter_read(ir) || t2c_outlined_insn(ir))
        return true;
    switch (ir->opcode) {
#if JIT_SYSTEM_HELPERS
    JIT_SYSTEM_AMO_CASES:
    JIT_SYSTEM_CSR_CASES:
        return false;
#endif
    default:
        return insn_is_translatable(ir->opcode);
    }
}

static void block_end_fallthrough(block_t *block)
{
#if RV32_HAS(BLOCK_CHAINING)
    block->page_terminated = true;
#else
    /* T1 requires a terminal branch when no fallthrough marker is present. */
    block->translatable = false;
#endif
}
#endif

static bool block_translate(riscv_t *rv, block_t *block)
{
#if RV32_HAS(T2C) && RV32_HAS(SYSTEM)
    /* Collection splits blocks at instructions AOT cannot lower, so that a
     * lowerable prefix forms its own region. Replay binds regions by PC and
     * keeps T1's blocks whole.
     */
    const bool aot = PRIV(rv)->aot_collect;
#endif
retranslate:
#if RV32_HAS(T2C) && RV32_HAS(SYSTEM)
    if (aot)
        memset(block->aot_insn_starts, 0, sizeof(block->aot_insn_starts));
#endif
#if RV32_T1C_STATS_ENABLED
    block->stats_blocker = T1C_INTERPRETED_TRANSLATABLE;
#endif
#if RV32_HAS(SYSTEM)
    block->priv_mode = rv->priv_mode;
#if RV32_HAS(JIT)
    block->satp = rv->csr_satp;
#endif
#endif
    block->pc_start = block->pc_end = rv->PC;
#if RV32_HAS(BLOCK_CHAINING)
    block->page_terminated = false;
#endif

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
        uint32_t insn = fetch_instruction(rv, block->pc_end);

#if RV32_HAS(SYSTEM)
        if (!insn && need_retranslate) {
            free_decoded_irs(rv, block);
            block_reset(block);
            T1C_COUNT(fetch_retranslations, 1);
            need_retranslate = false;
            need_handle_signal = false;
            goto retranslate;
        }
#endif

#if RV32_HAS(SYSTEM)
        /* A synchronous fetch handler may have redirected execution. Never
         * execute or cache the prefix decoded before that redirect.
         */
        if (need_handle_signal)
            return false;
#endif

        /* If instruction fetch failed due to trap (page fault, etc.), break.
         * The caller checks rv->trap_cnt and invokes trap handler.
         * Note: insn==0 alone is ambiguous; we verify trap state explicitly.
         */
        if (!insn)
            break;

        /* decode the instruction */
        if (!rv_decode(ir, insn)) {
            if (prev_ir) {
#if RV32_HAS(BLOCK_CHAINING)
                block->page_terminated = true;
#endif
                break;
            }
            rv->compressed = is_compressed(insn);
            SET_CAUSE_AND_TVAL_THEN_TRAP(rv, ILLEGAL_INSN, insn);
#if RV32_HAS(SYSTEM)
            need_handle_signal = true;
#endif
            break;
        }
#if RV32_T1C_STATS_ENABLED
        ir->stats_owner = block;
#endif
#if RV32_HAS(T2C) && RV32_HAS(SYSTEM)
        /* An unsupported tail must not discard an otherwise native prefix.
         * Decode the boundary instruction on the next dispatcher entry.
         */
        const bool aot_boundary = aot && !aot_can_lower_insn(ir);
        if (aot_boundary && block->n_insn) {
            block_end_fallthrough(block);
            break;
        }
#endif
        ir->impl = insn_handler(ir);
        ir->pc = block->pc_end; /* compute the end of pc */
#if RV32_HAS(T2C) && RV32_HAS(SYSTEM)
        const unsigned halfword = (ir->pc - block->pc_start) / 2;
        if (aot && halfword < 2048)
            block->aot_insn_starts[halfword / 64] |= UINT64_C(1)
                                                     << (halfword % 64);
#endif
        block->pc_end += is_compressed(insn) ? 2 : 4;
        block->n_insn++;
        prev_ir = ir;
#if RV32_HAS(JIT)
        if (!insn_is_translatable(ir->opcode)) {
#if RV32_T1C_STATS_ENABLED
            rv_t1c_blockers_decoded[ir->opcode]++;
            if (block->stats_blocker >= N_RV_INSNS)
                block->stats_blocker = ir->opcode;
#endif
            block->translatable = false;
#if RV32_HAS(T2C) && !RV32_HAS(SYSTEM)
            if (!t2c_counter_read(ir))
                block->aot_translatable = false;
#endif
        }
#endif
#if RV32_HAS(T2C) && RV32_HAS(SYSTEM)
        if (!aot_can_lower_insn(ir))
            block->aot_translatable = false;
#endif
#if RV32_HAS(T2C) && RV32_HAS(SYSTEM)
        /* Isolate a nonbranch boundary too, so it cannot poison its suffix.
         * Control transfers retain their ordinary branch termination below.
         */
        if (aot_boundary && !insn_is_branch(ir->opcode)) {
            block_end_fallthrough(block);
            break;
        }
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

#if RV32_HAS(BLOCK_CHAINING) || RV32_HAS(SYSTEM)
        /* Keep block starts indexed by page. A single C-enabled instruction
         * may straddle into the next page; SFENCE.VMA also checks its preceding
         * page before retiring retained code.
         */
        {
            const uint32_t page_end =
                (block->pc_start & ~(RV_PG_SIZE - 1)) + RV_PG_SIZE;
            /* With C, leave the final halfword for a fresh block: its
             * instruction may need another page and enter a fetch handler.
             * This also applies without chaining, so a decoded prefix always
             * executes before a fetch can cross a permission boundary.
             */
            const uint32_t fetch_slack =
                RV32_HAS(SYSTEM) && RV32_HAS(EXT_C) ? 2 : 0;
            if (block->pc_end >= page_end - fetch_slack) {
#if RV32_HAS(BLOCK_CHAINING)
                block->page_terminated = true;
#endif
                break;
            }
        }
#endif

        ir = mpool_calloc(rv->block_ir_mp);
        if (unlikely(!ir))
            return false;
    }

    /* If no instructions were successfully decoded (e.g., first instruction
     * was illegal), free the allocated IR and return failure.
     */
    if (unlikely(!prev_ir)) {
        mpool_free(rv->block_ir_mp, block->ir_head);
        block->ir_head = NULL;
        return false;
    }

    /* Free orphaned IR that was allocated at end of previous iteration but not
     * used due to early break (fetch fail, decode fail, etc.).
     * This IR was linked at prev_ir->next but never became prev_ir.
     */
    if (prev_ir->next)
        mpool_free(rv->block_ir_mp, prev_ir->next);

    block->ir_tail = prev_ir;
    block->ir_tail->next = NULL;
#if RV32_HAS(JIT) && !RV32_HAS(BLOCK_CHAINING)
    /* Generated code emits a fallthrough exit only for page-terminated
     * blocks, which need chaining. A block that stops before a branch (a page
     * boundary, an undecodable or unfetchable successor) stays interpreted.
     */
    if (!insn_is_branch(block->ir_tail->opcode))
        block->translatable = false;
#endif
    /* Set cycle cost before macro-op fusion. This intentionally counts
     * original instructions for accurate timing - fused operations still
     * represent the same logical work as unfused sequences.
     */
    block->cycle_cost = block->n_insn;
    return true;
}

#if RV32_HAS(MOP_FUSION)
static inline void remove_next_nth_ir(const riscv_t *rv,
                                      rv_insn_t *ir,
                                      block_t *block,
                                      uint8_t n)
{
    /* Block chaining records the successor blocks on the tail instruction.
     * Lazy fusion runs on blocks that are already live and possibly chained,
     * so carry those links over when the removed run reaches the tail.
     * Otherwise the chain would be lost with the freed instruction, and any
     * edge still naming this block as its source would resolve to a tail that
     * no longer holds the link.
     */
    rv_insn_t *chain_taken = block->ir_tail->branch_taken;
    rv_insn_t *chain_untaken = block->ir_tail->branch_untaken;

    for (uint8_t i = 0; i < n; i++) {
        rv_insn_t *next = ir->next;
        ir->next = ir->next->next;
        mpool_free(rv->block_ir_mp, next);
    }
    if (!ir->next) {
        block->ir_tail = ir;
        ir->branch_taken = chain_taken;
        ir->branch_untaken = chain_untaken;
    }
    block->n_insn -= n;
}

/* Count consecutive instructions with same opcode */
static inline int count_consecutive_insn(rv_insn_t *ir, uint16_t opcode)
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
 * Returns true on success, false on allocation failure (graceful degradation).
 * Uses pooled allocation for fuse arrays up to FUSE_MAX_ENTRIES.
 */
static inline bool try_fuse_sequence(riscv_t *rv,
                                     block_t *block,
                                     rv_insn_t *ir,
                                     int count,
                                     uint16_t fuse_opcode)
{
    if (count <= 1)
        return false;

    /* Reject sequences exceeding pool slot size - rare case with diminishing
     * returns. This also handles the overflow check implicitly.
     */
    if (unlikely(count > FUSE_MAX_ENTRIES))
        return false;

    opcode_fuse_t *fuse_data = mpool_alloc(rv->fuse_mp);
    if (unlikely(!fuse_data))
        return false;

    ir->fuse = fuse_data;
    /* Copy original instruction BEFORE changing opcode (preserves original
     * opcode in fuse[0] for handlers like fuse_shift_exec that need it) */
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
#if RV32_HAS_TRACE_FUSION
/* Fuse @count records from @ir through the branch @terminal into one record
 * run by @impl, which takes over the branch's learned edges.
 */
static bool fuse_with_terminal(riscv_t *rv,
                               block_t *block,
                               rv_insn_t *ir,
                               int count,
                               const rv_insn_t *terminal,
                               rv_insn_impl_t impl)
{
    rv_insn_t *taken = terminal->branch_taken;
    rv_insn_t *untaken = terminal->branch_untaken;
    if (!try_fuse_sequence(rv, block, ir, count, rv_insn_fuse13))
        return false;
    ir->branch_taken = taken;
    ir->branch_untaken = untaken;
    ir->impl = impl;
    return true;
}

/* Fuse the records of an exactly matched trace into one record run by impl.
 * When the terminal branch is fused too, the fused record takes over its
 * learned edges; otherwise the branch stays the next, generic record.
 */
FORCE_INLINE bool fuse_trace(riscv_t *rv,
                             block_t *block,
                             rv_insn_t *ir,
                             enum trace_match_id id,
                             bool fuse_terminal,
                             rv_insn_impl_t impl)
{
    const trace_match_spec_t *spec = &trace_match_specs[id];
    const rv_insn_t *terminal;
    if (!trace_match(ir, spec, &terminal))
        return false;
    if (fuse_terminal)
        return fuse_with_terminal(rv, block, ir, spec->count, terminal, impl);
    if (!try_fuse_sequence(rv, block, ir, spec->count - 1, rv_insn_fuse13))
        return false;
    ir->impl = impl;
    return true;
}
#endif

#if RV32_HAS_TRACE_FUSION
/* Decode a register copy, "addi rd, rs, 0" or "c.mv rd, rs". */
static inline bool insn_is_copy(const rv_insn_t *ir, uint8_t *rd, uint8_t *rs)
{
    if (ir->opcode == rv_insn_addi && !ir->imm) {
        *rd = ir->rd;
        *rs = ir->rs1;
        return true;
    }
#if RV32_HAS(EXT_C)
    if (ir->opcode == rv_insn_cmv) {
        *rd = ir->rd;
        *rs = ir->rs2;
        return true;
    }
#endif
    return false;
}

/* Either width of an instruction: OPCODE_IS(op, lw, clw). */
#if RV32_HAS(EXT_C)
#define OPCODE_IS(op, base, compressed) \
    ((op) == rv_insn_##base || (op) == rv_insn_##compressed)
#else
#define OPCODE_IS(op, base, compressed) ((op) == rv_insn_##base)
#endif

/* Match do_pointer_reverse()'s five instructions, each in either width. */
static bool fuse_pointer_reverse(riscv_t *rv, block_t *block, rv_insn_t *ir)
{
    uint8_t node, list, prev, from;
    if (!insn_is_copy(ir, &node, &list))
        return false;
    rv_insn_t *load = ir->next;
    rv_insn_t *store = load ? load->next : NULL;
    rv_insn_t *copy = store ? store->next : NULL;
    rv_insn_t *branch = copy ? copy->next : NULL;
    if (!branch || !insn_is_copy(copy, &prev, &from) ||
        !OPCODE_IS(load->opcode, lw, clw) ||
        !OPCODE_IS(store->opcode, sw, csw) ||
        !OPCODE_IS(branch->opcode, bne, cbnez) || branch->rs2 != rv_reg_zero)
        return false;
    if (!node || !list || !prev || node == list || prev == node ||
        prev == list || load->rs1 != node || load->rd != list ||
        store->rs1 != node || store->rs2 != prev || store->imm != load->imm ||
        from != node || branch->rs1 != list)
        return false;

    /* The recorded PCs give each instruction's offset, whatever its width. */
    const uint32_t load_off = load->pc - ir->pc;
    const uint32_t branch_off = branch->pc - ir->pc;
    const uint32_t branch_len = branch->opcode == rv_insn_bne ? 4 : 2;
    const uint32_t load_compressed = store->pc - load->pc == 2;
    const int32_t link = load->imm;
    if (!fuse_with_terminal(rv, block, ir, 5, branch, do_pointer_reverse))
        return false;
    ir->rd = node;
    ir->rs1 = list;
    ir->rs2 = prev;
    ir->imm = link;
    ir->imm2 =
        load_off | branch_off << 8 | branch_len << 16 | load_compressed << 24;
    return true;
}
#undef OPCODE_IS
#endif

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
#if RV32_HAS_TRACE_FUSION && RV32_HAS(EXT_M) && !RV32_HAS(RV32E)
        case rv_insn_mul:
            if (!fuse_trace(rv, block, ir, trace_idea_round_prefix, false,
                            do_idea_round_prefix_trace))
                fuse_trace(rv, block, ir, trace_idea_alu_suffix, false,
                           do_idea_alu_suffix_trace);
            break;
#endif
        case rv_insn_sh:
#if RV32_HAS_TRACE_FUSION
            count = count_consecutive_insn(ir, rv_insn_sh);
            if (try_fuse_sequence(rv, block, ir, count, rv_insn_fuse3))
                ir->impl = do_fuse_sh;
#endif
            break;
        case rv_insn_lhu: {
#if RV32_HAS_TRACE_FUSION
            if (fuse_trace(rv, block, ir, trace_emfloat_halfword_shift, true,
                           do_emfloat_halfword_shift_trace))
                break;
            count = count_consecutive_insn(ir, rv_insn_lhu);
            if (try_fuse_sequence(rv, block, ir, count, rv_insn_fuse4))
                ir->impl = do_fuse_lhu;
#endif
            break;
        }
        case rv_insn_lbu: {
#if RV32_HAS_TRACE_FUSION
            /* libc byte-copy loop:
             * lbu rd, off(src); addi src, src, n; addi dst, dst, n;
             * sb rd, off(dst); bne src, limit, loop
             *
             * Execute records in order instead of imposing non-aliasing
             * constraints: this also preserves unusual but legal register
             * overlaps.  Compressed instructions retain their normal path.
             */
            if (fuse_trace(rv, block, ir, trace_byte_copy, true,
                           do_byte_copy_loop)) {
                /* The terminal BNE's lhs is the first ADDI result. If the
                 * second ADDI cannot overwrite it, retain that value in the
                 * handler instead of reloading X[] after it. */
                if (ir->fuse[2].rd != ir->fuse[1].rd)
                    ir->imm2 |= 1 << 8;
                break;
            }
            fuse_trace(rv, block, ir, trace_backward_byte_copy, true,
                       do_byte_copy_loop);
#endif
            break;
        }
        case rv_insn_lui:
            next_ir = ir->next;
            if (!next_ir)
                break;
            switch (next_ir->opcode) {
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
                } else if (ir->rd != rv_reg_zero && ir->rd != next_ir->rs1 &&
                           next_ir->imm == ir->imm &&
                           (next_ir->imm < -2048 || next_ir->imm > 2047)) {
                    /* LUI + ADD fusion (fuse2). Constant propagation always
                     * turns "lui r, K; add d, s, r" into "lui r, K; addi d, s,
                     * K", so the ADD form never reaches this pass. Only an
                     * immediate wider than 12 bits marks such a rewrite, so a
                     * decoded ADDI after a folded small constant stays free for
                     * its own fusions.
                     */
#if RV32_HAS(SYSTEM_MMIO)
                    modified_regs |= (1u << ir->rd);
                    if (next_ir->rd != 0)
                        modified_regs |= (1u << next_ir->rd);
#endif
                    ir->opcode = rv_insn_fuse2;
                    ir->rs1 = next_ir->rs1;
                    ir->rs2 = next_ir->rd;
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
                 * In SYSTEM mode, JIT uses MMU handler for address translation.
                 * LUI + LW fusion (fuse9)
                 * Skip a misaligned address: it traps, and only the unfused
                 * instructions carry the JIT's alignment checks.
                 */
                if (ir->rd != rv_reg_zero && ir->rd == next_ir->rs1 &&
                    !(((uint32_t) ir->imm + (uint32_t) next_ir->imm) & 3)) {
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
                break;
            case rv_insn_sw:
                /* LUI + SW fusion (fuse10): lui rd, imm20; sw rs2, imm12(rd)
                 * Common pattern for absolute/PC-relative stores.
                 * The lui result is used as base address for sw.
                 * Skip if rd == x0: LUI x0 produces 0, not imm << 12.
                 * Skip if rd == rs2: JIT uses rd as scratch for address
                 * calculation, which would overwrite the value to store.
                 *
                 * In SYSTEM mode, JIT uses MMU handler for address translation.
                 * LUI + SW fusion (fuse10)
                 * Skip a misaligned address, as for fuse9.
                 */
                if (ir->rd != rv_reg_zero && ir->rd == next_ir->rs1 &&
                    ir->rd != next_ir->rs2 &&
                    !(((uint32_t) ir->imm + (uint32_t) next_ir->imm) & 3)) {
                    ir->imm2 = next_ir->imm; /* sw offset */
                    ir->rs1 = next_ir->rs2;  /* sw source (data to store) */
                    ir->opcode = rv_insn_fuse10;
                    ir->impl = dispatch_table[ir->opcode];
                    remove_next_nth_ir(rv, ir, block, 1);
                }
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
            /* SW + ADDI post-increment fusion (fuse13). Keep SYSTEM mode
             * unfused because either operation may cross an MMIO or fault
             * boundary that must remain independently observable.
             */
            next_ir = ir->next;
#if !RV32_HAS(SYSTEM)
            if (next_ir && IF_insn(next_ir, addi) && ir->rs1 == next_ir->rs1 &&
                next_ir->rs1 == next_ir->rd) {
                ir->imm2 = next_ir->imm;
                ir->opcode = rv_insn_fuse13;
                ir->impl = dispatch_table[ir->opcode];
                remove_next_nth_ir(rv, ir, block, 1);
                break;
            }
#endif
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
        case rv_insn_lw: {
#if RV32_HAS_TRACE_FUSION && !RV32_HAS(RV32E)
            if (fuse_trace(rv, block, ir, trace_strlen_word, true,
                           do_strlen_word_trace))
                break;
            if (trace_match(ir, &trace_match_specs[trace_record_copy], NULL)) {
                remove_next_nth_ir(rv, ir, block, 23);
                ir->impl = do_record_copy_trace;
                break;
            }
#endif
            /* Check for LW + ADDI post-increment fusion (fuse11) first.
             * In SYSTEM mode, JIT uses MMU handler for address translation.
             */
            next_ir = ir->next;
            /* fuse11: LW + ADDI post-increment fusion */
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
        }
        case rv_insn_slli:
        case rv_insn_srli:
        case rv_insn_srai: {
#if RV32_HAS_TRACE_FUSION && !RV32_HAS(RV32E)
            if (fuse_trace(rv, block, ir, trace_primes_probe, true,
                           do_primes_probe_trace) ||
                fuse_trace(rv, block, ir, trace_bitfield_set_bit, true,
                           do_bitfield_set_bit_trace) ||
                fuse_trace(rv, block, ir, trace_bitfield_invert_bit, true,
                           do_bitfield_invert_bit_trace))
                break;
#endif
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
        }
#if RV32_HAS_TRACE_FUSION && RV32_HAS(EXT_C)
        case rv_insn_cmv:
            fuse_pointer_reverse(rv, block, ir);
            break;
#endif
        case rv_insn_addi:
            next_ir = ir->next;
#if RV32_HAS_TRACE_FUSION
            if (fuse_pointer_reverse(rv, block, ir))
                break;
#if !RV32_HAS(RV32E)
            /* NumSift's fixed child-index dataflow graph. */
            if (fuse_trace(rv, block, ir, trace_numsift_index, true,
                           do_numsift_index_trace))
                break;
#endif
#endif
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

#if RV32_HAS(JIT)
    /* Fusion frees records that compiled code may still point at. */
    if (jit_block_retains_ir(block)) {
        block->lazy_fusion_done = true;
        return;
    }
#endif

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
            uint16_t fuse_opcode =
                (cand->opcode == rv_insn_lw) ? rv_insn_fuse4 : rv_insn_fuse3;
            if (try_fuse_sequence(rv, block, cand->ir, cand->count,
                                  fuse_opcode)) {
                if (cand->opcode == rv_insn_lw)
                    T1C_COUNT(lazy_fused_loads, 1);
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
    for (i = 0, ir = block->ir_head; i < block->n_insn; i++, ir = ir->next) {
        ((constopt_func_t) constopt_table[ir->opcode])(ir, &info);
        /* Folds may write any rd: decode turns x0 ALU destinations into NOP,
         * and handlers that keep an x0 write leave x0 known zero.
         */
        assert(!info.is_constant[0] || !info.const_val[0]);
    }
}

#if RV32_HAS_PACKED_TAIL
static inline void specialize_native_addi_self(block_t *block)
{
    for (rv_insn_t *ir = block->ir_head; ir; ir = ir->next) {
        if (IF_insn(ir, addi) && ir->rd == ir->rs1)
            ir->impl = do_addi_self;
    }
}

/* Translation and fusion use the linked pool representation.  Once both have
 * completed, lay the records out consecutively and replace each link with the
 * already-resolved successor handler. */
static bool pack_block_ir(riscv_t *rv, block_t *block)
{
    assert(block->n_insn);
    rv_insn_t *packed = malloc(block->n_insn * sizeof(*packed));
    if (!packed)
        return false;

    rv_insn_t *ir = block->ir_head;
    for (uint32_t i = 0; i < block->n_insn; i++) {
        assert(ir);
        rv_insn_t *next = ir->next;
        packed[i] = *ir;
        mpool_free(rv->block_ir_mp, ir);
        ir = next;
    }
    assert(!ir);

    for (uint32_t i = 0; i + 1 < block->n_insn; i++)
        packed[i].next_impl = packed[i + 1].impl;
    packed[block->n_insn - 1].next_impl = NULL;
    block->ir_head = packed;
    block->ir_tail = &packed[block->n_insn - 1];
    return true;
}

static void free_linked_block(riscv_t *rv, block_t *block)
{
    free_decoded_irs(rv, block);
    mpool_free(rv->block_mp, block);
}
#endif

#if RV32_HAS(JIT)
/* Release the IRs of a block that is leaving the cache */
void block_free_irs(riscv_t *rv, block_t *block)
{
#if RV32_HAS(T2C) && RV32_HAS(SYSTEM)
    if (rv->aot_lift && !block->aot_saved)
        t2c_aot_collect_block(rv, block);
#endif
    /* Before the decoder pool reuses any record or its owning block. */
    jit_block_release(rv, block);
    for (rv_insn_t *ir = block->ir_head, *next_ir; ir; ir = next_ir) {
        next_ir = block_next_ir(ir);
        free(ir->branch_table);
        if (ir->fuse)
            mpool_free(rv->fuse_mp, ir->fuse);
#if !RV32_HAS_PACKED_TAIL
        mpool_free(rv->block_ir_mp, ir);
#endif
    }
#if RV32_HAS_PACKED_TAIL
    free(block->ir_head);
#endif
}
#endif

static block_t *block_find_or_translate(riscv_t *rv
#if RV32_HAS(JIT)
                                        ,
                                        uint32_t *freq
#endif
)
{
#if !RV32_HAS(JIT)
    block_map_t *map = &rv->block_map;
    /* lookup the next block using L1 cache with hash table fallback */
    block_t *next_blk = block_lookup_or_find(rv, rv->PC);
#else
    /* lookup the next block in the block cache */
    cache_lookup_t lookup = cache_get_with_freq(rv->block_cache, rv->PC, true);
    block_t *next_blk = lookup.value;
    *freq = lookup.freq;
    /* discard cache if satp mismatch or block was invalidated by SFENCE.VMA */
#if RV32_T1C_STATS_ENABLED
    if (!next_blk)
        T1C_COUNT(lookup_cold, 1);
#if RV32_HAS(SYSTEM)
    else if (!satp_in_context(rv, next_blk->satp))
        T1C_COUNT(lookup_satp, 1);
    else if (next_blk->priv_mode != rv->priv_mode)
        T1C_COUNT(lookup_priv, 1);
    else if (next_blk->invalidated)
        T1C_COUNT(lookup_invalidated, 1);
#endif
    else
        T1C_COUNT(lookup_hits, 1);
#endif
    if (!block_matches_context(rv, next_blk))
        next_blk = NULL;
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

    if (unlikely(!block_translate(rv, next_blk))) {
        free_decoded_irs(rv, next_blk);
        mpool_free(rv->block_mp, next_blk);
        return NULL;
    }
    T1C_COUNT(decoded_blocks, 1);
#if RV32_T1C_STATS_ENABLED
#if RV32_HAS(SYSTEM)
    if (rv->priv_mode == RV_PRIV_U_MODE)
#endif
        T1C_COUNT(decoded_user, 1);
#if RV32_HAS(SYSTEM)
    else
        T1C_COUNT(decoded_kernel, 1);
#endif
#endif
#if RV32_HAS(SYSTEM)
#if RV32_HAS(JIT)
    next_blk->satp =
#endif
        mmu_note_code(rv, next_blk->pc_start, next_blk->pc_end);
#endif

#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
    next_blk->invalidated = false;
#if RV32_HAS(ELF_LOADER)
    /* T1C builds its MMU path only for SYSTEM_MMIO, and otherwise addresses
     * guest memory physically, so code that runs with paging on stays
     * interpreted. Building that path for every system build would lift this.
     */
    if (next_blk->satp) {
        next_blk->translatable = false;
#if RV32_T1C_STATS_ENABLED
        if (next_blk->stats_blocker == T1C_INTERPRETED_TRANSLATABLE)
            next_blk->stats_blocker = T1C_BLOCKER_CONTEXT;
#endif
    }
#endif
#endif

    optimize_constant(rv, next_blk);
#if RV32_HAS_PACKED_TAIL
    specialize_native_addi_self(next_blk);
#endif
#if RV32_HAS(T2C) && RV32_HAS(SYSTEM)
    if (next_blk->aot_translatable && aot_enabled(rv))
        next_blk->aot_page_hash = t2c_aot_page_hash(rv, next_blk);
#endif
#if RV32_HAS(MOP_FUSION)
    /* macro operation fusion */
    match_pattern(rv, next_blk);
#endif

#if RV32_HAS_PACKED_TAIL
    if (unlikely(!pack_block_ir(rv, next_blk))) {
        free_linked_block(rv, next_blk);
        return NULL;
    }
#if RV32_HAS(JIT)
    /* Chain only into blocks T1 can never compile. Every other block is
     * entered through rv_step(), as without packing, so it is profiled and
     * its compiled code runs once T1 has it.
     */
    next_blk->head_impl = next_blk->ir_head->impl;
    if (next_blk->translatable)
        next_blk->ir_head->impl = do_enter_dispatch;
#endif
#endif

#if !RV32_HAS(JIT)
    /* insert the block into block map and L1 cache */
    block_insert(&rv->block_map, rv, next_blk);
#else
    list_add(&next_blk->list, &rv->block_list);

    /* insert the block into block cache */
    /* A cache miss is cold. cache_put knows the initial or revived frequency
     * it just installed, so take it from there rather than probing again.
     */
    block_t *replaced_blk = cache_put(rv->block_cache, rv->PC, next_blk, freq);

    if (!replaced_blk)
        return next_blk;

    if (prev == replaced_blk)
        prev = NULL;

    /* remove the connection from parents */
#if RV32_HAS(BLOCK_CHAINING)
    /* Remove incoming edges without scanning all translated blocks. */
    block_unlink_incoming_edges(replaced_blk);
    block_unlink_outgoing_edges(replaced_blk);
#endif

    /* Native artifact entries belong to verified mappings, not decoded IR, so
     * evicting an IR block keeps them; code retirement clears their caches.
     */
    block_free_irs(rv, replaced_blk);
    list_del_init(&replaced_blk->list);
    mpool_free(rv->block_mp, replaced_blk);
#endif

    assert(next_blk);
    return next_blk;
}

#if RV32_HAS(T2C) && !RV32_HAS(SYSTEM)
/* Native code has published the retired prefix and every guest register.
 * Bypass both native tiers: user T1 address operands do not retain RV32
 * wraparound. Interpret the remainder rather than repeatedly redispatching
 * to the same guarded entry or a newly hot T1 block.
 */
void t2c_interpret_block(riscv_t *rv)
{
    uint32_t freq;
    block_t *block = block_find_or_translate(rv, &freq);
    if (!block) {
        rv_log_fatal("Failed to allocate or translate block at PC=0x%08x",
                     rv->PC);
        rv->halt = true;
        return;
    }
    set_reset(&pc_set);
    has_loops = false;
    const rv_insn_t *ir = block->ir_head;
#if RV32_HAS_PACKED_TAIL
    const rv_insn_impl_t impl = block->head_impl;
#else
    const rv_insn_impl_t impl = ir->impl;
#endif
    impl(rv, ir, rv->csr_cycle, rv->PC);
}
#endif

/* We disable profiler to make sure every guest instructions be translated by
 * JIT compiler in architecture test.
 */
#if RV32_HAS(JIT) && !RV32_HAS(ARCH_TEST)
/* A block that loops is compiled once it has been dispatched this many times.
 * Compiled code belongs to one address space, or is shared kernel code, and a
 * fence retires only the spaces whose code mappings changed, so even a short
 * process keeps what it compiles for its life. Native system builds link
 * successors compiled later, so two loop dispatches or 16 ordinary ones
 * amortize translation: across 300 process launches, these start processes
 * 5% faster than 8 and 64 did. Lower ones compile too much cold code: 8
 * ordinary dispatches start them 5% slower than 16, and 1 and 4 19% slower.
 * WebAssembly keeps its profiling policy.
 */
#if RV32_HAS(SYSTEM) && !defined(__EMSCRIPTEN__)
#define LOOP_THRESHOLD 2
#define BLOCK_THRESHOLD 16
#elif RV32_HAS(SYSTEM)
#define LOOP_THRESHOLD 64
#else
#define LOOP_THRESHOLD 2
#endif
#ifndef BLOCK_THRESHOLD
#define BLOCK_THRESHOLD THRESHOLD
#endif

static bool runtime_profiler(riscv_t *rv UNUSED, block_t *block, uint32_t freq)
{
#if RV32_HAS(SYSTEM)
    if (!satp_in_context(rv, block->satp))
        return false;
#endif

    /* To profile a block after chaining, it must first be executed. */
    if (unlikely(freq >= LOOP_THRESHOLD && block->has_loops))
        return true;
    /* using frequency exceeds predetermined threshold */
    if (unlikely(freq >= BLOCK_THRESHOLD))
        return true;
    return false;
}
#endif

#if RV32_HAS(SYSTEM_MMIO)

/* Interrupts are taken between blocks, where no instruction is in flight.
 * Outside a nested handler, enter the handler without running it in place:
 * the dispatcher then runs it from the trap vector as ordinary code.
 */
#define RAISE_INTERRUPT(rv, cause)                       \
    do {                                                 \
        if ((rv)->trap_cnt) {                            \
            SET_CAUSE_AND_TVAL_THEN_TRAP(rv, cause, 0);  \
        } else {                                         \
            SET_CAUSE_AND_TVAL_THEN_ENTER(rv, cause, 0); \
            (rv)->interrupt_entered = true;              \
        }                                                \
    } while (0)

/* Return whether an interrupt trap was taken. */
static bool rv_check_interrupt(riscv_t *rv)
{
    vm_attr_t *attr = PRIV(rv);
    bool trapped = false;
    /* Resume the interrupted code for one dispatch first, so a handler
     * re-armed to fire at once cannot starve it.
     */
    if (unlikely(rv->interrupt_grant)) {
        rv->interrupt_grant = false;
        return false;
    }
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

    /* Derive current timer from cycle counter for interrupt comparison.
     * Timer is no longer incremented per-instruction; instead computed here.
     */
    uint64_t current_timer = rv->csr_cycle + rv->timer_offset;
    if (current_timer > attr->timer)
        rv->csr_sip |= RV_INT_STI;
    else
        rv->csr_sip &= ~RV_INT_STI;

    if (rv_has_plic_trap(rv)) {
        uint32_t intr_applicable = rv->csr_sip & rv->csr_sie;
        uint8_t intr_idx = ilog2(intr_applicable);
        switch (intr_idx) {
        case (SUPERVISOR_SW_INTR & 0xf):
            RAISE_INTERRUPT(rv, SUPERVISOR_SW_INTR);
            trapped = true;
            break;
        case (SUPERVISOR_TIMER_INTR & 0xf):
            RAISE_INTERRUPT(rv, SUPERVISOR_TIMER_INTR);
            trapped = true;
            break;
        case (SUPERVISOR_EXTERNAL_INTR & 0xf):
            RAISE_INTERRUPT(rv, SUPERVISOR_EXTERNAL_INTR);
            trapped = true;
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
    return trapped;
}
#endif

#if RV32_HAS(JIT) || RV32_HAS(SYSTEM)
/* Settle a trap that stopped generated code or block translation, and return
 * whether there was one. A page fault runs the guest's handler inside the MMU
 * helper. If that handler resumed somewhere else (need_handle_signal),
 * execution stopped at the access and rv->PC already names the new target. A
 * trap still pending, from that handler faulting in turn or otherwise, is taken
 * here.
 */
static inline bool settle_trap(riscv_t *rv UNUSED)
{
#if RV32_HAS(SYSTEM)
    bool settled = need_handle_signal;
    need_handle_signal = false;
    if (rv->trap_cnt) {
        trap_handler(rv);
        return true;
    }
    return settled;
#else
    return false;
#endif
}
#endif

#if RV32_HAS(JIT)
/* Enter generated code. Clang's -fsanitize=function checks the callee of an
 * indirect call by reading a type signature from the eight bytes before it,
 * which generated code does not carry. The T1 entry point is the first byte of
 * its own mapping, so that read faults whenever the page below is unmapped, as
 * address randomization sometimes leaves it. These calls skip the check.
 */
static DISABLE_UBSAN_FUNC void run_t1_block(riscv_t *rv,
                                            struct jit_state *state,
                                            const block_t *block)
{
    T1C_COUNT(native_entries, 1);
#if defined(__EMSCRIPTEN__)
    (void) state;
    jit_wasm_run(rv, block);
#else
    ((exec_block_func_t) state->buf)(rv,
                                     (uintptr_t) (state->buf + block->offset));
#endif
}

/* Record that a block's native code is live, for RVOP_CHAIN_TAIL. */
#if RV32_HAS(SYSTEM)
#define JIT_STAMP_NATIVE(rv, block) \
    ((block)->ir_head->native_gen = (uint32_t) jit_code_generation(rv))
#else
#define JIT_STAMP_NATIVE(rv, block) ((void) 0)
#endif

#if RV32_HAS(T2C)
/* Native code checks the dispatcher's chain deadline at every region entry
 * and back edge, so it returns in time for timer interrupts.
 */
static DISABLE_UBSAN_FUNC void run_t2c_block(riscv_t *rv, void *entry)
{
    T1C_COUNT(t2c_entries, 1);
#if RV32_HAS(SYSTEM)
    rv->aot_dispatches++;
#endif
    if (rv->aot_enter)
        rv->aot_enter(rv, entry);
    else
        ((exec_t2c_func_t) entry)(rv);
}
#endif
#endif

void rv_step(void *arg)
{
    assert(arg);
    riscv_t *rv = arg;

    vm_attr_t *attr = PRIV(rv);
    uint32_t cycles = attr->cycle_per_step;

    /* A reboot starts rv_step again with the reset guest's cycle counter. */
    const uint64_t cycles_target = rv->csr_cycle + cycles;
#if RV32_HAS_PACKED_TAIL || RV32_HAS(SYSTEM)
    rv->branch_chain_cycle_target = cycles_target;
#endif
#if RV32_HAS_PACKED_TAIL
#if RV32_HAS(JIT)
    set_reset(&pc_set);
#endif
#endif

#if defined(__EMSCRIPTEN__)
    /* A fixed guest-cycle budget caps fast backends at the browser's frame
     * rate. Use elapsed host time to yield, retaining the cycle cap as an upper
     * bound. Sample the JS clock once per stride of guest cycles, so its cost
     * follows the work done rather than the number of (possibly short) blocks;
     * a sparse dispatch count covers loops that stall cycles.
     */
    const double wasm_deadline = emscripten_get_now() + 8.0;
    uint64_t wasm_clock_cycle = rv->csr_cycle;
    unsigned wasm_dispatches = 0;
#endif

    /* loop until hitting the cycle target or hart is halted */
    while (rv->csr_cycle < cycles_target && !rv->halt) {
#if RV32_HAS(ARCH_TEST) && RV32_HAS(JIT) && !RV32_HAS(SYSTEM)
        /* Native stores bypass the interpreter's tohost write hook. Observe
         * their result before dispatching the next block of a self-checking
         * test, which otherwise spins forever after reporting completion.
         */
        if (rv->tohost_addr) {
            check_tohost_write(rv, rv->tohost_addr,
                               rv->io.mem_read_w(rv, rv->tohost_addr));
            if (rv->halt)
                break;
        }
#endif
        T1C_COUNT(dispatches, 1);
#if defined(__EMSCRIPTEN__)
        if (rv->csr_cycle - wasm_clock_cycle >= (1U << 15) ||
            !(++wasm_dispatches & 255)) {
            wasm_clock_cycle = rv->csr_cycle;
            if (emscripten_get_now() >= wasm_deadline)
                break;
        }
#endif
#if RV32_HAS(SYSTEM_MMIO)
        /* check for any interrupt after every block emulation */
#ifdef __EMSCRIPTEN__
        /* A trap moves rv->PC to the trap vector, but a saved continuation
         * still belongs to the interrupted code. Discard it and redispatch from
         * rv->PC instead. The interrupted block must not chain into the
         * handler either.
         */
        if (rv_check_interrupt(rv)) {
            rv->next_insn = NULL;
            prev = NULL;
        }
#else
        /* The interrupted block must not chain into the handler. */
        if (rv_check_interrupt(rv))
            prev = NULL;
#endif
#endif

#if RV32_HAS(SYSTEM)
        /* Chains stop at the slice end, or earlier when the timer comes due,
         * so that the next dispatch delivers the interrupt on time. A timer
         * already pending is not a deadline: it waits for the guest to unmask
         * it, which the slice end bounds.
         */
        uint64_t chain_target = cycles_target;
#if RV32_HAS(SYSTEM_MMIO)
        const uint64_t timer_due = attr->timer - rv->timer_offset + 1;
        if (timer_due > rv->csr_cycle && timer_due < chain_target)
            chain_target = timer_due;
#endif
        /* A synchronous interrupt handler may consume this entire slice.
         * Still let the resumed guest execute one block, as the interpreter
         * does, before returning to the outer scheduler.
         */
        if (rv->csr_cycle >= chain_target)
            chain_target = rv->csr_cycle + 1;
        rv->branch_chain_cycle_target = chain_target;
#endif

#ifdef __EMSCRIPTEN__
#if !RV32_HAS(JIT)
        if (need_clear_block_map)
            rv->next_insn = NULL;
#endif
        // Resume from saved instruction after stack unwind
        if (rv->next_insn) {
            const rv_insn_t *ir = rv->next_insn;
            rv->next_insn = NULL;
            if (unlikely(!ir->impl(rv, ir, rv->csr_cycle, rv->PC))) {
                prev = NULL;
                break;
            }
            prev = NULL; /* Reset to avoid mis-chaining across unwind */
            continue;
        }
#endif

#if RV32_HAS(T2C) && RV32_HAS(SYSTEM)
        /* A bound entry has already passed instruction translation and page
         * identity checks. Code retirement clears this same cache, so a hit
         * can skip IR lookup, frequency updates, and symbol binding.
         */
        if (rv->aot_index && !rv->aot_fallback) {
            bool fault;
            void *entry = aot_native_entry(rv, &fault);
            if (unlikely(fault)) {
                need_retranslate = false;
                settle_trap(rv);
                prev = NULL;
                continue;
            }
            if (entry) {
                last_pc = rv->PC;
                run_t2c_block(rv, entry);
                settle_trap(rv);
                prev = NULL;
                continue;
            }
        }
#endif

#if JIT_RUN_CACHED
        /* After a one-shot AOT fallback, the decoded block runs the access. */
        if (IIF(RV32_HAS(T2C))(!rv->aot_fallback &&, ) jit_run_cached(rv)) {
            settle_trap(rv);
            prev = NULL;
            continue;
        }
#endif

        if (prev && prev->pc_start != last_pc) {
            /* update previous block */
#if !RV32_HAS(JIT)
            prev = block_lookup_or_find(rv, last_pc);
#else
            prev = last_pc ? cache_get(rv->block_cache, last_pc, false) : NULL;
#endif
        }
        /* lookup the next block in block map or translate a new block,
         * and move onto the next block.
         */
#if RV32_HAS(JIT)
        uint32_t freq;
        block_t *block = block_find_or_translate(rv, &freq);
#else
        block_t *block = block_find_or_translate(rv);
#endif
        /* by now, a block should be available */
        if (unlikely(!block)) {
            if (rv_has_halted(rv)) /* a fault handler stopped the hart */
                return;
#if RV32_HAS(SYSTEM)
            /* A page fault while fetching the first instruction: settle it and
             * continue instead of halting.
             */
            if (settle_trap(rv)) {
                prev = NULL;
                continue;
            }
#endif
            rv_log_fatal("Failed to allocate or translate block at PC=0x%08x",
                         rv->PC);
            rv->halt = true;
#if RV32_HAS_PACKED_TAIL
            rv->branch_chain_cycle_target = 0;
#endif
            return;
        }
        assert(block);

#if !RV32_HAS(JIT)
        /* An inline trap can skip the interrupted instruction, returning
         * before its nextop consumes the deferred clear. Retire old mappings
         * before executing any block, including one just fetched by a trap.
         */
        if (unlikely(retire_pending_blocks(rv))) {
            prev = NULL;
            continue;
        }
#endif

#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
        assert(block_matches_context(rv, block));
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
#if RV32_HAS(SYSTEM)
            && block_matches_context(rv, prev)
#if RV32_HAS(JIT)
            /* Shared and per-space code retire separately, so an edge must
             * stay within one of them, or a live block could follow it into
             * retired code.
             */
            && prev->satp == block->satp
#endif
#endif
        ) {
            const rv_insn_t *last_ir = prev->ir_tail;
            /* Chain onto the edge this transition actually took. A
             * page-terminated block and an unconditional direct branch both
             * always leave through their taken slot; a conditional branch
             * leaves through the one is_branch_taken names.
             */
            if (prev->page_terminated)
                block_link_edge(prev, true, block);
            else if (!insn_is_unconditional_branch(last_ir->opcode))
                block_link_edge(prev, is_branch_taken, block);
            else if (insn_is_direct_branch(last_ir->opcode))
                block_link_edge(prev, true, block);
        }
#endif
        last_pc = rv->PC;
#if RV32_HAS(JIT)
#if RV32_HAS(T2C)
        if (!block->compiled)
            t2c_aot_bind(rv, block);
        /* Execute an offline-compiled region when available. */
        bool aot_fallback = false;
#if RV32_HAS(SYSTEM)
        aot_fallback = rv->aot_fallback;
        rv->aot_fallback = false;
#endif
        if (!aot_fallback && block->hot2) {
            /* The artifact remains mapped for the lifetime of this VM. */
#if defined(__aarch64__)
            /* Ensure instruction cache coherency before executing T2C code */
            __asm__ volatile("isb" ::: "memory");
#endif
            JIT_STAMP_NATIVE(rv, block);
            run_t2c_block(rv, block->func);
            settle_trap(rv);
            prev = NULL;
            continue;
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
#if RV32_HAS_PACKED_TAIL
            set_reset(&pc_set);
#endif
            /* Only the emulator thread updates or reads n_invoke. */
            block->n_invoke++;
#if defined(__aarch64__)
            /* Ensure instruction cache coherency before executing JIT code */
            __asm__ volatile("isb" ::: "memory");
#endif
            JIT_STAMP_NATIVE(rv, block);
            run_t1_block(rv, state, block);
            if (rv->jit_entry_cycles)
                rv->csr_cycle += block->cycle_cost;
            settle_trap(rv);
            prev = NULL;
            continue;
        } /* check if the execution path is potential hotspot */
#if RV32_HAS_PACKED_TAIL
        /* Compilable packed blocks return to the dispatcher at each edge.
         * Keep their interpreted path across dispatches to detect cycles.
         */
        if (set_probe(&pc_set, block->pc_start))
            block->has_loops = true;
#endif
        if (block->translatable
#if RV32_HAS(T2C) && !RV32_HAS(SYSTEM)
            && (!PRIV(rv)->aot_collect || freq >= T2C_PROFILE_THRESHOLD)
#endif
#if !RV32_HAS(ARCH_TEST)
            && runtime_profiler(rv, block, freq)
#endif
        ) {
            const bool translated = jit_translate(rv, block);
#if defined(__EMSCRIPTEN__)
            /* Compiling a module takes host time that guest cycles do not show;
             * read the clock on the next dispatch.
             */
            wasm_dispatches = 255;
#endif
            if (translated) {
#if RV32_HAS_PACKED_TAIL
                set_reset(&pc_set);
#endif
#if defined(__aarch64__)
                /* Ensure icache coherency before executing JIT */
                __asm__ volatile("isb" ::: "memory");
#endif
                JIT_STAMP_NATIVE(rv, block);
                run_t1_block(rv, state, block);
                if (rv->jit_entry_cycles)
                    rv->csr_cycle += block->cycle_cost;
                settle_trap(rv);
                prev = NULL;
                continue;
            }
        }
#if !RV32_HAS_PACKED_TAIL
        set_reset(&pc_set);
#endif
        has_loops = false;
#endif
        /* execute the block by interpreter.
         * Per-instruction cycle counting is used to support block chaining,
         * where chained blocks bypass the outer loop and accumulate cycles
         * via the cycle++ in each instruction handler.
         */
        const rv_insn_t *ir = block->ir_head;
        uint64_t cycle = rv->csr_cycle;
#if RV32_HAS(JIT) && RV32_HAS_PACKED_TAIL
        const rv_insn_impl_t impl = block->head_impl;
#else
        const rv_insn_impl_t impl = ir->impl;
#endif
#if RV32_HAS(SYSTEM)
        rv->block_interrupted = false;
#endif
        if (unlikely(!impl(rv, ir, cycle, rv->PC))) {
            /* block should not be extended if exception handler invoked */
            prev = NULL;
            break;
        }
#if RV32_HAS(JIT)
        if (has_loops && !block->has_loops)
            block->has_loops = true;
#endif
#if RV32_HAS(SYSTEM)
        /* Exception-table fixups may resume inside this block. They are not
         * the terminal branch and must never train one of its chain edges.
         */
        prev = rv->block_interrupted ? NULL : block;
#else
        prev = block;
#endif
    }

#if RV32_HAS(SYSTEM_MMIO)
    /* Direct callers only; see syscall_restart_hart() */
    if (rv->reboot_requested) {
        rv->reboot_requested = false;
        rv->halt = false;
        rv_reset_dispatcher_state();
        return;
    }
#endif

#if RV32_HAS_PACKED_TAIL
    rv->branch_chain_cycle_target = 0;
#endif

    /* Incremental memory maintenance: reclaim unused pages periodically.
     * Step slices differ between builds, so count retired cycles and keep the
     * cadence of 65536 calls with the default 100-cycle slice.
     */
    static uint64_t gc_cycle = 0;
    if (unlikely(rv->csr_cycle - gc_cycle >= UINT64_C(65536) * 100)) {
        gc_cycle = rv->csr_cycle;
        memory_gc();
    }

#ifdef __EMSCRIPTEN__
    if (rv_has_halted(rv)) {
        bool stop_requested = indirect_rv_stop_requested();
        em_stop_main_loop();
        reset_rv_run_state();
        indirect_rv_cleanup();
        rv_log_info("RISC-V emulator is destroyed");
        if (!stop_requested)
            report_run_completion();
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
    uint32_t insn = fetch_instruction(rv, rv->PC);
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
        assert(rv->trap_cnt &&
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

    ir.impl = insn_handler(&ir);
    ir.pc = rv->PC;
    ir.next = NULL;
    ir.impl(rv, &ir, rv->csr_cycle, rv->PC);
    return;
}

#if RV32_HAS(SYSTEM)

/*
 * Run the trap handler of the guest OS instruction by instruction until it
 * returns via sret. A trap raised while running it, e.g., a page fault or an
 * external interrupt, enters this function again and raises rv->trap_cnt, so
 * each level runs until its own sret brings trap_cnt below the depth it was
 * entered at, and then the outer level resumes.
 */
static void __trap_handler(riscv_t *rv)
{
    rv_insn_t *ir = mpool_calloc(rv->block_ir_mp);
    assert(ir);

    const uint32_t depth = rv->trap_cnt;
#if RV32_T1C_STATS_ENABLED
    const uint32_t cause =
        RV_PRIV_IS_U_OR_S_MODE() ? rv->csr_scause : rv->csr_mcause;
    const uint32_t cause_index = T1C_CAUSE_INDEX(cause);
#endif
    /* A nested trap overwrites last_csr_sepc, so restore the one of this level
     * for CHECK_PENDING_SIGNAL after this level returns.
     */
    const uint32_t last_csr_sepc = rv->last_csr_sepc;

    /* decremented by sret implementation */
    while (rv->trap_cnt >= depth && !rv_has_halted(rv)) {
        uint32_t insn;
    retry_fetch:
        insn = fetch_instruction(rv, rv->PC);

        /* If instruction fetch returned 0 and need_retranslate is set, this
         * indicates MMU was enabled during execution. Clear flag and retry
         * the fetch. This matches the pattern in block_translate and rv_step
         * for handling MMU enable during trap handling.
         */
        if (!insn && need_retranslate) {
            need_retranslate = false;
            goto retry_fetch;
        }

        /* If instruction fetch failed (insn==0), it indicates either:
         * 1. Page fault during ifetch (need_handle_signal was set)
         * 2. Invalid PC or other error
         * In both cases, break out and let the main loop handle it.
         */
        if (!insn)
            break;

        T1C_COUNT(trap_instructions, 1);
        T1C_COUNT(trap_insns[cause_index], 1);
        rv_decode(ir, insn);
        reloc_enable_mmu_jalr_addr = rv->PC;

        ir->impl = insn_handler(ir);
        rv->compressed = is_compressed(insn);
        ir->impl(rv, ir, rv->csr_cycle, rv->PC);

#if RV32_HAS(SYSTEM_MMIO)
        /*
         * local_irq_enable() might happen during the trap handling, for
         * example, userspace store page fault occurs when writing to the disk
         * and need to wait for the disk interrupt.
         *
         * Thus, need to check if any interrupt occurs when SIE = 1 and take it
         * as a nested trap before the sret instruction of this level. Its sret
         * returns here and the handling of this level resumes.
         */
        if (rv->trap_cnt >= depth && rv_has_plic_trap(rv) &&
            ilog2(rv->csr_sip & rv->csr_sie) ==
                (SUPERVISOR_EXTERNAL_INTR & 0xf))
            SET_CAUSE_AND_TVAL_THEN_TRAP(rv, SUPERVISOR_EXTERNAL_INTR, 0);
#endif /* SYSTEM_MMIO */
    }

    rv->last_csr_sepc = last_csr_sepc;
    mpool_free(rv->block_ir_mp, ir);
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
        rv->csr_sstatus &= ~(SSTATUS_SPIE | SSTATUS_SIE | SSTATUS_SPP);
        rv->csr_sstatus |= (sstatus_sie << SSTATUS_SPIE_SHIFT) |
                           (rv->priv_mode << SSTATUS_SPP_SHIFT);
        rv->priv_mode = RV_PRIV_S_MODE;
#if RV32_HAS(SYSTEM)
        mmu_access_context_changed(rv);
#endif
        base = rv->csr_stvec & ~0x3;
        mode = rv->csr_stvec & 0x3;
        cause = rv->csr_scause;
        T1C_COUNT(traps[T1C_CAUSE_INDEX(cause)], 1);
        rv->csr_sepc = rv->PC;
#if RV32_HAS(SYSTEM)
        rv->last_csr_sepc = rv->csr_sepc;
        if (!rv->csr_stvec) { /* in case CSR is not configured */
            if (rv->trap_cnt)
                rv->trap_cnt--;
#if RV32_HAS(ELF_LOADER)
            /* A directly loaded program has no OS to handle its exceptions;
             * treat them as user mode does. Resuming at sepc would retry a
             * misaligned access forever.
             */
            if (!(cause & (1U << 31))) {
                rv_trap_default_handler(rv, cause, rv->csr_stval,
                                        &rv->csr_sepc);
                return;
            }
#endif
            /* For system mode without trap vector, restore PC from sepc
             * and clear trap_cnt to continue execution. This handles
             * spurious interrupts during early boot before handlers are set.
             */
            rv->PC = rv->csr_sepc;
            return;
        }
#endif
    } else { /* machine */
        const uint32_t mstatus_mie =
            (rv->csr_mstatus & MSTATUS_MIE) >> MSTATUS_MIE_SHIFT;
        rv->csr_mstatus &= ~(MSTATUS_MPIE | MSTATUS_MIE | MSTATUS_MPP);
        rv->csr_mstatus |= (mstatus_mie << MSTATUS_MPIE_SHIFT) |
                           (rv->priv_mode << MSTATUS_MPP_SHIFT);
        rv->priv_mode = RV_PRIV_M_MODE;
#if RV32_HAS(SYSTEM)
        mmu_access_context_changed(rv);
#endif
        base = rv->csr_mtvec & ~0x3;
        mode = rv->csr_mtvec & 0x3;
        cause = rv->csr_mcause;
        T1C_COUNT(traps[T1C_CAUSE_INDEX(cause)], 1);
        rv->csr_mepc = rv->PC;
        if (!rv->csr_mtvec) { /* in case CSR is not configured */
#if RV32_HAS(SYSTEM)
            /* For system mode without trap vector, restore PC from mepc
             * and clear trap_cnt to continue execution. This handles
             * spurious interrupts during early boot before handlers are set.
             */
            rv->PC = rv->csr_mepc;
            if (rv->trap_cnt)
                rv->trap_cnt--;
#else
            rv_trap_default_handler(rv, cause, rv->csr_mtval, &rv->csr_mepc);
#endif
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
    IIF(RV32_HAS(SYSTEM))(if (rv->trap_cnt) __trap_handler(rv);, )
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
    /* The emulator services the call in place of a trap, but it must still
     * discard the reservation as a trap would.
     */
    RV_RESERVE_CLEAR(rv);
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
        const char *comma = i < N_RV_REGS - 1 ? "," : "";
        fprintf(f, "  \"x%d\": %u%s\n", i, rv->X[i], comma);
    }
    fprintf(f, "}\n");

    if (out_file_path[0] != '-')
        fclose(f);
}
