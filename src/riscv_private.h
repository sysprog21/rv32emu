/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <assert.h>

/* for system-mode reboot */
#if RV32_HAS(SYSTEM_MMIO)
#include <setjmp.h>
#endif

#include <stdbool.h>
#include <string.h>

#include "common.h"

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

    /* vector extension */
    CSR_VSTART = 0x008,
    CSR_VXSAT = 0x009,
    CSR_VXRM = 0x00A,
    CSR_VCSR = 0x00F,
    CSR_VL = 0xC20,
    CSR_VTYPE = 0xC21,
    CSR_VLENB = 0xC22,
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

#if RV32_HAS(JIT) && RV32_HAS(BLOCK_CHAINING)
/* An edge is owned by its source block and linked into the destination block's
 * incoming_edges list. Keeping the edge with its source means that either
 * endpoint can remove it without searching the translated-block list.
 */
typedef struct {
    struct list_head target_link;
    struct block *source;
    struct block *target;
} block_edge_t;
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
    bool compiled;     /**< The T2C request is enqueued or not */
    bool is_compiling; /**< T2C thread is currently processing this block */
    bool should_free;  /**< Block was evicted while compiling, freed by T2C */
#endif
    uint32_t offset;   /**< The machine code offset in T1 code cache */
    uint32_t n_invoke; /**< The invoking times of T1 machine code */
    void *func;        /**< The function pointer of T2 machine code */
#if RV32_HAS(T2C)
    void *llvm_engine; /**< LLVM execution engine (keeps func memory alive) */
#endif
    struct list_head list;
#if RV32_HAS(BLOCK_CHAINING)
    struct list_head incoming_edges; /**< edges ending at this block */
    block_edge_t taken_edge, untaken_edge;
#endif
#endif
} block_t;

#if RV32_HAS(BLOCK_CHAINING)
/* The chain pointer for an edge lives on its source block's current tail
 * instruction. Deriving it on demand rather than pinning it keeps the edge
 * valid when macro-op fusion shortens an already-chained block and moves
 * @ir_tail back over the instruction that held the link.
 */
static inline rv_insn_t **block_chain_slot(block_t *source, bool taken)
{
    return taken ? &source->ir_tail->branch_taken
                 : &source->ir_tail->branch_untaken;
}
#endif

#if RV32_HAS(JIT) && RV32_HAS(BLOCK_CHAINING)
/* Edge lists are owned by the emulator thread alone. The T2C thread signals a
 * block it can no longer reach through should_free and never touches an edge
 * itself, so none of this needs cache_lock.
 */
static inline void block_init_edge_lists(block_t *block)
{
    INIT_LIST_HEAD(&block->incoming_edges);
    INIT_LIST_HEAD(&block->taken_edge.target_link);
    INIT_LIST_HEAD(&block->untaken_edge.target_link);
    block->taken_edge.source = block;
    block->untaken_edge.source = block;
    block->taken_edge.target = NULL;
    block->untaken_edge.target = NULL;
}

/* The patch site of an edge always lives on its source block's current tail
 * instruction. Deriving it on demand rather than pinning it at link time keeps
 * the edge valid when macro-op fusion shortens an already-chained block and
 * moves @ir_tail backwards over the instruction that held the link.
 */
static inline rv_insn_t **block_edge_slot(const block_edge_t *edge)
{
    return block_chain_slot(edge->source, edge == &edge->source->taken_edge);
}

static inline void block_edge_unlink(block_edge_t *edge)
{
    if (!edge->target)
        return;

    rv_insn_t **slot = block_edge_slot(edge);
    if (!list_empty(&edge->target_link)) {
        assert(*slot == edge->target->ir_head);
        list_del_init(&edge->target_link);
    }
    *slot = NULL;
    edge->target = NULL;
}

/* Install a chain edge while the slot is still open, so the first transition
 * observed through it wins. An eviction clears the slot and the edge together,
 * which is what lets a later transition re-chain it.
 */
static inline void block_link_edge(block_t *source, bool taken, block_t *target)
{
    rv_insn_t **slot = block_chain_slot(source, taken);
    if (*slot)
        return;

    block_edge_t *edge = taken ? &source->taken_edge : &source->untaken_edge;
    assert(!edge->target);
    /* The T2C thread reads this slot while tracing, and chaining runs on the
     * emulator thread without cache_lock. Publish it atomically so that read
     * is not a data race: a tracer sees either no edge or this one, and both
     * describe a block it may legitimately compile. Relaxed is enough -
     * nothing downstream of the pointer is published by this store.
     */
    ATOMIC_STORE(slot, target->ir_head, ATOMIC_RELAXED);
    edge->target = target;
    list_add(&edge->target_link, &target->incoming_edges);
}

static inline void block_unlink_outgoing_edges(block_t *block)
{
    block_edge_unlink(&block->taken_edge);
    block_edge_unlink(&block->untaken_edge);
}

static inline void block_unlink_incoming_edges(block_t *block)
{
    while (!list_empty(&block->incoming_edges)) {
        block_edge_t *edge =
            list_first_entry(&block->incoming_edges, block_edge_t, target_link);
        if (edge->source == block)
            list_del_init(&edge->target_link);
        else
            block_edge_unlink(edge);
    }
}

static inline void block_unlink_edges(block_t *block)
{
    block_unlink_outgoing_edges(block);
    block_unlink_incoming_edges(block);
}

static inline uint32_t block_incoming_edge_count(block_t *block)
{
    uint32_t count = 0;
    block_edge_t *edge;

    list_for_each_entry (edge, &block->incoming_edges, target_link)
        count++;
    return count;
}
#elif RV32_HAS(BLOCK_CHAINING)
/* Without the JIT there is no eviction to unlink from, so a chain edge is
 * just the pointer. Keeping the name and the rule lets rv_step spell chaining
 * one way across both builds.
 */
static inline void block_link_edge(block_t *source, bool taken, block_t *target)
{
    rv_insn_t **slot = block_chain_slot(source, taken);
    if (!*slot)
        *slot = target->ir_head;
}
#endif

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

/* L1 direct-mapped block cache for fast block lookup.
 * Inspired by rvdbt's tcache.h design.
 * Expected gain: 5-15% by avoiding hash table lookup for hot loops.
 *
 * Design: Separated tag/pointer arrays for cache efficiency.
 * - Tag array (1KB) checked first - fits in L1, good for miss path
 * - Pointer array (2KB) loaded only on hit
 * - Benchmarked faster than interleaved on x86-64 (+11.9% vs +8.8%)
 */
#define BLOCK_L1_SIZE 256
#define BLOCK_L1_MASK (BLOCK_L1_SIZE - 1)

/* Index shift for L1 cache lookup.
 * With EXT_C: PCs can be half-word aligned, shift by 1 to use bit 1.
 * Without EXT_C: PCs are word-aligned, shift by 2.
 * Using correct shift reduces conflict misses in compressed code.
 */
#if RV32_HAS(EXT_C)
#define BLOCK_L1_INDEX_SHIFT 1
#else
#define BLOCK_L1_INDEX_SHIFT 2
#endif

/* Cache line size for alignment (typical x86/Arm64). */
#define CACHE_LINE_SIZE 64

/* Invalid tag sentinel - guaranteed never to match a valid PC.
 * Valid RISC-V PCs are word-aligned (or half-word for C extension),
 * so a value with low bits set is always invalid.
 */
#define BLOCK_L1_INVALID_TAG 0xFFFFFFFFu

/* L1 block cache with separated arrays for cache efficiency.
 * Tag array checked first (1KB), pointer loaded only on hit (2KB).
 * Separated layout benchmarked faster than interleaved on x86-64.
 */
typedef struct {
    uint32_t tags[BLOCK_L1_SIZE]; /**< PC tags for fast comparison */
    block_t *ptrs[BLOCK_L1_SIZE]; /**< block pointers, loaded on tag hit */
} block_l1_cache_t;

/* clear all block in the block map */
void block_map_clear(riscv_t *rv);

struct riscv_internal {
    bool halt; /**< indicate whether the core is halted */

#ifdef __EMSCRIPTEN__
/* Soft limit: yield at block boundaries */
#ifndef WASM_BLOCK_LIMIT
#define WASM_BLOCK_LIMIT 5000
#endif
/* Hard limit: force yield (total budget 15000 with the soft limit) */
#ifndef WASM_BLOCK_HARD_LIMIT
#define WASM_BLOCK_HARD_LIMIT 10000
#endif
    /* WASM stack overflow prevention:
     * Break recursion chain at block boundaries to unwind stack.
     * Use block-count budget (not instruction count) to avoid interrupting
     * kernel atomic operations within basic blocks.
     */
    int wasm_block_depth;
    const rv_insn_t *next_insn;
#ifdef WASM_DEBUG_BLOCKS
    int max_block_depth_seen;      /* Track max recursion for validation */
    int yield_soft_count;          /* INTER yields at block boundaries */
    int yield_hard_count;          /* INTRA emergency yields */
    uint64_t total_depth_at_yield; /* For calculating avg depth per yield */
#endif
#endif

    /* integer registers */
    /*
     * Aarch64 encoder only accepts 9 bits signed offset. Do not put this
     * structure below the section.
     */
    riscv_word_t X[N_RV_REGS];
    riscv_word_t PC;

    uint64_t timer; /**< strictly increment timer */

#if RV32_HAS(VIRTIO_NET)
    uint64_t last_vnet_refresh;
#endif

#if RV32_HAS(VIRTIO_SND)
    uint64_t last_vsnd_refresh;
#endif

#if RV32_HAS(SYSTEM)
    /* is_trapped must be within 256-byte offset for ARM64 JIT access */
    bool is_trapped;
#endif

#if !RV32_HAS(JIT)
    /* L1 block cache - tag/pointer separation for cache efficiency.
     * Tags checked first (1KB), pointers loaded only on hit (2KB).
     * Placed near hot fields for interpreter fast path.
     */
    block_l1_cache_t block_l1 __ALIGNED(CACHE_LINE_SIZE);
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
        uint32_t pc; /* PC of the instruction (for trap return address) */
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

#if RV32_HAS_PACKED_TAIL
    /* Exclusive rv_step() cycle limit while native branch chaining is live. */
    uint64_t branch_chain_cycle_target;
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

#if RV32_HAS(EXT_A)
    /* LR/SC reservation set.
     *
     * LR.W registers the naturally aligned word it loaded; SC.W stores only
     * if that reservation is still valid and covers the same address.  The
     * spec lets an implementation invalidate a reservation for its own
     * reasons, so the set is kept deliberately small (one word, one hart):
     * it is armed by LR.W and cleared by SC.W and by any trap or interrupt.
     * Clearing on traps is what stops a reservation from surviving a context
     * switch, which would otherwise let an SC.W succeed against an LR.W
     * performed by a different task.
     */
    bool lr_valid;    /**< a reservation is currently held */
    uint32_t lr_addr; /**< address reserved by the last LR.W */
#endif
#if !RV32_HAS(JIT)
    block_map_t block_map; /**< basic block map (fallback on L1 miss) */
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
#if RV32_HAS(T2C)
    void *inline_cache; /* Inline cache for fast indirect jump resolution */
#endif
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

#if RV32_HAS(SYSTEM_MMIO)
    /* Jump buffer for restarting the main loop after a Linux guestOS reboot */
    jmp_buf reboot_jmp;
#endif
#endif

#if RV32_HAS(ARCH_TEST)
    /* RISC-V architectural test support: tohost/fromhost addresses */
    uint32_t tohost_addr;
    uint32_t fromhost_addr;
#endif

#if RV32_HAS(EXT_V)
    vreg_t V[N_RV_REGS];

    uint32_t csr_vstart; /* Vector start position */
    uint32_t csr_vxsat;  /* Fixed-Point Saturate Flag */
    uint32_t csr_vxrm;   /* Fixed-Point Rounding Mode */
    uint32_t csr_vcsr;   /* Vector control and status +register */
    uint32_t csr_vl;     /* Vector length */
    uint32_t csr_vtype;  /* Vector data type register */
    uint32_t csr_vlenb;  /* VLEN/8 (vector register length in bytes) */
#endif
};

/* A cached block is usable from here only when it belongs to the address space
 * executing now. Address translation makes that a real question in system
 * mode; user mode has one space, so this folds away to a null check.
 */
static inline bool block_matches_context(const riscv_t *rv UNUSED,
                                         const block_t *block)
{
    /* satp and invalidated only exist where the JIT caches blocks across
     * address spaces; elsewhere there is nothing to disambiguate.
     */
#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
    return block && block->satp == rv->csr_satp && !block->invalidated;
#else
    return block;
#endif
}

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
 * On native builds (HAVE_MMAP=1), no bounds checking is performed. The
 * invariant that 'addr' is valid is maintained by ELF loader validation,
 * memory layout, and stack/heap bounds. Out-of-bounds access triggers
 * SIGSEGV which chains to the default handler.
 *
 * On Emscripten/WASM (HAVE_MMAP=0), explicit bounds checking prevents
 * "out of bounds memory access" WASM traps. The branch is highly
 * predictable (almost never taken) so the performance cost is negligible.
 *
 * Note: Only used when SYSTEM mode is disabled. SYSTEM mode requires MMU/TLB
 * translation which must go through the io callbacks.
 */
#if !RV32_HAS(SYSTEM)
FORCE_INLINE uint32_t ram_read_w(const riscv_t *rv, uint32_t addr)
{
    vm_attr_t *attr = PRIV(rv);
#if !HAVE_MMAP
    if (unlikely((uint64_t) addr + sizeof(uint32_t) > attr->mem->mem_size))
        return 0;
#endif
    uint32_t val;
    memcpy(&val, attr->mem->mem_base + addr, sizeof(val));
    return val;
}

FORCE_INLINE uint16_t ram_read_s(const riscv_t *rv, uint32_t addr)
{
    vm_attr_t *attr = PRIV(rv);
#if !HAVE_MMAP
    if (unlikely((uint64_t) addr + sizeof(uint16_t) > attr->mem->mem_size))
        return 0;
#endif
    uint16_t val;
    memcpy(&val, attr->mem->mem_base + addr, sizeof(val));
    return val;
}

FORCE_INLINE uint8_t ram_read_b(const riscv_t *rv, uint32_t addr)
{
    vm_attr_t *attr = PRIV(rv);
#if !HAVE_MMAP
    if (unlikely((uint64_t) addr >= attr->mem->mem_size))
        return 0;
#endif
    return attr->mem->mem_base[addr];
}

FORCE_INLINE void ram_write_w(const riscv_t *rv, uint32_t addr, uint32_t val)
{
    vm_attr_t *attr = PRIV(rv);
#if !HAVE_MMAP
    if (unlikely((uint64_t) addr + sizeof(uint32_t) > attr->mem->mem_size))
        return;
#endif
    memcpy(attr->mem->mem_base + addr, &val, sizeof(val));
}

FORCE_INLINE void ram_write_s(const riscv_t *rv, uint32_t addr, uint16_t val)
{
    vm_attr_t *attr = PRIV(rv);
#if !HAVE_MMAP
    if (unlikely((uint64_t) addr + sizeof(uint16_t) > attr->mem->mem_size))
        return;
#endif
    memcpy(attr->mem->mem_base + addr, &val, sizeof(val));
}

FORCE_INLINE void ram_write_b(const riscv_t *rv, uint32_t addr, uint8_t val)
{
    vm_attr_t *attr = PRIV(rv);
#if !HAVE_MMAP
    if (unlikely((uint64_t) addr >= attr->mem->mem_size))
        return;
#endif
    attr->mem->mem_base[addr] = val;
}
#endif /* !RV32_HAS(SYSTEM) */
