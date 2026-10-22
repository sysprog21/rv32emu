/* Optional JIT diagnostics. Disabled builds emit no counter updates. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "feature.h"

#ifndef RV32_T1C_STATS
#define RV32_T1C_STATS 0
#endif

#if RV32_T1C_STATS && RV32_HAS(JIT)
#define RV32_T1C_STATS_ENABLED 1
#define T1C_STATS_FIELDS(_)  \
    _(dispatches)            \
    _(lookup_hits)           \
    _(lookup_cold)           \
    _(lookup_priv)           \
    _(lookup_satp)           \
    _(lookup_invalidated)    \
    _(decoded_kernel)        \
    _(decoded_user)          \
    _(flushed_native_blocks) \
    _(decoded_blocks)        \
    _(native_entries)        \
    _(cached_native_entries) \
    _(compile_requests)      \
    _(interp_pc_syncs)       \
    _(hot_interp_pc_syncs)   \
    _(trap_instructions)     \
    _(t2c_entries)           \
    _(tlb_ctx_changes)       \
    _(satp_writes)           \
    _(sfence_all)            \
    _(sfence_page)           \
    _(fence_i)               \
    _(fence_i_blocks)        \
    _(retire_calls)          \
    _(retire_pending_slots)  \
    _(pending_patched)       \
    _(lazy_fused_loads)

struct t1c_stats {
#define T1C_STATS_FIELD(name) uint64_t name;
    T1C_STATS_FIELDS(T1C_STATS_FIELD)
#undef T1C_STATS_FIELD
    bool reported;
    uint64_t trap_insns[64]; /* trap-interpreted instructions per cause */
    uint64_t traps[64];      /* traps taken per T1C_CAUSE_INDEX */
};
extern struct t1c_stats rv_t1c_stats;
/* Indexed by decoded opcode. Only the emulator thread updates these arrays. */
extern uint64_t rv_t1c_blockers_decoded[];
extern uint64_t rv_t1c_blockers_entries[];
extern uint64_t rv_t1c_blockers_insns[];
void rv_t1c_stats_report(uint64_t guest_cycles);
#define T1C_COUNT(name, n) (rv_t1c_stats.name += (uint64_t) (n))
/* Exceptions take slots 0-31 and interrupts 32-63. */
#define T1C_CAUSE_INDEX(cause) ((((cause) >> 31) << 5) | ((cause) & 31))
#else
#define RV32_T1C_STATS_ENABLED 0
#define T1C_COUNT(name, n) ((void) 0)
#endif
