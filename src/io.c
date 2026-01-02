/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if HAVE_MMAP
#include <signal.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "io.h"
#include "log.h"

static uint8_t *data_memory_base;
static uint64_t data_memory_size;

#if HAVE_MMAP
/* Demand Paging Memory Management
 *
 * Memory is allocated in chunks on-demand using signal handling:
 * 1. Initial mapping uses PROT_NONE (no access, no physical memory)
 * 2. Access triggers SIGSEGV/SIGBUS, handler activates the chunk
 * 3. Unused chunks can be reclaimed via madvise(MADV_DONTNEED)
 *
 * This provides automatic memory growth with minimal initial footprint.
 */

/* Chunk size: 64KB provides good balance between granularity and overhead */
#define CHUNK_SHIFT 16
#define CHUNK_SIZE (1UL << CHUNK_SHIFT)
#define CHUNK_MASK (~(CHUNK_SIZE - 1))

/* Maximum chunks for 4GB address space: 4GB / 64KB = 65536 */
#define MAX_CHUNKS (0x100000000ULL >> CHUNK_SHIFT)
#define BITMAP_SIZE (MAX_CHUNKS / 8)

/* Bitmap tracking which chunks are activated */
static uint8_t chunk_bitmap[BITMAP_SIZE];

/* GC state: circular scan index */
static uint32_t gc_scan_idx;

/* Statistics: current and peak number of activated chunks (atomic for signal
 * safety) */
static atomic_uint_fast32_t active_chunks;
static atomic_uint_fast32_t peak_chunks;

/* Previous signal handlers to chain */
static struct sigaction prev_sigsegv_handler;
static struct sigaction prev_sigbus_handler;

static inline bool bitmap_test(uint32_t idx)
{
    return (chunk_bitmap[idx >> 3] & (1 << (idx & 7))) != 0;
}

static inline void bitmap_set(uint32_t idx)
{
    chunk_bitmap[idx >> 3] |= (1 << (idx & 7));
}

static inline void bitmap_clear(uint32_t idx)
{
    chunk_bitmap[idx >> 3] &= ~(1 << (idx & 7));
}

/* Check if a memory region is all zeros (for reclaim decision).
 * Uses byte-wise scan to avoid alignment issues with word-sized access.
 * Compiler may optimize this to SIMD operations where available.
 */
static bool is_region_zero(const uint8_t *ptr, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        if (ptr[i] != 0)
            return false;
    }
    return true;
}

/* Signal handler for demand paging
 *
 * Signal safety notes:
 * - mprotect() is async-signal-safe per POSIX.1-2008 and later
 * - Atomic operations with memory_order_relaxed are signal-safe
 * - Bitmap operations are on static memory with no locks
 * - No heap allocation or stdio calls in the handler
 */
static void memory_fault_handler(int sig, siginfo_t *si, void *context)
{
    uintptr_t fault_addr = (uintptr_t) si->si_addr;
    uintptr_t base = (uintptr_t) data_memory_base;
    uintptr_t end = base + data_memory_size;

    /* Check if fault is within our guest memory range */
    if (fault_addr >= base && fault_addr < end) {
        /* Calculate chunk boundaries relative to base address */
        uintptr_t offset = fault_addr - base;
        uintptr_t aligned_offset = offset & CHUNK_MASK;
        uintptr_t chunk_start = base + aligned_offset;
        uint32_t chunk_idx = aligned_offset >> CHUNK_SHIFT;

        /* Calculate chunk size (may be partial for last chunk) */
        size_t chunk_len = CHUNK_SIZE;
        if (chunk_start + CHUNK_SIZE > end) {
            /* Last partial chunk */
            chunk_len = end - chunk_start;
        }

        /* Activate the chunk with read/write permissions */
        int result =
            mprotect((void *) chunk_start, chunk_len, PROT_READ | PROT_WRITE);

        if (result == 0) {
            /* Only count if not already active (handles re-fault edge cases) */
            if (!bitmap_test(chunk_idx)) {
                bitmap_set(chunk_idx);
                uint_fast32_t current =
                    atomic_fetch_add_explicit(&active_chunks, 1,
                                              memory_order_relaxed) +
                    1;
                uint_fast32_t peak =
                    atomic_load_explicit(&peak_chunks, memory_order_relaxed);
                while (current > peak) {
                    if (atomic_compare_exchange_weak_explicit(
                            &peak_chunks, &peak, current, memory_order_relaxed,
                            memory_order_relaxed))
                        break;
                }
            }
            return; /* Resume execution */
        }
    }

    /* Not our fault or mprotect failed - chain to previous handler */
    struct sigaction *prev =
        (sig == SIGSEGV) ? &prev_sigsegv_handler : &prev_sigbus_handler;

    if (prev->sa_flags & SA_SIGINFO) {
        prev->sa_sigaction(sig, si, context);
    } else if (prev->sa_handler == SIG_DFL) {
        /* Restore default handler and re-raise */
        struct sigaction dfl;
        dfl.sa_handler = SIG_DFL;
        sigemptyset(&dfl.sa_mask);
        dfl.sa_flags = 0;
        sigaction(sig, &dfl, NULL);
        raise(sig);
    } else if (prev->sa_handler != SIG_IGN) {
        prev->sa_handler(sig);
    }
}

static bool install_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = memory_fault_handler;
    sa.sa_flags = SA_SIGINFO; /* No SA_NODEFER: prevent reentrancy */
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGSEGV, &sa, &prev_sigsegv_handler) == -1) {
        rv_log_error("Failed to install SIGSEGV handler for demand paging");
        return false;
    }

    if (sigaction(SIGBUS, &sa, &prev_sigbus_handler) == -1) {
        rv_log_error("Failed to install SIGBUS handler for demand paging");
        sigaction(SIGSEGV, &prev_sigsegv_handler, NULL);
        return false;
    }

    return true;
}

static void restore_signal_handlers(void)
{
    sigaction(SIGSEGV, &prev_sigsegv_handler, NULL);
    sigaction(SIGBUS, &prev_sigbus_handler, NULL);
}
#endif /* HAVE_MMAP */

memory_t *memory_new(uint64_t size)
{
    if (!size)
        return NULL;

    /* Maximum supported size is 4GB (32-bit address space).
     * The demand paging bitmap is sized for exactly 4GB.
     */
    if (size > 0x100000000ULL)
        return NULL;

    memory_t *mem = malloc(sizeof(memory_t));
    if (!mem)
        return NULL;

#if HAVE_MMAP
    /* Install signal handlers for demand paging */
    if (!install_signal_handlers()) {
        free(mem);
        return NULL;
    }

    /* Map memory with PROT_NONE initially - no physical memory allocated.
     * Chunks are activated on-demand when accessed (via signal handler).
     * MAP_NORESERVE ensures no swap space is reserved upfront.
     */
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif
    data_memory_base = mmap(NULL, size, PROT_NONE,
                            MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
    if (data_memory_base == MAP_FAILED) {
        restore_signal_handlers();
        free(mem);
        return NULL;
    }

    data_memory_size = size;
    memset(chunk_bitmap, 0, sizeof(chunk_bitmap));
    gc_scan_idx = 0;
    atomic_store_explicit(&active_chunks, 0, memory_order_relaxed);
    atomic_store_explicit(&peak_chunks, 0, memory_order_relaxed);
#else
    /* Fallback for systems without mmap (e.g., Windows, Emscripten).
     * Cannot use demand paging - physical memory is allocated upfront.
     * Limit to 512MB to avoid excessive memory consumption.
     * Build with HAVE_MMAP=1 for larger address spaces.
     */
#define MALLOC_MAX_SIZE (512UL * 1024 * 1024) /* 512 MB */
    if (size > MALLOC_MAX_SIZE) {
        free(mem);
        return NULL;
    }
    data_memory_base = malloc(size);
    if (!data_memory_base) {
        free(mem);
        return NULL;
    }
    /* Zero-initialize for consistent behavior */
    memset(data_memory_base, 0, size);
    data_memory_size = size;
#undef MALLOC_MAX_SIZE
#endif

    mem->mem_base = data_memory_base;
    mem->mem_size = data_memory_size; /* Use actual allocated size */
    return mem;
}

void memory_delete(memory_t *mem)
{
#if HAVE_MMAP
    /* Restore handlers first to prevent use-after-free in signal handler */
    restore_signal_handlers();
    munmap(mem->mem_base, mem->mem_size);
#else
    free(mem->mem_base);
#endif
    free(mem);
}

/* Return peak physical memory usage in bytes.
 * With MMAP: Returns actual physical memory allocated via demand paging.
 * Without MMAP: Returns total allocated size (no demand paging available).
 */
uint64_t memory_get_usage(void)
{
#if HAVE_MMAP
    /* Clamp to actual memory size to prevent overflow */
    uint32_t max_chunks = (data_memory_size + CHUNK_SIZE - 1) >> CHUNK_SHIFT;
    uint_fast32_t peak =
        atomic_load_explicit(&peak_chunks, memory_order_relaxed);
    uint32_t chunks = (peak < max_chunks) ? peak : max_chunks;
    return (uint64_t) chunks * CHUNK_SIZE;
#else
    return data_memory_size;
#endif
}

/* Incremental garbage collection - scans one chunk per call.
 * Reclaims zeroed chunks by releasing physical pages (madvise)
 * and re-arming the fault handler (mprotect PROT_NONE).
 */
void memory_gc(void)
{
#if HAVE_MMAP
    uint32_t idx = gc_scan_idx;

    /* Only process chunks within our actual memory size */
    uint32_t max_idx = (data_memory_size + CHUNK_SIZE - 1) >> CHUNK_SHIFT;
    if (max_idx > MAX_CHUNKS)
        max_idx = MAX_CHUNKS;

    if (idx >= max_idx) {
        gc_scan_idx = 0;
        return;
    }

    /* Block signals during bitmap operations to prevent races with handler */
    sigset_t block_set, old_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGSEGV);
    sigaddset(&block_set, SIGBUS);
    sigprocmask(SIG_BLOCK, &block_set, &old_set);

    /* Check if this chunk is activated */
    if (bitmap_test(idx)) {
        uint8_t *chunk_ptr =
            data_memory_base + ((uintptr_t) idx << CHUNK_SHIFT);

        /* Calculate chunk size (may be partial for last chunk) */
        size_t chunk_len = CHUNK_SIZE;
        uintptr_t chunk_end = (uintptr_t) chunk_ptr + CHUNK_SIZE;
        uintptr_t mem_end = (uintptr_t) data_memory_base + data_memory_size;
        if (chunk_end > mem_end)
            chunk_len = mem_end - (uintptr_t) chunk_ptr;

        /* Reclaim if chunk is all zeros */
        if (is_region_zero(chunk_ptr, chunk_len)) {
            /* Protect first to prevent races where chunk gets dirtied
             * between madvise and mprotect. Only proceed if successful.
             */
            if (mprotect(chunk_ptr, chunk_len, PROT_NONE) == 0) {
                /* Release physical pages back to OS (advisory) */
                madvise(chunk_ptr, chunk_len, MADV_DONTNEED);
                bitmap_clear(idx);
                uint_fast32_t current =
                    atomic_load_explicit(&active_chunks, memory_order_relaxed);
                if (current > 0)
                    atomic_fetch_sub_explicit(&active_chunks, 1,
                                              memory_order_relaxed);
            }
        }
    }

    /* Restore signal mask */
    sigprocmask(SIG_SETMASK, &old_set, NULL);

    /* Advance to next chunk (circular) */
    gc_scan_idx = (idx + 1) % max_idx;
#endif
}

/*
 * Fast memory access functions - no bounds checking for performance.
 * Callers must validate addresses. With MMAP, out-of-bounds access
 * triggers SIGSEGV that chains to the default handler.
 */

void memory_read(const memory_t *mem,
                 uint8_t *dst,
                 uint32_t addr,
                 uint32_t size)
{
    memcpy(dst, mem->mem_base + addr, size);
}

uint32_t memory_ifetch(uint32_t addr)
{
    uint32_t val;
    memcpy(&val, data_memory_base + addr, sizeof(val));
    return val;
}

/* Safe unaligned memory access using memcpy (compiler optimizes to load/store)
 */
#define MEM_READ_IMPL(size, type)                            \
    type memory_read_##size(uint32_t addr)                   \
    {                                                        \
        type val;                                            \
        memcpy(&val, data_memory_base + addr, sizeof(type)); \
        return val;                                          \
    }

MEM_READ_IMPL(w, uint32_t)
MEM_READ_IMPL(s, uint16_t)
MEM_READ_IMPL(b, uint8_t)

#define MEM_WRITE_IMPL(size, type)                              \
    void memory_write_##size(uint32_t addr, const uint8_t *src) \
    {                                                           \
        memcpy(data_memory_base + addr, src, sizeof(type));     \
    }

MEM_WRITE_IMPL(w, uint32_t)
MEM_WRITE_IMPL(s, uint16_t)
MEM_WRITE_IMPL(b, uint8_t)
