/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#define USE_MMAP 1
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "mpool.h"

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
} mpool_t;

static void *mem_arena(size_t sz)
{
#if defined(USE_MMAP)
    void *p =
        mmap(0, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED)
        return NULL;
#else
    void *p = malloc(sz);
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
    if (pool_size < chunk_size + sizeof(memchunk_t))
        pool_size += sizeof(memchunk_t);
    size_t page_count = (pool_size + pgsz - 1) / pgsz;
    char *p = mem_arena(page_count * pgsz);
    if (!p) {
        free(new_mp);
        return NULL;
    }

    new_mp->area.mapped = p;
    new_mp->page_count = page_count;
    new_mp->chunk_count = pool_size / (sizeof(memchunk_t) + chunk_size);
    new_mp->chunk_size = chunk_size;
    new_mp->free_chunk_head = (memchunk_t *) p;
    memchunk_t *cur = new_mp->free_chunk_head;
    for (size_t i = 0; i < new_mp->chunk_count - 1; i++) {
        cur->next =
            (memchunk_t *) ((char *) cur + (sizeof(memchunk_t) + chunk_size));
        cur = cur->next;
    }
    cur->next = NULL;
    return new_mp;
}

static void *mpool_extend(mpool_t *mp)
{
    size_t pool_size = mp->page_count * getpagesize();
    char *p = mem_arena(pool_size);
    if (!p)
        return NULL;
    area_t *new_area = malloc(sizeof(area_t));
    new_area->mapped = p;
    new_area->next = NULL;
    size_t chunk_count = pool_size / (sizeof(memchunk_t) + mp->chunk_size);
    mp->free_chunk_head = (memchunk_t *) p;
    memchunk_t *cur = mp->free_chunk_head;
    for (size_t i = 0; i < chunk_count - 1; i++) {
        cur->next = (memchunk_t *) ((char *) cur +
                                    (sizeof(memchunk_t) + mp->chunk_size));
        cur = cur->next;
    }
    mp->chunk_count += chunk_count;
    /* insert new mapped */
    area_t *cur_area = &mp->area;
    while (cur_area->next) {
        cur_area = cur_area->next;
    }
    cur_area->next = new_area;
    return p;
}

void *mpool_alloc(mpool_t *mp)
{
    if (!mp->chunk_count && !(mpool_extend(mp)))
        return NULL;
    char *ptr = (char *) mp->free_chunk_head + sizeof(memchunk_t);
    mp->free_chunk_head = mp->free_chunk_head->next;
    mp->chunk_count--;
    return ptr;
}

void *mpool_calloc(mpool_t *mp)
{
    if (!mp->chunk_count && !(mpool_extend(mp)))
        return NULL;
    char *ptr = (char *) mp->free_chunk_head + sizeof(memchunk_t);
    mp->free_chunk_head = mp->free_chunk_head->next;
    mp->chunk_count--;
    memset(ptr, 0, mp->chunk_size);
    return ptr;
}

void mpool_free(mpool_t *mp, void *target)
{
    memchunk_t *ptr = (memchunk_t *) ((char *) target - sizeof(memchunk_t));
    ptr->next = mp->free_chunk_head;
    mp->free_chunk_head = ptr;
    mp->chunk_count++;
}

void mpool_destroy(mpool_t *mp)
{
#if defined(USE_MMAP)
    size_t mem_size = mp->page_count * getpagesize();
    area_t *cur = &mp->area, *tmp = NULL;
    while (cur) {
        tmp = cur;
        cur = cur->next;
        munmap(tmp->mapped, mem_size);
        if (tmp != &mp->area)
            free(tmp);
    }
#else
    area_t *cur = &mp->area, *tmp = NULL;
    while (cur) {
        tmp = cur;
        cur = cur->next;
        free(tmp->mapped);
        if (tmp != &mp->area)
            free(tmp);
    }
#endif
    free(mp);
}
