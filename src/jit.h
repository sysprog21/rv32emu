/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "aot_entry.h"
#include "riscv_private.h"
#include "utils.h"

/* Instructions charged at block entry that follow a trapping instruction.
 * Fused records can contain several independently faulting instructions.
 * Their members are all 4 bytes: trace fusion, the only form that mixes
 * widths, is disabled in JIT builds. Offsets from the block start keep the
 * order of a block whose addresses wrap past the top of the RV32 space.
 */
static inline uint32_t block_unretired_after(const block_t *block, uint32_t pc)
{
    const uint32_t at = pc - block->pc_start;
    uint32_t n = 0;
    for (const rv_insn_t *ir = block->ir_head, *next; ir; ir = next) {
        next = ir == block->ir_tail ? NULL : block_next_ir(ir);
        const uint32_t start = ir->pc - block->pc_start;
        const uint32_t end =
            (next ? next->pc : block->pc_end) - block->pc_start;
        const uint32_t count = end - start > 4 ? (end - start) / 4 : 1;
        if (at < start)
            n += count;
        else if (at < end && count > 1)
            n += count - 1 - (at - start) / 4;
    }
    return n;
}

/* Length of an original opcode; fused memory members are full-width. */
static inline uint32_t jit_instruction_size(uint16_t opcode)
{
    switch (opcode) {
#define _(inst, branch, len, translatable, regs) \
    case rv_insn_##inst:                         \
        return len;
        RV_INSN_LIST
#undef _
    default:
        return 4;
    }
}

/* T1 links direct branches whether or not the interpreter has taken the edge
 * yet, and records a jump whose target is not compiled so that the target's
 * translation can patch it later. A target with an offline-compiled entry is
 * never patched, so its jump returns to the dispatcher, which prefers AOT.
 */
#define JIT_LINK_ALL_EDGES 1

/* Native system builds dispatch straight to live T1 code, after any bound
 * AOT entry.
 */
#if RV32_HAS(SYSTEM_MMIO) && RV32_HAS(JIT) && !defined(__EMSCRIPTEN__)
#define JIT_RUN_CACHED 1
#else
#define JIT_RUN_CACHED 0
#endif

/* Native system builds also compile AMOs and CSR instructions, as
 * calls into C between native code, instead of interpreting each block that
 * holds one: the kernel runs them in its hottest paths, such as reference
 * counting and masking interrupts.
 */
#if JIT_RUN_CACHED && RV32_HAS(EXT_A) && RV32_HAS(Zicsr) && !RV32_HAS(ARCH_TEST)
#define JIT_SYSTEM_HELPERS 1
#else
#define JIT_SYSTEM_HELPERS 0
#endif
#define JIT_SYSTEM_AMO_CASES \
    case rv_insn_amoswapw:   \
    case rv_insn_amoaddw:    \
    case rv_insn_amoxorw:    \
    case rv_insn_amoandw:    \
    case rv_insn_amoorw:     \
    case rv_insn_amominw:    \
    case rv_insn_amomaxw:    \
    case rv_insn_amominuw:   \
    case rv_insn_amomaxuw
#define JIT_SYSTEM_CSR_CASES \
    case rv_insn_csrrw:      \
    case rv_insn_csrrs:      \
    case rv_insn_csrrc:      \
    case rv_insn_csrrwi:     \
    case rv_insn_csrrsi:     \
    case rv_insn_csrrci

#if RV32_HAS(T2C)
static inline bool t2c_counter_read(const rv_insn_t *ir)
{
#if RV32_HAS(SYSTEM) && RV32_HAS(Zicsr)
    if (ir->rs1 ||
        (ir->opcode != rv_insn_csrrs && ir->opcode != rv_insn_csrrc &&
         ir->opcode != rv_insn_csrrsi && ir->opcode != rv_insn_csrrci))
        return false;
    switch (ir->imm & 0xfff) {
    case CSR_CYCLE:
    case CSR_CYCLEH:
    case CSR_INSTRET:
    case CSR_INSTRETH:
    case CSR_TIME:
    case CSR_TIMEH:
        return true;
    }
#else
    (void) ir;
#endif
    return false;
}
#if RV32_HAS(SYSTEM)
/* ir is unused when neither extension is built. */
static inline bool t2c_outlined_insn(const rv_insn_t *ir UNUSED)
{
#if RV32_HAS(EXT_A)
    switch (ir->opcode) {
    case rv_insn_lrw:
    case rv_insn_scw:
    case rv_insn_amoswapw:
    case rv_insn_amoaddw:
    case rv_insn_amoxorw:
    case rv_insn_amoandw:
    case rv_insn_amoorw:
    case rv_insn_amominw:
    case rv_insn_amomaxw:
    case rv_insn_amominuw:
    case rv_insn_amomaxuw:
        return true;
    }
#endif
#if RV32_HAS(Zicsr)
    if ((ir->imm & 0xfff) == CSR_SSTATUS) {
        switch (ir->opcode) {
        case rv_insn_csrrw:
        case rv_insn_csrrs:
        case rv_insn_csrrc:
        case rv_insn_csrrwi:
        case rv_insn_csrrsi:
        case rv_insn_csrrci:
            return true;
        }
    }
#endif
    return false;
}
void t2c_run_outlined_insn(riscv_t *, uint32_t, uint32_t);
#endif

#endif

/* Jump condition codes for branch instruction code generation.
 * Values match x86-64 Jcc opcodes; on Arm64, emit_jcc_offset() maps these
 * to equivalent condition codes. Used by both architectures as symbolic
 * constants for conditional/unconditional jumps.
 */
#define JCC_JE 0x84  /* Jump if Equal (conditional) */
#define JCC_JNE 0x85 /* Jump if Not Equal (conditional) */
#define JCC_JL 0x8c  /* Jump if Less - signed (conditional) */
#define JCC_JGE 0x8d /* Jump if Greater or Equal - signed (conditional) */
#define JCC_JB 0x82  /* Jump if Below - unsigned (conditional) */
#define JCC_JAE 0x83 /* Jump if Above or Equal - unsigned (conditional) */
#define JCC_JBE 0x86 /* Jump if Below or Equal - unsigned (conditional) */
#define JCC_JA 0x87  /* Jump if Above - unsigned (conditional) */
#define JCC_JLE 0x8e /* Jump if Less or Equal - signed (conditional) */
#define JCC_JG 0x8f  /* Jump if Greater - signed (conditional) */
#define JCC_JMP 0xe9 /* Jump unconditional */

/* The native backend's state. The Wasm backend (jit_wasm.c) keeps its own
 * definition private; shared code only passes struct jit_state pointers.
 */
#if !defined(__EMSCRIPTEN__)
struct jump {
    uint32_t offset_loc;
    uint32_t target_pc;
    uint32_t target_offset;
#if RV32_HAS(SYSTEM)
    uint32_t target_satp;
    uint32_t target_priv;
#endif
};

/* A translated block: where its code starts in the code cache. Entries are
 * found through hash chains of indices into the offset map, -1 ending a chain:
 * by pc, satp and fetch privilege for lookups, and in system mode also by
 * address space and by page, so that the entries of either can be retired when
 * the guest flushes them. A retired entry is never found again; its code stays
 * in the cache, unreachable, until the cache is flushed.
 */
struct offset_map {
    uint32_t pc;
    uint32_t offset;
    int32_t next;
#if RV32_HAS(SYSTEM)
    uint32_t satp;
    uint32_t priv_mode;
    int32_t next_in_space;
    int32_t next_in_page;
    bool retired;
#endif
};

/* at least twice MAX_BLOCKS entries */
#if BLOCK_CACHE_LARGE
#define OFFSET_INDEX_BITS 17
#else
#define OFFSET_INDEX_BITS 14
#endif
#define SPACE_INDEX_BITS 8
#define PAGE_INDEX_BITS_JIT 12

struct indirect_site;

/* A jump to a target that had not been compiled when it was emitted. */
struct pending_jump {
    uint32_t offset_loc;
    uint32_t target_pc;
#if RV32_HAS(SYSTEM)
    uint32_t target_satp;
    uint32_t target_priv;
#endif
    int32_t next;
};

struct jit_state {
#if RV32_HAS(SYSTEM)
    uint32_t priv_mode; /* context of the blocks currently being emitted */
    uint64_t code_generation; /* retirement while a native helper is live */
#endif
#if defined(__x86_64__)
    bool has_bmi2; /* detected once; never emit unsupported host instructions */
#endif
    int32_t offset_index[1 << OFFSET_INDEX_BITS];
#if RV32_HAS(SYSTEM)
    int32_t space_index[1 << SPACE_INDEX_BITS];
    int32_t page_index[1 << PAGE_INDEX_BITS_JIT];
#endif
    uint8_t *buf;
    uint32_t offset;
    uint32_t stack_size;
    uint32_t size;
    uint32_t entry_loc;
    uint32_t exit_loc;
    uint32_t org_size; /* size of prologue and epilogue */
    uint32_t retpoline_loc;
    struct offset_map *offset_map;
    int n_blocks;
    struct jump *jumps;
    int n_jumps;
#if JIT_LINK_ALL_EDGES
    /* Pending jumps, chained by target as offset_index chains blocks */
    int32_t pending_index[1 << OFFSET_INDEX_BITS];
    struct pending_jump *pending;
    int n_pending;
    /* Guard slots of each block's indirect jump, filled in at run time */
    struct indirect_site *sites;
#endif
};
#endif /* !__EMSCRIPTEN__ */

#if RV32_HAS(SYSTEM)
#if defined(__EMSCRIPTEN__)
uint64_t jit_code_generation(const riscv_t *rv);
#else
static inline uint64_t jit_code_generation(const riscv_t *rv)
{
    return ((const struct jit_state *) rv->jit_state)->code_generation;
}
#endif
#endif

struct host_reg {
    uint8_t reg_idx : 5;   /* index to the host's register file */
    int8_t vm_reg_idx : 6; /* index to the vm register */
    bool dirty : 1; /* whether the context of register has been overridden */
    bool alive : 1; /* whether the register is no longer used in current basic
                       block */
};

/* Flags passed from generated code to jit_misaligned_trap(). */
#define JIT_MISALIGN_STORE 1U
#define JIT_MISALIGN_COMPRESSED 2U

#if RV32_HAS(T2C) && !RV32_HAS(SYSTEM)
/* The user AOT slow callback also resumes a wrapping address in the
 * interpreter, without adding a flag test to normal native dispatch.
 */
#define AOT_USER_INTERPRET (1U << 31)
void t2c_interpret_block(riscv_t *rv);
#endif

/* Raise the exception the interpreter raises for a misaligned load or store.
 * Both JIT tiers call this with rv->PC set to the faulting instruction and
 * every guest register in rv->X, then leave the block: the trap handler has
 * already set rv->PC to the trap vector, or, in user mode without one, emulated
 * the access and advanced past it.
 */
void jit_misaligned_trap(riscv_t *rv, uint32_t addr, uint32_t flags);

#if JIT_SYSTEM_HELPERS
/* Perform a CSR instruction as the interpreter does, at guest cycle count
 * cycle, with val the value of rs1 or the immediate; return the old CSR.
 */
uint32_t rv_csr_insn(riscv_t *rv,
                     uint32_t opcode,
                     uint32_t csr,
                     uint32_t val,
                     uint64_t cycle);
#endif

struct jit_state *jit_state_init(size_t size, uintptr_t mem_base);
void jit_state_exit(struct jit_state *state);
#if defined(__EMSCRIPTEN__)
void jit_wasm_run(riscv_t *rv, const block_t *block);
void jit_wasm_retire_block(riscv_t *rv, block_t *block);
#endif

#if RV32_HAS(SYSTEM)
#define BLOCK_SATP(block) ((block)->satp)
#define BLOCK_PRIV(block) ((block)->priv_mode)
#else
#define BLOCK_SATP(block) 0
#define BLOCK_PRIV(block) 0
#endif

/* Whether a program that never reads a counter may have each block's cycles
 * charged once on entry. Wasm charges generated operations and detached helpers
 * separately.
 */
#if defined(__EMSCRIPTEN__)
#define JIT_ENTRY_CYCLES 0
#else
#define JIT_ENTRY_CYCLES 1
#endif

/* Whether the block's compiled code still points at its IR records, which must
 * then stay in place. Wasm helpers do; native code does not.
 */
static inline bool jit_block_retains_ir(const block_t *block)
{
#if defined(__EMSCRIPTEN__)
    return block->hot; /* set exactly while the block owns a module */
#else
    (void) block;
    return false;
#endif
}

/* Drop backend resources tied to a block before its IR is freed. */
static inline void jit_block_release(riscv_t *rv, block_t *block)
{
#if defined(__EMSCRIPTEN__)
    jit_wasm_retire_block(rv, block);
#else
    (void) rv;
    (void) block;
#endif
}
#if JIT_RUN_CACHED
/* Run a live native entry without requiring its decoded block to be cached. */
bool jit_run_cached(riscv_t *rv);
#endif
bool jit_translate(riscv_t *rv, block_t *block);
/* Release a block's IR records (defined in emulate.c). */
void block_free_irs(riscv_t *rv, block_t *block);

#if RV32_HAS(SYSTEM)
/* Stop reusing the code translated for address space satp. Called when the
 * guest flushes it: a block rebuilt afterwards gets fresh code instead of what
 * was translated from the old mappings.
 */
void jit_retire_space(struct jit_state *state, uint32_t satp);

/* Drop all translated code, for a FENCE.I, after which any page of any
 * address space may hold new instructions.
 */
void jit_flush(riscv_t *rv);

/* Retire live native entries in every retained address space. */
void jit_retire_all(struct jit_state *state);

/* Whether live code translated for any address space starts in the page of
 * va; retired code does not count. Without block chaining, blocks may span
 * pages, so any live code counts whatever va is.
 */
bool jit_page_has_code_any(const struct jit_state *state, uint32_t va);
#endif
typedef void (*exec_block_func_t)(riscv_t *rv, uintptr_t);

#if RV32_HAS(T2C)
#define T2C_PROFILE_THRESHOLD 64
bool t2c_compile(riscv_t *, block_t *, const char *);
bool t2c_aot_init(riscv_t *);
void t2c_aot_bind(riscv_t *, block_t *);
void t2c_aot_exit(riscv_t *);
void t2c_aot_checkpoint(riscv_t *);
#if RV32_HAS(SYSTEM)
struct aot_entry;
const struct aot_entry *t2c_aot_has_pc(riscv_t *);
void *t2c_aot_lookup(riscv_t *, const struct aot_entry *);
uint64_t t2c_aot_page_hash(riscv_t *, block_t *);
#endif
void t2c_aot_collect_block(riscv_t *, block_t *);
void t2c_aot_invalidate(riscv_t *);
typedef void (*exec_t2c_func_t)(riscv_t *);

/* The jit-cache records the program counters and the entries of executable
 * instructions generated by T2C. Like hardware cache, the old jit-cache will be
 * replaced by the new one which uses the same slot.
 */

/* The size of jit-cache table should be the power of 2, thus, we can easily
 * access the element by masking the program counter.
 */
#define JIT_CACHE_INDEX_BITS 12
#define N_JIT_CACHE_ENTRIES (1 << JIT_CACHE_INDEX_BITS)
#define JIT_CACHE_HASH_MULTIPLIER UINT32_C(2654435761)

/* Slot of the jit-cache entry for pc in address space satp (0 outside system
 * mode). t2c_jit_cache_helper() computes the same index in LLVM IR for the
 * lookup in generated code, so the two must change together: an entry stored
 * under any other index is never found, and every indirect jump falls back to
 * the dispatcher.
 */
static inline uint32_t jit_cache_slot(uint32_t pc, uint32_t satp)
{
#if RV32_HAS(SYSTEM)
    /* Use high product bits: masking a folded PC leaves recurring kernel
     * entry pairs in the same slot even with a substantially larger table.
     */
    return ((pc ^ satp) * JIT_CACHE_HASH_MULTIPLIER) >>
           (32 - JIT_CACHE_INDEX_BITS);
#else
    return (pc ^ (pc >> 12) ^ satp) & (N_JIT_CACHE_ENTRIES - 1);
#endif
}

/* Inline cache for fast-path indirect jump resolution.
 * Stores the most recently used (target, entry) pair per call site.
 * Hit rate is typically >90% for stable branch patterns (returns, vtables).
 *
 * Thread model: The inline cache is per-riscv_t instance and accessed only
 * by the main execution thread. No synchronization needed - single writer/
 * reader pattern. Artifact binding also runs on that execution thread.
 *
 * Memory model: Non-atomic loads/stores are used because:
 * 1. Single-threaded access within each riscv_t instance
 * 2. T2C requires 64-bit host (static_assert below) ensuring atomic 64-bit
 *    loads - no torn reads possible for the key field
 *
 * satp stability: In system mode, the key includes the address space whose
 * code map records the page: the current satp, or SATP_SHARED for kernel code
 * reached through global mappings, which kernel graphs look up directly.
 * satp changes only via CSR writes, which are serialized with execution.
 *
 * Native binding lifetime:
 * - SFENCE.VMA code retirement clears the entries of the retired address
 *   spaces, and all entries when every space retires (emulate.c).
 * - Decoded block eviction retains validated native bindings.
 * - FENCE.I / code-cache flush clears all entries (jit.c).
 *
 * On cache hit, ISB is skipped on ARM64 since we already executed this target
 * successfully - the instruction cache was coherent at that time.
 */
#define N_INLINE_CACHE_ENTRIES (1 << 10)

struct inline_cache {
    uint64_t key; /* target PC (+ satp<<32 in system mode), 0 = empty */
    void *entry;  /* cached function pointer */
#if RV32_HAS(SYSTEM)
    uint32_t priv_mode; /* fetch privilege of the compiled function */
#endif
};

/* Slot of the inline cache entry for pc in address space satp (0 outside system
 * mode). The inline cache is direct-mapped, and generated code
 * (t2c_jit_cache_helper) computes the same index, so a key can only ever live
 * in this one slot.
 */
static inline uint32_t inline_cache_slot(uint32_t pc, uint32_t satp)
{
    return (pc ^ (pc >> 12) ^ satp) & (N_INLINE_CACHE_ENTRIES - 1);
}

/* Verify inline_cache struct layout for LLVM IR generation.
 * LLVM type: { i64 key, ptr entry [, i32 priv_mode] }
 * Offsets: key=0, entry=8
 *
 * T2C requires 64-bit host to ensure atomic 64-bit key loads (no torn reads).
 * On 32-bit hosts, non-atomic 64-bit loads could see partial updates.
 */
static_assert(sizeof(void *) == 8,
              "T2C inline cache requires 64-bit host for atomic key loads");
static_assert(offsetof(struct inline_cache, key) == 0,
              "inline_cache.key must be at offset 0");
static_assert(offsetof(struct inline_cache, entry) == 8,
              "inline_cache.entry must be at offset 8");
#if RV32_HAS(SYSTEM)
static_assert(offsetof(struct inline_cache, priv_mode) == 16,
              "inline_cache privilege must match LLVM field 2");
static_assert(sizeof(struct inline_cache) == 24, "system inline cache ABI");
#else
static_assert(sizeof(struct inline_cache) == 16, "user inline cache ABI");
#endif

struct inline_cache *inline_cache_init(void);
void inline_cache_exit(struct inline_cache *cache);
void inline_cache_clear(struct inline_cache *cache);

/* jit_cache entry for T2C compiled code lookup.
 * The hart thread alone writes and reads it. Writes still follow a seqlock
 * pattern (seq odd while key, entry and privilege change) that generated
 * code checks, which keeps the artifact ABI unchanged.
 */
struct jit_cache {
    uint32_t seq; /* sequence counter: odd = write in progress, even = stable */
#if RV32_HAS(SYSTEM)
    uint32_t priv_mode; /* occupies the existing four-byte alignment gap */
#endif
    uint64_t key; /* program counter, composed to satp in system simulation */
    void *entry;  /* entry of JIT-ed code */
};

/* Verify struct layout requirements for lock-free seqlock reads and LLVM IR.
 *
 * The jit_cache struct layout must match the LLVM type definition in t2c.c:
 *   LLVM: { i32 seq, i32 priv_or_pad, i64 key, ptr entry }
 *   Offsets: seq=0, pad=4, key=8, entry=16 (on 64-bit)
 *
 * The key field must be 8-byte aligned for atomic 64-bit loads without
 * requiring libatomic. The entry field must be pointer-aligned.
 *
 * If this struct layout changes, the LLVM type in t2c.c must be updated to
 * match (search for "jit_cache_memb" in t2c.c).
 */
#if RV32_HAS(SYSTEM)
static_assert(offsetof(struct jit_cache, priv_mode) == 4,
              "jit_cache privilege must match LLVM field 1");
#endif
static_assert(sizeof(struct jit_cache) == 24, "jit_cache ABI must match LLVM");
static_assert(offsetof(struct jit_cache, seq) == 0,
              "jit_cache.seq must be at offset 0 to match LLVM IR");
static_assert(offsetof(struct jit_cache, key) == 8,
              "jit_cache.key must be at offset 8 to match LLVM IR");
static_assert(offsetof(struct jit_cache, entry) == 16,
              "jit_cache.entry must be at offset 16 to match LLVM IR");
static_assert(offsetof(struct jit_cache, key) % 8 == 0,
              "jit_cache.key must be 8-byte aligned for atomic loads");
static_assert(offsetof(struct jit_cache, entry) % sizeof(void *) == 0,
              "jit_cache.entry must be pointer-aligned for atomic loads");

struct jit_cache *jit_cache_init(void);
void jit_cache_exit(struct jit_cache *cache);
void jit_cache_update(struct jit_cache *cache,
                      uint64_t key,
                      void *entry,
                      uint32_t priv_mode);
void jit_cache_clear(struct jit_cache *cache);
#if RV32_HAS(SYSTEM)
/* Drop the bindings recorded under address space satp. */
void jit_cache_retire_space(struct jit_cache *cache,
                            struct inline_cache *inline_cache,
                            uint32_t satp);
#endif
#endif
