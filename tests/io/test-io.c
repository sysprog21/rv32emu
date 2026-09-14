/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* Regression tests for guest memory bounds checking.
 *
 * A guest address is a 32 bit number chosen by the guest, so every bulk
 * accessor has to prove the range lies inside the arena before touching it.
 * memory_read() once did not, which let a guest read host memory past the end
 * of the arena and hand it back through write(2).
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "io.h"

#define ARENA_SIZE (1 * 1024 * 1024)
#define PATTERN 0xA5

/* Report whether every byte of the buffer was cleared, which is what a refused
 * read must leave behind: never stale host bytes.
 */
static bool all_zero(const uint8_t *buf, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (buf[i])
            return false;
    }
    return true;
}

int main(void)
{
    memory_t *mem = memory_new(ARENA_SIZE);
    assert(mem);
    assert(mem->mem_size == ARENA_SIZE);

    uint8_t src[64];
    memset(src, PATTERN, sizeof(src));
    assert(memory_write(mem, 0, src, sizeof(src)));

    uint8_t dst[64];

    /* an in-range read still returns the data */
    memset(dst, 0, sizeof(dst));
    assert(memory_read(mem, dst, 0, sizeof(dst)));
    assert(!memcmp(dst, src, sizeof(dst)));

    /* a read starting past the arena is refused and clears the destination */
    memset(dst, PATTERN, sizeof(dst));
    assert(!memory_read(mem, dst, ARENA_SIZE, sizeof(dst)));
    assert(all_zero(dst, sizeof(dst)));

    /* so is one that starts inside but runs off the end */
    memset(dst, PATTERN, sizeof(dst));
    assert(!memory_read(mem, dst, ARENA_SIZE - 16, sizeof(dst)));
    assert(all_zero(dst, sizeof(dst)));

    /* the last byte in the arena is still readable */
    assert(memory_read(mem, dst, ARENA_SIZE - 1, 1));

    /* addr + size must not be allowed to wrap back into range */
    memset(dst, PATTERN, sizeof(dst));
    assert(!memory_read(mem, dst, 0xFFFFFFF0, sizeof(dst)));
    assert(all_zero(dst, sizeof(dst)));

    /* writes and fills reject the same ranges */
    assert(!memory_write(mem, ARENA_SIZE, src, sizeof(src)));
    assert(!memory_write(mem, ARENA_SIZE - 16, src, sizeof(src)));
    assert(!memory_write(mem, 0xFFFFFFF0, src, sizeof(src)));
    assert(!memory_fill(mem, ARENA_SIZE - 16, sizeof(src), 0));
    assert(!memory_fill(mem, 0xFFFFFFF0, sizeof(src), 0));

    memory_delete(mem);
    printf("io: guest memory bounds enforced\n");
    return 0;
}
