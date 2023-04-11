/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "io.h"
#include "mpool.h"

static const uint32_t mask_lo = 0xffff;
static const uint32_t mask_hi = ~(0xffff);

static struct mpool *mp;

memory_t *memory_new()
{
    memory_t *m = calloc(1, sizeof(memory_t));
    /* Initialize the mpool size to sizeof(chunk_t) << 2, and it will extend
     * automatically if needed */
    mp = mpool_create(sizeof(chunk_t) << 2, sizeof(chunk_t));
    return m;
}

void memory_delete(memory_t *m)
{
    if (!m)
        return;
    mpool_destory(mp);
    free(m);
}

void memory_read(memory_t *m, uint8_t *dst, uint32_t addr, uint32_t size)
{
    /* test if this read is entirely within one chunk */
    if ((addr & mask_hi) == ((addr + size) & mask_hi)) {
        chunk_t *c;
        if ((c = m->chunks[addr >> 16])) {
            /* get the subchunk pointer */
            const uint32_t p = (addr & mask_lo);

            /* copy over the data */
            memcpy(dst, c->data + p, size);
        } else {
            memset(dst, 0, size);
        }
    } else {
        /* naive copy */
        for (uint32_t i = 0; i < size; ++i) {
            uint32_t p = addr + i;
            chunk_t *c = m->chunks[p >> 16];
            dst[i] = c ? c->data[p & 0xffff] : 0;
        }
    }
}

uint32_t memory_read_str(memory_t *m, uint8_t *dst, uint32_t addr, uint32_t max)
{
    uint32_t len = 0;
    const uint8_t *end = dst + max;
    for (;; ++len, ++dst) {
        uint8_t ch = 0;
        memory_read(m, &ch, addr + len, 1);
        if (dst < end)
            *dst = ch;
        if (!ch)
            break;
    }
    return len + 1;
}

uint32_t memory_ifetch(memory_t *m, uint32_t addr)
{
    const uint32_t addr_lo = addr & mask_lo;
    assert((addr_lo & 1) == 0);

    chunk_t *c = m->chunks[addr >> 16];
    assert(c);
    return *(const uint32_t *) (c->data + addr_lo);
}

uint32_t memory_read_w(memory_t *m, uint32_t addr)
{
    const uint32_t addr_lo = addr & mask_lo;
    chunk_t *c = m->chunks[addr >> 16];
    return *(const uint32_t *) (c->data + addr_lo);
}

uint16_t memory_read_s(memory_t *m, uint32_t addr)
{
    const uint32_t addr_lo = addr & mask_lo;
    chunk_t *c = m->chunks[addr >> 16];
    return *(const uint16_t *) (c->data + addr_lo);
}

uint8_t memory_read_b(memory_t *m, uint32_t addr)
{
    chunk_t *c = m->chunks[addr >> 16];
    return c->data[addr & mask_lo];
}

void memory_write(memory_t *m, uint32_t addr, const uint8_t *src, uint32_t size)
{
    for (uint32_t i = 0; i < size; ++i) {
        const uint32_t addr_lo = (addr + i) & mask_lo,
                       addr_hi = (addr + i) >> 16;
        chunk_t *c = m->chunks[addr_hi];
        if (!c) {
            c = mpool_calloc(mp);
            m->chunks[addr_hi] = c;
        }
        c->data[addr_lo] = src[i];
    }
}

#define MEM_WRITE_IMPL(size, type)                                           \
    void memory_write_##size(memory_t *m, uint32_t addr, const uint8_t *src) \
    {                                                                        \
        const uint32_t addr_lo = addr & mask_lo, addr_hi = addr >> 16;       \
        chunk_t *c = m->chunks[addr_hi];                                     \
        if (unlikely(!c)) {                                                  \
            c = mpool_calloc(mp);                                            \
            m->chunks[addr_hi] = c;                                          \
        }                                                                    \
        *(type *) (c->data + addr_lo) = *(const type *) src;                 \
    }

MEM_WRITE_IMPL(w, uint32_t);
MEM_WRITE_IMPL(s, uint16_t);
MEM_WRITE_IMPL(b, uint8_t);

void memory_fill(memory_t *m, uint32_t addr, uint32_t size, uint8_t val)
{
    for (uint32_t i = 0; i < size; ++i) {
        uint32_t p = addr + i;
        uint32_t x = p >> 16;
        chunk_t *c = m->chunks[x];
        if (!c) {
            c = mpool_calloc(mp);
            m->chunks[x] = c;
        }
        c->data[p & 0xffff] = val;
    }
}
