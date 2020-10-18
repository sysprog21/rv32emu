#pragma once

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

struct memory_t {
    struct chunk_t {
        uint8_t data[0x10000];
    };

    memory_t() { memset(chunks, 0, sizeof(chunks)); }

    ~memory_t() { clear(); }

    // read a c-string from memory
    uint32_t read_str(uint8_t *dst, uint32_t addr, uint32_t max)
    {
        uint32_t len = 0;
        const uint8_t *end = dst + max;
        for (;; ++len, ++dst) {
            uint8_t ch = 0;
            read(&ch, addr + len, 1);
            if (dst < end)
                *dst = ch;
            if (!ch)
                break;
        }
        return len + 1;
    }

    // read an instruction from memory
    uint32_t read_ifetch(uint32_t addr)
    {
        const uint32_t addr_lo = addr & mask_lo;
        assert((addr_lo & 3) == 0);
        chunk_t *c = chunks[addr >> 16];
        assert(c);
        return *(const uint32_t *) (c->data + addr_lo);
    }

    // read a word from memory
    uint32_t read_w(uint32_t addr)
    {
        const uint32_t addr_lo = addr & mask_lo;
        // test if this is within one chunk
        if (addr_lo <= 0xfffc) {
            // get the chunk
            if (chunk_t *c = chunks[addr >> 16])
                return *(const uint32_t *) (c->data + addr_lo);
            return 0u;
        }
        uint32_t dst = 0;
        read((uint8_t *) &dst, addr, 4);
        return dst;
    }

    // read a short from memory
    uint16_t read_s(uint32_t addr)
    {
        const uint32_t addr_lo = addr & mask_lo;
        // test if this is within one chunk
        if (addr_lo <= 0xfffe) {
            // get the chunk
            if (chunk_t *c = chunks[addr >> 16])
                return *(const uint16_t *) (c->data + addr_lo);
            return 0u;
        }
        uint16_t dst = 0;
        read((uint8_t *) &dst, addr, 2);
        return dst;
    }

    // read a byte from memory
    uint8_t read_b(uint32_t addr)
    {
        // get the chunk
        if (chunk_t *c = chunks[addr >> 16])
            return *(c->data + (addr & 0xffff));
        return 0u;
    }

    // read a length of data from memory
    void read(uint8_t *dst, uint32_t addr, uint32_t size)
    {
        // if this read is entirely within one chunk
        if ((addr & mask_hi) == ((addr + size) & mask_hi)) {
            // get the chunk
            if (chunk_t *c = chunks[addr >> 16]) {
                // get the subchunk pointer
                const uint32_t p = (addr & mask_lo);
                // copy over the data
                memcpy(dst, c->data + p, size);
            } else {
                memset(dst, 0, size);
            }
        } else {
            // naive copy
            for (uint32_t i = 0; i < size; ++i) {
                uint32_t p = addr + i;
                chunk_t *c = chunks[p >> 16];
                dst[i] = c ? c->data[p & 0xffff] : 0;
            }
        }
    }

    void write(uint32_t addr, const uint8_t *src, uint32_t size)
    {
        for (uint32_t i = 0; i < size; ++i) {
            uint32_t p = addr + i;
            uint32_t x = p >> 16;
            chunk_t *c = chunks[x];
            if (c == nullptr) {
                c = new chunk_t;
                memset(c->data, 0, sizeof(c->data));
                chunks[x] = c;
            }
            c->data[p & 0xffff] = src[i];
        }
    }

    void fill(uint32_t addr, uint32_t size, uint8_t val)
    {
        for (uint32_t i = 0; i < size; ++i) {
            uint32_t p = addr + i;
            uint32_t x = p >> 16;
            chunk_t *c = chunks[x];
            if (c == nullptr) {
                c = new chunk_t;
                memset(c->data, 0, sizeof(c->data));
                chunks[x] = c;
            }
            c->data[p & 0xffff] = val;
        }
    }

    void clear()
    {
        for (chunk_t *c : chunks) {
            if (c)
                delete c;
        }
        memset(chunks, 0, sizeof(chunks));
    }

protected:
    static const uint32_t mask_lo = 0xffff;
    static const uint32_t mask_hi = ~mask_lo;

    chunk_t *chunks[0x10000];
};
