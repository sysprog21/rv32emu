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
    if (!mem)
        return NULL;
    assert(mem);
#if HAVE_MMAP
#if defined(TSAN_ENABLED)
    /* ThreadSanitizer compatibility: Use MAP_FIXED to allocate at a specific
     * address to avoid conflicts with TSAN's shadow memory.
     */
#if defined(__x86_64__)
    /* x86_64: Allocate within TSAN's range (0x7cf000000000 - 0x7ffffffff000).
     *
     * Fixed address: 0x7d0000000000
     * Size: up to 4GB (0x100000000)
     * End: 0x7d0100000000 (well within app range)
     */
    void *fixed_addr = (void *) 0x7d0000000000UL;
#elif defined(__aarch64__)
    /* ARM64 (macOS/Apple Silicon): Use higher address range.
     *
     * Fixed address: 0x150000000000 (21TB)
     * Size: up to 4GB (0x100000000)
     * End: 0x150100000000
     *
     * This avoids TSAN's shadow memory and typical process allocations.
     * Requires ASLR disabled via: setarch $(uname -m) -R
     */
    void *fixed_addr = (void *) 0x150000000000UL;
#else
#error "TSAN is only supported on x86_64 and aarch64"
#endif
    data_memory_base = mmap(fixed_addr, size, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (data_memory_base == MAP_FAILED) {
        free(mem);
        return NULL;
    }
#else
    /* Standard allocation without TSAN */
    data_memory_base = mmap(NULL, size, PROT_READ | PROT_WRITE,
                            MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (data_memory_base == MAP_FAILED) {
        free(mem);
        return NULL;
    }
#endif
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
