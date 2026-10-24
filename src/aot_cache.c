/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <stdlib.h>
#include "jit.h"

#include "riscv_private.h"

/* Native entries bound from an AOT artifact. Only the execution thread
 * publishes them; generated code reads the tables directly, so their layout
 * must match the LLVM types in t2c.c.
 */

struct jit_cache *jit_cache_init(void)
{
    return calloc(N_JIT_CACHE_ENTRIES, sizeof(struct jit_cache));
}

void jit_cache_exit(struct jit_cache *cache)
{
    free(cache);
}

struct inline_cache *inline_cache_init(void)
{
    return calloc(N_INLINE_CACHE_ENTRIES, sizeof(struct inline_cache));
}

void inline_cache_exit(struct inline_cache *cache)
{
    free(cache);
}

/* Clear all inline cache entries, when code is retired or the emulator resets.
 * No seqlock needed - only the execution thread reads/writes inline cache.
 */
void inline_cache_clear(struct inline_cache *cache)
{
    memset(cache, 0, N_INLINE_CACHE_ENTRIES * sizeof(struct inline_cache));
}

/* Seqlock write pattern:
 * 1. Increment seq to odd (signals write in progress)
 * 2. Write entry, key and privilege (atomic to avoid a data race with readers)
 * 3. Increment seq to even (signals write complete)
 * Release ordering on seq ensures readers see consistent state.
 */
static void jit_cache_store(struct jit_cache *slot,
                            uint64_t key,
                            void *entry,
                            uint32_t priv_mode UNUSED)
{
    uint32_t seq = ATOMIC_LOAD(&slot->seq, ATOMIC_RELAXED);
    ATOMIC_STORE(&slot->seq, seq + 1, ATOMIC_RELEASE); /* odd = writing */
    ATOMIC_STORE(&slot->entry, entry, ATOMIC_RELEASE);
    ATOMIC_STORE(&slot->key, key, ATOMIC_RELEASE);
#if RV32_HAS(SYSTEM)
    ATOMIC_STORE(&slot->priv_mode, priv_mode, ATOMIC_RELEASE);
#endif
    ATOMIC_STORE(&slot->seq, seq + 2, ATOMIC_RELEASE); /* even = done */
}

void jit_cache_update(struct jit_cache *cache,
                      uint64_t key,
                      void *entry,
                      uint32_t priv_mode)
{
    /* The key packs satp above pc. Mixing satp into the slot spreads entries
     * from different address spaces across the table.
     */
    uint32_t pos = jit_cache_slot((uint32_t) key, (uint32_t) (key >> 32));
    jit_cache_store(&cache[pos], key, entry, priv_mode);
}

void jit_cache_clear(struct jit_cache *cache)
{
    for (uint32_t i = 0; i < N_JIT_CACHE_ENTRIES; i++)
        jit_cache_store(&cache[i], 0, NULL, 0);
}

#if RV32_HAS(SYSTEM)
void jit_cache_retire_space(struct jit_cache *cache,
                            struct inline_cache *inline_cache,
                            uint32_t satp)
{
    for (uint32_t i = 0; i < N_JIT_CACHE_ENTRIES; i++) {
        if (cache[i].entry && (uint32_t) (cache[i].key >> 32) == satp)
            jit_cache_store(&cache[i], 0, NULL, 0);
    }
    for (uint32_t i = 0; i < N_INLINE_CACHE_ENTRIES; i++) {
        if (inline_cache[i].entry &&
            (uint32_t) (inline_cache[i].key >> 32) == satp)
            inline_cache[i] = (struct inline_cache) {0};
    }
}

void jit_cache_retire_unshared(struct jit_cache *cache,
                               struct inline_cache *inline_cache)
{
    for (uint32_t i = 0; i < N_JIT_CACHE_ENTRIES; i++) {
        if (cache[i].entry && (uint32_t) (cache[i].key >> 32) != SATP_SHARED)
            jit_cache_store(&cache[i], 0, NULL, 0);
    }
    for (uint32_t i = 0; i < N_INLINE_CACHE_ENTRIES; i++) {
        if (inline_cache[i].entry &&
            (uint32_t) (inline_cache[i].key >> 32) != SATP_SHARED)
            inline_cache[i] = (struct inline_cache) {0};
    }
}
#endif
