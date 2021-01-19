#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "io.h"

static const uint32_t mask_lo = 0xffff;
static const uint32_t mask_hi = ~(0xffff);

memory_t *memory_new()
{
    memory_t *m = malloc(sizeof(memory_t));
    memset(m->chunks, 0, sizeof(m->chunks));
    return m;
}

void memory_delete(memory_t *m)
{
    if (!m)
        return;

    for (uint32_t i = 0; i < (sizeof(m->chunks) / sizeof(chunk_t *)); i++) {
        chunk_t *c = m->chunks[i];
        if (c)
            free(c);
    }
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

uint32_t memory_read_ifetch(memory_t *m, uint32_t addr)
{
    const uint32_t addr_lo = addr & mask_lo;
    #ifdef ENABLE_RV32C
        assert((addr_lo & 1) == 0);
    #else
        assert((addr_lo & 3) == 0);
    #endif

    chunk_t *c = m->chunks[addr >> 16];
    assert(c);
    return *(const uint32_t *) (c->data + addr_lo);
}

uint32_t memory_read_w(memory_t *m, uint32_t addr)
{
    const uint32_t addr_lo = addr & mask_lo;
    if (addr_lo <= 0xfffc) { /* test if this is within one chunk */
        chunk_t *c;
        if ((c = m->chunks[addr >> 16]))
            return *(const uint32_t *) (c->data + addr_lo);
        return 0u;
    }
    uint32_t dst = 0;
    memory_read(m, (uint8_t *) &dst, addr, 4);
    return dst;
}

uint16_t memory_read_s(memory_t *m, uint32_t addr)
{
    const uint32_t addr_lo = addr & mask_lo;
    if (addr_lo <= 0xfffe) { /* test if this is within one chunk */
        chunk_t *c;
        if ((c = m->chunks[addr >> 16]))
            return *(const uint16_t *) (c->data + addr_lo);
        return 0u;
    }
    uint16_t dst = 0;
    memory_read(m, (uint8_t *) &dst, addr, 2);
    return dst;
}

uint8_t memory_read_b(memory_t *m, uint32_t addr)
{
    chunk_t *c;
    if ((c = m->chunks[addr >> 16]))
        return *(c->data + (addr & 0xffff));
    return 0u;
}

void memory_write(memory_t *m, uint32_t addr, const uint8_t *src, uint32_t size)
{
    for (uint32_t i = 0; i < size; ++i) {
        uint32_t p = addr + i;
        uint32_t x = p >> 16;
        chunk_t *c = m->chunks[x];
        if (!c) {
            c = malloc(sizeof(chunk_t));
            memset(c->data, 0, sizeof(c->data));
            m->chunks[x] = c;
        }
        c->data[p & 0xffff] = src[i];
    }
}

void memory_fill(memory_t *m, uint32_t addr, uint32_t size, uint8_t val)
{
    for (uint32_t i = 0; i < size; ++i) {
        uint32_t p = addr + i;
        uint32_t x = p >> 16;
        chunk_t *c = m->chunks[x];
        if (!c) {
            c = malloc(sizeof(chunk_t));
            memset(c->data, 0, sizeof(c->data));
            m->chunks[x] = c;
        }
        c->data[p & 0xffff] = val;
    }
}
