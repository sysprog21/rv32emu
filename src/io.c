/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if HAVE_MMAP
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "io.h"

static uint8_t *data_memory_base;

memory_t *memory_new(uint32_t size)
{
    if (!size)
        return NULL;

    memory_t *mem = malloc(sizeof(memory_t));
    assert(mem);
#if HAVE_MMAP
    data_memory_base = mmap(NULL, size, PROT_READ | PROT_WRITE,
                            MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (data_memory_base == MAP_FAILED) {
        free(mem);
        return NULL;
    }
#else
    data_memory_base = malloc(size);
    if (!data_memory_base) {
        free(mem);
        return NULL;
    }
#endif
    mem->mem_base = data_memory_base;
    mem->mem_size = size;
    return mem;
}

void memory_delete(memory_t *mem)
{
#if HAVE_MMAP
    munmap(mem->mem_base, mem->mem_size);
#else
    free(mem->mem_base);
#endif
    free(mem);
}

void memory_read(const memory_t *mem,
                 uint8_t *dst,
                 uint32_t addr,
                 uint32_t size)
{
    memcpy(dst, mem->mem_base + addr, size);
}

uint32_t memory_ifetch(uint32_t addr)
{
    return *(const uint32_t *) (data_memory_base + addr);
}

#define MEM_READ_IMPL(size, type)                   \
    type memory_read_##size(uint32_t addr)          \
    {                                               \
        return *(type *) (data_memory_base + addr); \
    }

MEM_READ_IMPL(w, uint32_t)
MEM_READ_IMPL(s, uint16_t)
MEM_READ_IMPL(b, uint8_t)

#define MEM_WRITE_IMPL(size, type)                                 \
    void memory_write_##size(uint32_t addr, const uint8_t *src)    \
    {                                                              \
        *(type *) (data_memory_base + addr) = *(const type *) src; \
    }

MEM_WRITE_IMPL(w, uint32_t)
MEM_WRITE_IMPL(s, uint16_t)
MEM_WRITE_IMPL(b, uint8_t)
