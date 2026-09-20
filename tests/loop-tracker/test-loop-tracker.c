#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "utils.h"

HASH_FUNC_IMPL(test_hash, SET_SIZE_BITS, SET_SIZE);

static loop_tracker_t tracker;

int main(void)
{
    /* Zero is a valid PC, including before the first reset. */
    assert(!loop_tracker_seen(&tracker, 0));
    assert(loop_tracker_seen(&tracker, 0));
    for (unsigned i = 0; i < 100; i++) {
        loop_tracker_reset(&tracker);
        assert(!loop_tracker_seen(&tracker, 0));
        assert(loop_tracker_seen(&tracker, 0));
    }

    rv_hash_key_t keys[SET_SLOTS_SIZE + 1];
    unsigned count = 0;
    for (rv_hash_key_t key = 0; count < SET_SLOTS_SIZE + 1; key++) {
        if (test_hash(key) == 0)
            keys[count++] = key;
    }
    loop_tracker_reset(&tracker);
    for (unsigned i = 0; i < SET_SLOTS_SIZE; i++) {
        assert(!loop_tracker_seen(&tracker, keys[i]));
        assert(loop_tracker_seen(&tracker, keys[i]));
    }
    /* Overflow must not report a loop or overwrite earlier observations. */
    assert(!loop_tracker_seen(&tracker, keys[SET_SLOTS_SIZE]));
    assert(!loop_tracker_seen(&tracker, keys[SET_SLOTS_SIZE]));
    for (unsigned i = 0; i < SET_SLOTS_SIZE; i++)
        assert(loop_tracker_seen(&tracker, keys[i]));
    loop_tracker_reset(&tracker);
    assert(!loop_tracker_seen(&tracker, keys[SET_SLOTS_SIZE]));
    assert(loop_tracker_seen(&tracker, keys[SET_SLOTS_SIZE]));

    /* Wrapping must invalidate ancient epoch-one buckets too. */
    memset(&tracker, 0, sizeof(tracker));
    loop_tracker_reset(&tracker);
    assert(!loop_tracker_seen(&tracker, 0));
    tracker.epoch = UINT32_MAX;
    assert(!loop_tracker_seen(&tracker, 1));
    loop_tracker_reset(&tracker);
    assert(tracker.epoch == 1);
    assert(!loop_tracker_seen(&tracker, 0));
    assert(!loop_tracker_seen(&tracker, 1));
    assert(loop_tracker_seen(&tracker, 0));
    assert(loop_tracker_seen(&tracker, 1));

#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
    /* Address-space bits must remain part of the key. */
    loop_tracker_reset(&tracker);
    assert(!loop_tracker_seen(&tracker, UINT64_C(0x100001234)));
    assert(!loop_tracker_seen(&tracker, UINT64_C(0x200001234)));
    assert(loop_tracker_seen(&tracker, UINT64_C(0x100001234)));
    assert(loop_tracker_seen(&tracker, UINT64_C(0x200001234)));
#endif
    return 0;
}
