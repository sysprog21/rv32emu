/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#define USE_MMAP 1
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "io.h"

static uint8_t *data_memory_base;
/* set memory size to 2^32 bytes */
#define MEM_SIZE 0x100000000ULL

memory_t *memory_new()
{
    memory_t *mem = malloc(sizeof(memory_t));
#if defined(USE_MMAP)
    data_memory_base = mmap(NULL, MEM_SIZE, PROT_READ | PROT_WRITE,
                            MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (data_memory_base == MAP_FAILED)
        return NULL;
#else
    data_memory_base = malloc(MEM_SIZE);
    if (!data_memory_base)
        return NULL;
#endif
    mem->mem_base = data_memory_base;
    mem->mem_size = MEM_SIZE;
    return mem;
}

void memory_delete(memory_t *mem)
{
#if defined(USE_MMAP)
    munmap(mem->mem_base, MEM_SIZE);
#else
    free(mem->mem_base);
#endif
}

void memory_read(memory_t *mem, uint8_t *dst, uint32_t addr, uint32_t size)
{
    memcpy(dst, mem->mem_base + addr, size);
}

uint32_t memory_read_str(memory_t *mem,
                         uint8_t *dst,
                         uint32_t addr,
                         uint32_t max)
{
    uint32_t len = 0;
    const uint8_t *end = dst + max;
    for (;; ++len, ++dst) {
        uint8_t ch = 0;
        memory_read(mem, &ch, addr + len, 1);
        if (dst < end)
            *dst = ch;
        if (!ch)
            break;
    }
    return len + 1;
}

uint32_t memory_ifetch(uint32_t addr)
{
    return *((const uint32_t *) (data_memory_base + addr));
}

#define MEM_READ_IMPL(size, type)                     \
    type memory_read_##size(uint32_t addr)            \
    {                                                 \
        return *((type *) (data_memory_base + addr)); \
    }

MEM_READ_IMPL(w, uint32_t);
MEM_READ_IMPL(s, uint16_t);
MEM_READ_IMPL(b, uint8_t);

#define MEM_WRITE_IMPL(size, type)                                     \
    void memory_write_##size(uint32_t addr, const uint8_t *src)        \
    {                                                                  \
        *((type *) (data_memory_base + addr)) = *((const type *) src); \
    }

MEM_WRITE_IMPL(w, uint32_t);
MEM_WRITE_IMPL(s, uint16_t);
MEM_WRITE_IMPL(b, uint8_t);
