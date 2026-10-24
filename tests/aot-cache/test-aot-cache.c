/* Exercise the native binding cache against a full-table reference model. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "jit.h"
#include "riscv_private.h"

static uint32_t random_state = 1;

static uint32_t next_random(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

int main(void)
{
    struct jit_cache *cache = jit_cache_init();
    assert(cache);
    struct inline_cache *inline_cache = inline_cache_init();
    assert(inline_cache);
    static uint64_t keys[N_JIT_CACHE_ENTRIES];
    static void *entries[N_JIT_CACHE_ENTRIES];
    static uint32_t privileges[N_JIT_CACHE_ENTRIES];
    static unsigned targets[16];
    /* Include SATP zero, different SATPs sharing a slot, and SATPs differing
     * in high bits. Exact keys must distinguish all of them.
     */
    const uint32_t spaces[] = {0,          1,          64,         128,
                               0x80000001, 0x80000400, 0x80000801, 0xffffffff,
                               0x80400001, 0x80400400, 0x80800001};
    for (unsigned step = 0; step < 100000; step++) {
        if (next_random() % 64 == 0) {
            jit_cache_clear(cache);
            memset(keys, 0, sizeof(keys));
            memset(entries, 0, sizeof(entries));
            memset(privileges, 0, sizeof(privileges));
        } else {
            uint32_t satp = spaces[next_random() % ARRAY_SIZE(spaces)];
            uint32_t pc = (next_random() % 256) * 2 |
                          (next_random() % 16) << 12 |
                          (next_random() % 128) << 22;
            uint64_t key = pc | ((uint64_t) satp << 32);
            void *entry = &targets[next_random() % ARRAY_SIZE(targets)];
            uint32_t priv = next_random() % 4;
            unsigned slot = jit_cache_slot(pc, satp);
            jit_cache_update(cache, key, entry, priv);
            keys[slot] = key;
            entries[slot] = entry;
            privileges[slot] = priv;
        }
        for (unsigned i = 0; i < N_JIT_CACHE_ENTRIES; i++) {
            assert(cache[i].key == keys[i]);
            assert(cache[i].entry == entries[i]);
#if RV32_HAS(SYSTEM)
            assert(cache[i].priv_mode == privileges[i]);
#endif
            assert(!(cache[i].seq & 1));
        }
    }
    inline_cache[1].key = 1;
    inline_cache[1].entry = &targets[0];
    inline_cache_clear(inline_cache);
    for (unsigned i = 0; i < N_INLINE_CACHE_ENTRIES; i++)
        assert(!inline_cache[i].key && !inline_cache[i].entry);
    jit_cache_exit(cache);
    inline_cache_exit(inline_cache);
    puts("AOT cache replacement and invalidation passed");
    return 0;
}
