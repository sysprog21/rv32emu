/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "mpool.h"
#include "utils.h"

static uint32_t cache_size, cache_size_bits;
static struct mpool *cache_mp;

/* hash function for the cache */
HASH_FUNC_IMPL(cache_hash, cache_size_bits, cache_size)

struct hlist_head {
    struct hlist_node *first;
};

struct hlist_node {
    struct hlist_node *next, **pprev;
};

typedef struct {
    void *value;
    uint32_t key;
    uint32_t frequency;
    struct list_head list;
    struct hlist_node ht_list;
} lfu_entry_t;

typedef struct {
    struct hlist_head *ht_list_head;
} hashtable_t;

typedef struct cache {
    struct list_head *lists[THRESHOLD];
    uint32_t list_size;
    hashtable_t *map;
    uint32_t capacity;
} cache_t;

#define INIT_HLIST_HEAD(ptr) ((ptr)->first = NULL)

static inline void INIT_HLIST_NODE(struct hlist_node *h)
{
    h->next = NULL;
    h->pprev = NULL;
}

static inline int hlist_empty(const struct hlist_head *h)
{
    return !h->first;
}

static inline void hlist_add_head(struct hlist_node *n, struct hlist_head *h)
{
    struct hlist_node *first = h->first;
    n->next = first;
    if (first)
        first->pprev = &n->next;

    h->first = n;
    n->pprev = &h->first;
}

static inline bool hlist_unhashed(const struct hlist_node *h)
{
    return !h->pprev;
}

static inline void hlist_del(struct hlist_node *n)
{
    struct hlist_node *next = n->next;
    struct hlist_node **pprev = n->pprev;

    *pprev = next;
    if (next)
        next->pprev = pprev;
}

static inline void hlist_del_init(struct hlist_node *n)
{
    if (hlist_unhashed(n))
        return;
    hlist_del(n);
    INIT_HLIST_NODE(n);
}

#define hlist_entry(ptr, type, member) container_of(ptr, type, member)

#ifdef __HAVE_TYPEOF
#define hlist_entry_safe(ptr, type, member)                  \
    ({                                                       \
        typeof(ptr) ____ptr = (ptr);                         \
        ____ptr ? hlist_entry(____ptr, type, member) : NULL; \
    })
#else
#define hlist_entry_safe(ptr, type, member) \
    (ptr) ? hlist_entry(ptr, type, member) : NULL
#endif

#ifdef __HAVE_TYPEOF
#define hlist_for_each_entry(pos, head, member)                              \
    for (pos = hlist_entry_safe((head)->first, typeof(*(pos)), member); pos; \
         pos = hlist_entry_safe((pos)->member.next, typeof(*(pos)), member))
#else
#define hlist_for_each_entry(pos, head, member, type)              \
    for (pos = hlist_entry_safe((head)->first, type, member); pos; \
         pos = hlist_entry_safe((pos)->member.next, type, member))
#endif

cache_t *cache_create(int size_bits)
{
    int i;
    cache_t *cache = malloc(sizeof(cache_t));
    if (!cache)
        return NULL;

    cache_size_bits = size_bits;
    cache_size = 1 << size_bits;
    for (i = 0; i < THRESHOLD; i++) {
        cache->lists[i] = malloc(sizeof(struct list_head));
        if (!cache->lists[i])
            goto free_lists;
        INIT_LIST_HEAD(cache->lists[i]);
    }

    cache->map = malloc(sizeof(hashtable_t));
    if (!cache->map)
        goto free_lists;

    cache->map->ht_list_head = malloc(cache_size * sizeof(struct hlist_head));
    if (!cache->map->ht_list_head)
        goto free_map;

    for (size_t ii = 0; ii < cache_size; ii++)
        INIT_HLIST_HEAD(&cache->map->ht_list_head[ii]);
    cache->list_size = 0;
    cache_mp =
        mpool_create(cache_size * sizeof(lfu_entry_t), sizeof(lfu_entry_t));
    cache->capacity = cache_size;
    return cache;

free_map:
    free(cache->map);
free_lists:
    for (int j = 0; j < i; j++)
        free(cache->lists[j]);

    free(cache);
    return NULL;
}

void *cache_get(const cache_t *cache, uint32_t key, bool update)
{
    if (!cache->capacity ||
        hlist_empty(&cache->map->ht_list_head[cache_hash(key)]))
        return NULL;

    lfu_entry_t *entry = NULL;
#ifdef __HAVE_TYPEOF
    hlist_for_each_entry (entry, &cache->map->ht_list_head[cache_hash(key)],
                          ht_list)
#else
    hlist_for_each_entry (entry, &cache->map->ht_list_head[cache_hash(key)],
                          ht_list, lfu_entry_t)
#endif
    {
        if (entry->key == key)
            break;
    }
    if (!entry || entry->key != key)
        return NULL;

    /* When the frequency of use for a specific block exceeds the predetermined
     * THRESHOLD, the block is dispatched to the code generator to generate C
     * code. The generated C code is then compiled into machine code by the
     * target compiler.
     */
    if (update && entry->frequency < THRESHOLD) {
        list_del_init(&entry->list);
        list_add(&entry->list, cache->lists[entry->frequency++]);
    }

    /* return NULL if cache miss */
    return entry->value;
}

void *cache_put(cache_t *cache, uint32_t key, void *value)
{
    assert(cache->list_size <= cache->capacity);

    lfu_entry_t *replaced_entry = NULL, *entry;
    hlist_for_each_entry (entry, &cache->map->ht_list_head[cache_hash(key)],
                          ht_list) {
        if (entry->key != key)
            continue;
        /* update the existing cache */
        if (entry->value != value) {
            replaced_entry = entry;
            break;
        }
        /* should not put an identical block to cache */
        assert(NULL);
        __UNREACHABLE;
    }

    /* get the entry to be replaced if cache is full */
    if (!replaced_entry && cache->list_size == cache->capacity) {
        for (int i = 0; i < THRESHOLD; i++) {
            if (list_empty(cache->lists[i]))
                continue;
            replaced_entry =
                list_last_entry(cache->lists[i], lfu_entry_t, list);
            break;
        }
        assert(replaced_entry);
    }

    void *replaced_value = NULL;
    if (replaced_entry) {
        replaced_value = replaced_entry->value;
        list_del_init(&replaced_entry->list);
        hlist_del_init(&replaced_entry->ht_list);
        mpool_free(cache_mp, replaced_entry);
        cache->list_size--;
    }

    lfu_entry_t *new_entry = mpool_alloc(cache_mp);
    INIT_LIST_HEAD(&new_entry->list);
    INIT_HLIST_NODE(&new_entry->ht_list);
    new_entry->key = key;
    new_entry->value = value;
    new_entry->frequency = 0;
    list_add(&new_entry->list, cache->lists[new_entry->frequency++]);
    hlist_add_head(&new_entry->ht_list,
                   &cache->map->ht_list_head[cache_hash(key)]);
    cache->list_size++;

    assert(cache->list_size <= cache->capacity);
    return replaced_value;
}

void cache_free(cache_t *cache)
{
    for (int i = 0; i < THRESHOLD; i++)
        free(cache->lists[i]);
    mpool_destroy(cache_mp);
    free(cache->map->ht_list_head);
    free(cache->map);
    free(cache);
}

uint32_t cache_freq(const struct cache *cache, uint32_t key)
{
    if (!cache->capacity ||
        hlist_empty(&cache->map->ht_list_head[cache_hash(key)]))
        return 0;
    lfu_entry_t *entry = NULL;
#ifdef __HAVE_TYPEOF
    hlist_for_each_entry (entry, &cache->map->ht_list_head[cache_hash(key)],
                          ht_list)
#else
    hlist_for_each_entry (entry, &cache->map->ht_list_head[cache_hash(key)],
                          ht_list, lfu_entry_t)
#endif
    {
        if (entry->key == key)
            return entry->frequency;
    }
    return 0;
}

#if RV32_HAS(JIT)
bool cache_hot(const struct cache *cache, uint32_t key)
{
    if (!cache->capacity ||
        hlist_empty(&cache->map->ht_list_head[cache_hash(key)]))
        return false;
    lfu_entry_t *entry = NULL;
#ifdef __HAVE_TYPEOF
    hlist_for_each_entry (entry, &cache->map->ht_list_head[cache_hash(key)],
                          ht_list)
#else
    hlist_for_each_entry (entry, &cache->map->ht_list_head[cache_hash(key)],
                          ht_list, lfu_entry_t)
#endif
    {
        if (entry->key == key && entry->frequency == THRESHOLD)
            return true;
    }
    return false;
}
void cache_profile(const struct cache *cache,
                   FILE *output_file,
                   prof_func_t func)
{
    assert(func);
    for (int i = 0; i < THRESHOLD; i++) {
        lfu_entry_t *entry, *safe;
        list_for_each_entry_safe (entry, safe, cache->lists[i], list) {
            func(entry->value, entry->frequency, output_file);
        }
    }
}

void clear_cache_hot(const struct cache *cache, clear_func_t func)
{
    assert(cache);
    assert(func);
    for (int i = 0; i < THRESHOLD; i++) {
        lfu_entry_t *entry, *safe;
        list_for_each_entry_safe (entry, safe, cache->lists[i], list) {
            func(entry->value);
        }
    }
}
#endif
