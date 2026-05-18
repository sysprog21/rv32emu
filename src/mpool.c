/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if HAVE_MMAP
#include <sys/mman.h>
#include <unistd.h>
#else
#define getpagesize() 4096
#endif

#include "feature.h"
#include "mpool.h"

/* T2C runs a worker thread that calls mpool_free on the same pools the
 * main thread allocates from (block_mp / block_ir_mp / fuse_mp). The
 * freelist is unsynchronized internally, so without a guard the worker's
 * free can hand the same chunk out twice on the next main-thread alloc,
 * which later surfaces as `free(): corrupted unsorted chunks` on the libc
 * heap once a double-freed ir->branch_table is released. Gate the mutex on
 * T2C so non-threaded builds keep the original lock-free fast path.
 */
#if RV32_HAS(T2C)
#include <pthread.h>
/* Init/destroy are called from single-threaded setup/teardown paths
 * (riscv_create / riscv_delete in src/riscv.c); failure here means broken
 * contract (already-init/EBUSY at destroy = stray thread, ENOMEM at init
 * = no mutex at all) and must abort loudly rather than silently leaking
 * a half-initialized pool or unmapping memory under a live owner.
 */
#define MPOOL_LOCK_INIT(mp)                                         \
    do {                                                            \
        int _mpool_lock_rc = pthread_mutex_init(&(mp)->lock, NULL); \
        assert(_mpool_lock_rc == 0);                                \
        (void) _mpool_lock_rc;                                      \
    } while (0)
#define MPOOL_LOCK_DESTROY(mp)                                   \
    do {                                                         \
        int _mpool_lock_rc = pthread_mutex_destroy(&(mp)->lock); \
        assert(_mpool_lock_rc == 0);                             \
        (void) _mpool_lock_rc;                                   \
    } while (0)
#define MPOOL_LOCK(mp) pthread_mutex_lock(&(mp)->lock)
#define MPOOL_UNLOCK(mp) pthread_mutex_unlock(&(mp)->lock)
#else
#define MPOOL_LOCK_INIT(mp) ((void) 0)
#define MPOOL_LOCK_DESTROY(mp) ((void) 0)
#define MPOOL_LOCK(mp) ((void) 0)
#define MPOOL_UNLOCK(mp) ((void) 0)
#endif

/* Chunk layout: [memchunk_t next-pointer][user data] */
typedef struct memchunk {
    struct memchunk *next;
} memchunk_t;

typedef struct area {
    char *mapped;
    struct area *next;
} area_t;

typedef struct mpool {
    size_t chunk_count;
    size_t page_count;
    size_t chunk_size;
    struct memchunk *free_chunk_head;
    area_t area;
#if RV32_HAS(T2C)
    pthread_mutex_t lock;
#endif
} mpool_t;

/* Allocate page-aligned memory via mmap (demand-paged) or malloc fallback */
static void *mem_arena(size_t sz)
{
    void *p;
#if HAVE_MMAP
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif
    p = mmap(0, sz, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED)
        return NULL;
#else
    p = malloc(sz);
    if (!p)
        return NULL;
#endif
    return p;
}

mpool_t *mpool_create(size_t pool_size, size_t chunk_size)
{
    mpool_t *new_mp = malloc(sizeof(mpool_t));
    if (!new_mp)
        return NULL;

    new_mp->area.next = NULL;
    size_t pgsz = getpagesize();

    /* Overflow checks */
    if (chunk_size > SIZE_MAX - sizeof(memchunk_t))
        goto fail_mpool;
    if (pool_size < chunk_size + sizeof(memchunk_t))
        pool_size += sizeof(memchunk_t);
    if (pool_size > SIZE_MAX - pgsz + 1)
        goto fail_mpool;
    size_t page_count = (pool_size + pgsz - 1) / pgsz;
    if (page_count > SIZE_MAX / pgsz)
        goto fail_mpool;

    char *p = mem_arena(page_count * pgsz);
    if (!p)
        goto fail_mpool;

    new_mp->area.mapped = p;
    new_mp->page_count = page_count;
    new_mp->chunk_count = pool_size / (sizeof(memchunk_t) + chunk_size);
    new_mp->chunk_size = chunk_size;

    /* Build free list */
    new_mp->free_chunk_head = (memchunk_t *) p;
    memchunk_t *cur = new_mp->free_chunk_head;
    for (size_t i = 0; i < new_mp->chunk_count - 1; i++) {
        cur->next =
            (memchunk_t *) ((char *) cur + (sizeof(memchunk_t) + chunk_size));
        cur = cur->next;
    }
    cur->next = NULL;

    MPOOL_LOCK_INIT(new_mp);
    return new_mp;

fail_mpool:
    free(new_mp);
    return NULL;
}

/* Extend pool by allocating another memory area of same size */
static void *mpool_extend(mpool_t *mp)
{
    size_t pool_size = mp->page_count * getpagesize();
    char *p = mem_arena(pool_size);
    if (!p)
        return NULL;

    area_t *new_area = malloc(sizeof(area_t));
    if (!new_area)
        goto fail_area;

    new_area->mapped = p;
    new_area->next = NULL;
    size_t chunk_count = pool_size / (sizeof(memchunk_t) + mp->chunk_size);

    /* Build free list for new area */
    mp->free_chunk_head = (memchunk_t *) p;
    memchunk_t *cur = mp->free_chunk_head;
    for (size_t i = 0; i < chunk_count - 1; i++) {
        cur->next = (memchunk_t *) ((char *) cur +
                                    (sizeof(memchunk_t) + mp->chunk_size));
        cur = cur->next;
    }
    mp->chunk_count += chunk_count;

    /* Append to area list */
    area_t *cur_area = &mp->area;
    while (cur_area->next)
        cur_area = cur_area->next;
    cur_area->next = new_area;

    return p;

fail_area:
#if HAVE_MMAP
    munmap(p, pool_size);
#else
    free(p);
#endif
    return NULL;
}

FORCE_INLINE void *mpool_alloc_helper(mpool_t *mp)
{
    char *ptr = (char *) mp->free_chunk_head + sizeof(memchunk_t);
    mp->free_chunk_head = mp->free_chunk_head->next;
    mp->chunk_count--;
    return ptr;
}

void *mpool_alloc(mpool_t *mp)
{
    if (!mp)
        return NULL;
    MPOOL_LOCK(mp);
    if (!mp->chunk_count && !mpool_extend(mp)) {
        MPOOL_UNLOCK(mp);
        return NULL;
    }
    void *ptr = mpool_alloc_helper(mp);
    MPOOL_UNLOCK(mp);
    return ptr;
}

void *mpool_calloc(mpool_t *mp)
{
    if (!mp)
        return NULL;
    MPOOL_LOCK(mp);
    if (!mp->chunk_count && !mpool_extend(mp)) {
        MPOOL_UNLOCK(mp);
        return NULL;
    }
    char *ptr = mpool_alloc_helper(mp);
    memset(ptr, 0, mp->chunk_size);
    MPOOL_UNLOCK(mp);
    return ptr;
}

void mpool_free(mpool_t *mp, void *target)
{
    if (!mp || !target)
        return;
    MPOOL_LOCK(mp);
    memchunk_t *ptr = (memchunk_t *) ((char *) target - sizeof(memchunk_t));
    ptr->next = mp->free_chunk_head;
    mp->free_chunk_head = ptr;
    mp->chunk_count++;
    MPOOL_UNLOCK(mp);
}

void mpool_destroy(mpool_t *mp)
{
    if (!mp)
        return;
    MPOOL_LOCK_DESTROY(mp);
#if HAVE_MMAP
    size_t mem_size = mp->page_count * getpagesize();
    for (area_t *cur = &mp->area, *tmp; cur; cur = tmp) {
        tmp = cur->next;
        munmap(cur->mapped, mem_size);
        if (cur != &mp->area)
            free(cur);
    }
#else
    for (area_t *cur = &mp->area, *tmp; cur; cur = tmp) {
        tmp = cur->next;
        free(cur->mapped);
        if (cur != &mp->area)
            free(cur);
    }
#endif
    free(mp);
}
