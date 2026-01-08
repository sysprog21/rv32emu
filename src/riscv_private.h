/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once
#include <stdbool.h>
#include <string.h>

#if RV32_HAS(GDBSTUB)
#include "breakpoint.h"
#include "mini-gdbstub/include/gdbstub.h"
#endif
#include "decode.h"
#include "riscv.h"
#include "utils.h"
#if RV32_HAS(JIT)
#if RV32_HAS(T2C)
#include <pthread.h>
#endif
#include "cache.h"
#endif

#define PRIV(x) ((vm_attr_t *) x->data)

/* Maximum entries per fuse slot - limits fusion to 16 consecutive instructions.
 * Larger sequences are rare and provide diminishing returns.
 */
#define FUSE_MAX_ENTRIES 16
#define FUSE_SLOT_SIZE (FUSE_MAX_ENTRIES * sizeof(opcode_fuse_t))

/* CSRs */
enum {
    /* floating point */
    CSR_FFLAGS = 0x001, /* Floating-point accrued exceptions */
    CSR_FRM = 0x002,    /* Floating-point dynamic rounding mode */
    CSR_FCSR = 0x003,   /* Floating-point control and status register */

    /* Supervisor trap setup */
    CSR_SSTATUS = 0x100,    /* Supervisor status register */
    CSR_SIE = 0x104,        /* Supervisor interrupt-enable register */
    CSR_STVEC = 0x105,      /* Supervisor trap-handler base address */
    CSR_SCOUNTEREN = 0x106, /* Supervisor counter enable */

    /* Supervisor trap handling */
    CSR_SSCRATCH = 0x140, /* Supervisor register for machine trap handlers */
    CSR_SEPC = 0x141,     /* Supervisor exception program counter */
    CSR_SCAUSE = 0x142,   /* Supervisor trap cause */
    CSR_STVAL = 0x143,    /* Supervisor bad address or instruction */
    CSR_SIP = 0x144,      /* Supervisor interrupt pending */

    /* Supervisor protection and translation */
    CSR_SATP = 0x180, /* Supervisor address translation and protection */

    /* Machine information registers */
    CSR_MVENDORID = 0xF11, /* Vendor ID */
    CSR_MARCHID = 0xF12,   /* Architecture ID */
    CSR_MIMPID = 0xF13,    /* Implementation ID */
    CSR_MHARTID = 0xF14,   /* Hardware thread ID */

    /* Machine trap setup */
    CSR_MSTATUS = 0x300,    /* Machine status register */
    CSR_MISA = 0x301,       /* ISA and extensions */
    CSR_MEDELEG = 0x302,    /* Machine exception delegate register */
    CSR_MIDELEG = 0x303,    /* Machine interrupt delegate register */
    CSR_MIE = 0x304,        /* Machine interrupt-enable register */
    CSR_MTVEC = 0x305,      /* Machine trap-handler base address */
    CSR_MCOUNTEREN = 0x306, /* Machine counter enable */

    /* machine trap handling */
    CSR_MSCRATCH = 0x340, /* Scratch register for machine trap handlers */
    CSR_MEPC = 0x341,     /* Machine exception program counter */
    CSR_MCAUSE = 0x342,   /* Machine trap cause */
    CSR_MTVAL = 0x343,    /* Machine bad address or instruction */
    CSR_MIP = 0x344,      /* Machine interrupt pending */

    /* low words */
    CSR_CYCLE = 0xC00, /* Cycle counter for RDCYCLE instruction */
    CSR_TIME = 0xC01,  /* Timer for RDTIME instruction */
    CSR_INSTRET = 0xC02,

    /* high words */
    CSR_CYCLEH = 0xC80,
    CSR_TIMEH = 0xC81,
    CSR_INSTRETH = 0xC82,
};

/* Lazy fusion candidate for memory operations in SYSTEM_MMIO mode.
 * At decode time, consecutive LW/SW sequences are marked as candidates.
 * During first execution, addresses are verified as RAM (not MMIO).
 * Only proven-safe sequences are fused for subsequent executions.
 */
#if RV32_HAS(SYSTEM_MMIO) && RV32_HAS(MOP_FUSION)
typedef struct {
    rv_insn_t *ir;  /**< first instruction of candidate sequence */
    uint8_t count;  /**< number of consecutive instructions */
    uint8_t opcode; /**< rv_insn_lw or rv_insn_sw */
    bool verified;  /**< addresses verified as RAM during first exec */
    bool failed;    /**< at least one MMIO address detected */
} lazy_fusion_candidate_t;

#define MAX_LAZY_CANDIDATES 8
#endif

/* translated basic block */
typedef struct block {
    uint32_t n_insn;           /**< number of instructions encompassed */
    uint32_t pc_start, pc_end; /**< address range of the basic block */
    uint32_t cycle_cost;       /**< cycle cost for block-level counting */

    rv_insn_t *ir_head, *ir_tail; /**< the first and last ir for this block */

#if RV32_HAS(BLOCK_CHAINING)
    bool page_terminated; /**< Block ended at page boundary (not a branch) */
#endif

#if RV32_HAS(SYSTEM_MMIO) && RV32_HAS(MOP_FUSION)
    uint8_t n_lazy_candidates; /**< number of lazy fusion candidates */
    lazy_fusion_candidate_t lazy_candidates[MAX_LAZY_CANDIDATES];
    bool lazy_fusion_done; /**< lazy fusion already attempted */
#endif

#if RV32_HAS(JIT)
    bool hot;          /**< Determine the block is potential hotspot or not */
    bool hot2;         /**< Determine the block is strong hotspot or not */
    bool translatable; /**< Determine the block has RV32AF or not */
    bool has_loops;    /**< Determine the block has loop or not */
#if RV32_HAS(SYSTEM)
    uint32_t satp;
    bool invalidated; /**< Block invalidated by SFENCE.VMA, needs recompilation
                       */
#endif
#if RV32_HAS(T2C)
    bool compiled; /**< The T2C request is enqueued or not */
#endif
    uint32_t offset;   /**< The machine code offset in T1 code cache */
    uint32_t n_invoke; /**< The invoking times of T1 machine code */
    void *func;        /**< The function pointer of T2 machine code */
    struct list_head list;
#endif
} block_t;

/* T2C implies JIT (enforced by Kconfig and feature.h) */
#if RV32_HAS(T2C)
typedef struct {
    uint64_t key; /**< cache key (PC or PC|SATP) to look up block */
    struct list_head list;
} queue_entry_t;
#endif

#if RV32_HAS(SYSTEM)
/* Translation Lookaside Buffer (TLB) for caching VA-to-PA translations.
 * This reduces the overhead of page table walks in system simulation mode.
 *
 * TLB design:
 * - Direct-mapped cache indexed by VPN lower bits
 * - 64 entries for ~256KB working set coverage
 * - Each entry caches: VPN, PPN, permissions, and page level
 * - Separate dTLB (data) and iTLB (instruction) for better hit rates
 * - Superpages tracked via level field for selective SFENCE.VMA
 * - Invalidated on SFENCE.VMA or SATP changes
 */
#define TLB_SIZE 64
#define TLB_MASK (TLB_SIZE - 1)

/* Sv32 page levels: 1 = 4MB superpage, 2 = 4KB page */
#define TLB_PAGE_LEVEL_SUPER 1
#define TLB_PAGE_LEVEL_4K 2

typedef struct {
    uint32_t vpn, ppn; /* Virtual/Physical page number (upper 20 bits of VA) */
    uint32_t pte_addr; /* Physical address of PTE for A/D bit updates */
    uint8_t perm;      /* Permission bits: R(1), W(2), X(4), U(16) */
    uint8_t valid;     /* Entry validity flag */
    uint8_t dirty;     /* Cached dirty bit state (avoid repeated PTE writes) */
    uint8_t level;     /* Page level: 1=superpage (4MB), 2=4KB page */
} tlb_entry_t;
#endif

typedef struct {
    uint32_t block_capacity; /**< max number of entries in the block map */
    uint32_t size;           /**< number of entries currently in the map */
    block_t **map;           /**< block map */
} block_map_t;

/* clear all block in the block map */
void block_map_clear(riscv_t *rv);

struct riscv_internal {
    bool halt; /* indicate whether the core is halted */

    /* integer registers */
    /*
     * Aarch64 encoder only accepts 9 bits signed offset. Do not put this
     * structure below the section.
     */
    riscv_word_t X[N_RV_REGS];
    riscv_word_t PC;

    uint64_t timer; /* strictly increment timer */

#if RV32_HAS(SYSTEM)
    /* is_trapped must be within 256-byte offset for ARM64 JIT access.
     * Placed early in struct to ensure accessibility from JIT-generated code.
     */
    bool is_trapped;
#endif

#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
    /*
     * Aarch64 encoder only accepts 9 bits signed offset. Do not put this
     * structure below the section.
     */
    struct {
        uint8_t is_mmio; /* whether is MMIO or not (0=RAM, 1=MMIO/trap) */
        uint32_t type;   /* instruction type for MMIO handler */
        uint32_t vaddr;
        uint32_t paddr;
    } jit_mmu;
#endif
    /* user provided data */
    riscv_user_t data;

    /* I/O interface */
    riscv_io_t io;

#if RV32_HAS(EXT_F)
    /* float registers */
    riscv_float_t F[32];
    uint32_t csr_fcsr;
#endif

    /* csr registers */
    uint64_t csr_cycle;     /* Machine cycle counter */
    uint32_t csr_time[2];   /* Performance counter */
    uint32_t csr_mstatus;   /* Machine status register */
    uint32_t csr_mtvec;     /* Machine trap-handler base address */
    uint32_t csr_misa;      /* ISA and extensions */
    uint32_t csr_mtval;     /* Machine bad address or instruction */
    uint32_t csr_mcause;    /* Machine trap cause */
    uint32_t csr_mscratch;  /* Scratch register for machine trap handler */
    uint32_t csr_mepc;      /* Machine exception program counter */
    uint32_t csr_mip;       /* Machine interrupt pending */
    uint32_t csr_mie;       /* Machine interrupt enable */
    uint32_t csr_mideleg;   /* Machine interrupt delegation register */
    uint32_t csr_medeleg;   /* Machine exception delegation register */
    uint32_t csr_mvendorid; /* vendor ID */
    uint32_t csr_marchid;   /* Architecture ID */
    uint32_t csr_mimpid;    /* Implementation ID */
    uint32_t csr_mbadaddr;

    uint32_t csr_sstatus;    /* supervisor status register */
    uint32_t csr_stvec;      /* supervisor trap vector base address register */
    uint32_t csr_sip;        /* supervisor interrupt pending register */
    uint32_t csr_sie;        /* supervisor interrupt enable register */
    uint32_t csr_scounteren; /* supervisor counter-enable register */
    uint32_t csr_sscratch;   /* supervisor scratch register */
    uint32_t csr_sepc;       /* supervisor exception program counter */
    uint32_t csr_scause;     /* supervisor cause register */
    uint32_t csr_stval;      /* supervisor trap value register */
    uint32_t csr_satp;       /* supervisor address translation and protection */

    uint32_t priv_mode; /* U-mode or S-mode or M-mode */

    bool compressed; /**< current instruction is compressed or not */
#if !RV32_HAS(JIT)
    block_map_t block_map; /**< basic block map */
#else
    struct cache *block_cache;
    struct list_head block_list; /**< list of all translated blocks */
#if RV32_HAS(T2C)
    struct list_head wait_queue;
    pthread_mutex_t wait_queue_lock, cache_lock;
    pthread_cond_t wait_queue_cond;
    bool quit; /**< termination flag, protected by wait_queue_lock */
#endif
    void *jit_state;
    void *jit_cache;
#endif
    struct mpool *block_mp, *block_ir_mp, *fuse_mp;

#if RV32_HAS(GDBSTUB)
    /* gdbstub instance */
    gdbstub_t gdbstub;

    bool debug_mode;

    /* GDB instruction breakpoint */
    breakpoint_map_t breakpoint_map;

    /* The flag to notify interrupt from GDB client: it should be accessed by
     * atomic operation when starting the GDBSTUB.
     */
    bool is_interrupted;
#endif

#if RV32_HAS(SYSTEM)
    /* The flag that stores the SEPC CSR at the trap point for corectly
     * executing signal handler.
     */
    uint32_t last_csr_sepc;

    /* Data TLB for caching virtual-to-physical address translations.
     * Reduces page table walk overhead for repeated memory accesses.
     */
    tlb_entry_t dtlb[TLB_SIZE];

    /* Instruction TLB for caching instruction fetch translations.
     * Separate from dTLB for better hit rates and simpler permission checks.
     */
    tlb_entry_t itlb[TLB_SIZE];

    /* Timer offset for deriving timer from cycle counter.
     * timer = csr_cycle + timer_offset
     * This avoids per-instruction timer increments in the main loop.
     *
     * Note: RISC-V spec defines TIME as a separate real-time counter from
     * MTIME hardware. This emulator approximates TIME by deriving from CYCLE,
     * which is acceptable for emulation but differs from real hardware where
     * TIME would be independent of CPU frequency scaling or sleep states.
     */
    uint64_t timer_offset;
#endif

#if RV32_HAS(ARCH_TEST)
    /* RISC-V architectural test support: tohost/fromhost addresses */
    uint32_t tohost_addr;
    uint32_t fromhost_addr;
#endif
};

/* sign extend a 16 bit value */
FORCE_INLINE uint32_t sign_extend_h(const uint32_t x)
{
    return (int32_t) ((int16_t) x);
}

/* sign extend an 8 bit value */
FORCE_INLINE uint32_t sign_extend_b(const uint32_t x)
{
    return (int32_t) ((int8_t) x);
}

/* Detect the instruction is RV32C or not */
FORCE_INLINE bool is_compressed(uint32_t insn)
{
    return (insn & FC_OPCODE) != 3;
}

/* RAM Fast-Path Memory Access
 *
 * For userspace emulation (non-SYSTEM mode), memory accesses always go to
 * RAM, making the io callback indirection pure overhead. These inline
 * functions provide direct memory access, bypassing the function pointer
 * dispatch.
 *
 * Performance benefit: Eliminates indirect call overhead (~5-10 cycles per
 * memory access on modern CPUs due to branch predictor penalties).
 *
 * These functions perform NO bounds checking - they trust that 'addr' is
 * valid within mem_base. This invariant is maintained by:
 * 1. ELF loader validation: All program segments must fit within RAM bounds
 *    (see elf_load() in elf.c which rejects out-of-bounds segments)
 * 2. Memory layout: mem_base is allocated with sufficient size for the
 *    configured memory map (see memory_new() in riscv.c)
 * 3. Stack/heap bounds: Initial SP and brk limits are within RAM
 *
 * DO NOT relax ELF loader validation or memory allocation without adding
 * explicit bounds checks here. Out-of-bounds access leads to undefined
 * behavior (host memory corruption).
 *
 * Note: Only used when SYSTEM mode is disabled. SYSTEM mode requires MMU/TLB
 * translation which must go through the io callbacks.
 */
#if !RV32_HAS(SYSTEM)
FORCE_INLINE uint32_t ram_read_w(const riscv_t *rv, uint32_t addr)
{
    vm_attr_t *attr = PRIV(rv);
    uint32_t val;
    memcpy(&val, attr->mem->mem_base + addr, sizeof(val));
    return val;
}

FORCE_INLINE uint16_t ram_read_s(const riscv_t *rv, uint32_t addr)
{
    vm_attr_t *attr = PRIV(rv);
    uint16_t val;
    memcpy(&val, attr->mem->mem_base + addr, sizeof(val));
    return val;
}

FORCE_INLINE uint8_t ram_read_b(const riscv_t *rv, uint32_t addr)
{
    vm_attr_t *attr = PRIV(rv);
    return attr->mem->mem_base[addr];
}

FORCE_INLINE void ram_write_w(const riscv_t *rv, uint32_t addr, uint32_t val)
{
    vm_attr_t *attr = PRIV(rv);
    memcpy(attr->mem->mem_base + addr, &val, sizeof(val));
}

FORCE_INLINE void ram_write_s(const riscv_t *rv, uint32_t addr, uint16_t val)
{
    vm_attr_t *attr = PRIV(rv);
    memcpy(attr->mem->mem_base + addr, &val, sizeof(val));
}

FORCE_INLINE void ram_write_b(const riscv_t *rv, uint32_t addr, uint8_t val)
{
    vm_attr_t *attr = PRIV(rv);
    attr->mem->mem_base[addr] = val;
}
#endif /* !RV32_HAS(SYSTEM) */
