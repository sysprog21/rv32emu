#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "utils.h"

HASH_FUNC_IMPL(test_hash, SET_SIZE_BITS, SET_SIZE);

static set_t probe_set;

int main(void)
{
    /* Zero is a valid PC, including before the first reset. */
    assert(!set_probe(&probe_set, 0));
    assert(set_probe(&probe_set, 0));
    for (unsigned i = 0; i < 100; i++) {
        set_reset(&probe_set);
        assert(!set_probe(&probe_set, 0));
        assert(set_probe(&probe_set, 0));
    }

    rv_hash_key_t keys[SET_SLOTS_SIZE + 1];
    unsigned count = 0;
    for (rv_hash_key_t key = 0; count < SET_SLOTS_SIZE + 1; key++) {
        if (test_hash(key) == 0)
            keys[count++] = key;
    }
    set_reset(&probe_set);
    for (unsigned i = 0; i < SET_SLOTS_SIZE; i++) {
        assert(!set_probe(&probe_set, keys[i]));
        assert(set_probe(&probe_set, keys[i]));
    }
    /* Overflow must not report a loop or overwrite earlier observations. */
    assert(!set_probe(&probe_set, keys[SET_SLOTS_SIZE]));
    assert(!set_probe(&probe_set, keys[SET_SLOTS_SIZE]));
    for (unsigned i = 0; i < SET_SLOTS_SIZE; i++)
        assert(set_probe(&probe_set, keys[i]));
    set_reset(&probe_set);
    assert(!set_probe(&probe_set, keys[SET_SLOTS_SIZE]));
    assert(set_probe(&probe_set, keys[SET_SLOTS_SIZE]));

    /* Wrapping must invalidate ancient epoch-one buckets too. */
    memset(&probe_set, 0, sizeof(probe_set));
    set_reset(&probe_set);
    assert(!set_probe(&probe_set, 0));
    probe_set.epoch = UINT32_MAX;
    assert(!set_probe(&probe_set, 1));
    set_reset(&probe_set);
    assert(probe_set.epoch == 1);
    assert(!set_probe(&probe_set, 0));
    assert(!set_probe(&probe_set, 1));
    assert(set_probe(&probe_set, 0));
    assert(set_probe(&probe_set, 1));

#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
    /* Address-space bits must remain part of the key. */
    set_reset(&probe_set);
    assert(!set_probe(&probe_set, UINT64_C(0x100001234)));
    assert(!set_probe(&probe_set, UINT64_C(0x200001234)));
    assert(set_probe(&probe_set, UINT64_C(0x100001234)));
    assert(set_probe(&probe_set, UINT64_C(0x200001234)));
#endif
    /* set_add/set_has share the layout but keep strict semantics: a key is
     * stored exactly once, and zero is storable like any other value.
     */
    set_t strict;
    set_init(&strict);
    assert(!set_has(&strict, 0));
    assert(set_add(&strict, 0));
    assert(set_has(&strict, 0));
    assert(!set_add(&strict, 0));

    assert(!set_has(&strict, 12345));
    assert(set_add(&strict, 12345));
    assert(set_has(&strict, 12345));
    assert(set_has(&strict, 0));

    /* A reset retires both keys without touching the table. */
    set_reset(&strict);
    assert(!set_has(&strict, 0));
    assert(!set_has(&strict, 12345));
    assert(set_add(&strict, 0));

    return 0;
}
