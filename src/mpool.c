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

/* A pool has a single user, the emulator thread; the T2C thread hands back the
 * blocks it would otherwise free (see t2c_free_orphans), so no lock is needed.
 */

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
    size_t slot_size;
    struct memchunk *free_chunk_head;
    area_t area;
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

/* Thread an arena into a NULL-terminated free list and return its head. Pool
 * creation and extension build the list identically; keeping it in one place is
 * what stops the two copies from drifting apart. Requires chunk_count >= 1,
 * otherwise the loop bound underflows.
 */
static memchunk_t *build_freelist(char *p, size_t chunk_count, size_t slot_size)
{
    assert(chunk_count);
    memchunk_t *head = (memchunk_t *) p, *cur = head;
    for (size_t i = 0; i < chunk_count - 1; i++) {
        cur->next = (memchunk_t *) ((char *) cur + slot_size);
        cur = cur->next;
    }
    cur->next = NULL;
    return head;
}

mpool_t *mpool_create(size_t pool_size, size_t chunk_size)
{
    mpool_t *new_mp = malloc(sizeof(mpool_t));
    if (!new_mp)
        return NULL;

    new_mp->area.next = NULL;
    size_t pgsz = getpagesize();

    /* Overflow checks. The bound covers the header plus the round-up slack
     * added below, so neither addition can wrap.
     */
    if (chunk_size > SIZE_MAX - 2 * sizeof(memchunk_t) + 1)
        goto fail_mpool;
    /* Payloads sit one memchunk_t into each slot, so slot_size decides their
     * alignment: round it up rather than handing back pointers that drift out
     * of alignment as the arena is walked. Callers pass sizeof() of aligned
     * structs today, which this leaves untouched.
     */
    size_t slot_size = chunk_size + sizeof(memchunk_t);
    slot_size =
        (slot_size + sizeof(memchunk_t) - 1) & ~(sizeof(memchunk_t) - 1);

    /* A pool must hold at least one chunk. Below that chunk_count computes to 0
     * and the free-list build walks off the arena.
     */
    if (pool_size < slot_size)
        pool_size = slot_size;
    if (pool_size > SIZE_MAX - pgsz + 1)
        goto fail_mpool;
    size_t page_count = (pool_size + pgsz - 1) / pgsz;
    if (page_count > SIZE_MAX / pgsz)
        goto fail_mpool;

    size_t arena_size = page_count * pgsz;
    char *p = mem_arena(arena_size);
    if (!p)
        goto fail_mpool;

    new_mp->area.mapped = p;
    new_mp->page_count = page_count;
    new_mp->chunk_count = arena_size / slot_size;
    new_mp->chunk_size = chunk_size;
    new_mp->slot_size = slot_size;
    new_mp->free_chunk_head = build_freelist(p, new_mp->chunk_count, slot_size);

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
    size_t chunk_count = pool_size / mp->slot_size;

    /* Only reached with the pool drained, so no live free list is lost here. */
    assert(!mp->free_chunk_head);
    mp->free_chunk_head = build_freelist(p, chunk_count, mp->slot_size);
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
    if (!mp->chunk_count && !mpool_extend(mp))
        return NULL;
    return mpool_alloc_helper(mp);
}

void *mpool_calloc(mpool_t *mp)
{
    if (!mp)
        return NULL;
    if (!mp->chunk_count && !mpool_extend(mp))
        return NULL;
    char *ptr = mpool_alloc_helper(mp);
    memset(ptr, 0, mp->chunk_size);
    return ptr;
}

void mpool_free(mpool_t *mp, void *target)
{
    if (!mp || !target)
        return;
    memchunk_t *ptr = (memchunk_t *) ((char *) target - sizeof(memchunk_t));
    ptr->next = mp->free_chunk_head;
    mp->free_chunk_head = ptr;
    mp->chunk_count++;
}

void mpool_destroy(mpool_t *mp)
{
    if (!mp)
        return;
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
