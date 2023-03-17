/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"

#define MIN(a, b) ((a < b) ? a : b)
#define GOLDEN_RATIO_32 0x61C88647
#define HASH(val) \
    (((val) * (GOLDEN_RATIO_32)) >> (32 - (cache_size_bits))) & (cache_size - 1)

static uint32_t cache_size, cache_size_bits;

/*
 * Adaptive Replacement Cache (ARC) improves the fundamental LRU strategy
 * by dividing the cache into two lists, T1 and T2. list T1 is for LRU
 * strategy and list T2 is for LFU strategy. Moreover, it keeps two ghost
 * lists, B1 and B2, with replaced entries from the LRU list going into B1
 * and the LFU list going into B2.
 *
 * Based on B1 and B2, ARC will modify the size of T1 and T2. When a cache
 * hit occurs in B1, it indicates that T1's capacity is too little, therefore
 * we increase T1's size while decreasing T2. But, if the cache hit occurs in
 * B2, we would increase the size of T2 and decrease the size of T1.
 */
typedef enum {
    LRU_list,
    LFU_list,
    LRU_ghost_list,
    LFU_ghost_list,
    N_CACHE_LIST_TYPES
} cache_list_t;

struct list_head {
    struct list_head *prev, *next;
};

struct hlist_head {
    struct hlist_node *first;
};

struct hlist_node {
    struct hlist_node *next, **pprev;
};

/*
 * list maintains four cache lists T1, T2, B1, and B2.
 * ht_list maintains hashtable and improves the performance of cache searching.
 */
typedef struct {
    void *value;
    uint32_t key;
    cache_list_t type;
    struct list_head list;
    struct hlist_node ht_list;
} arc_entry_t;

typedef struct {
    struct hlist_head *ht_list_head;
} hashtable_t;

typedef struct cache {
    struct list_head *lists[N_CACHE_LIST_TYPES];
    uint32_t list_size[N_CACHE_LIST_TYPES];
    hashtable_t *map;
    uint32_t capacity;
    uint32_t lru_capacity;
} cache_t;

static inline void INIT_LIST_HEAD(struct list_head *head)
{
    head->next = head;
    head->prev = head;
}

static inline void list_add(struct list_head *node, struct list_head *head)
{
    struct list_head *next = head->next;

    next->prev = node;
    node->next = next;
    node->prev = head;
    head->next = node;
}

static inline void list_del(struct list_head *node)
{
    struct list_head *next = node->next;
    struct list_head *prev = node->prev;

    next->prev = prev;
    prev->next = next;
}

static inline void list_del_init(struct list_head *node)
{
    list_del(node);
    INIT_LIST_HEAD(node);
}

#define list_entry(node, type, member) container_of(node, type, member)

#define list_last_entry(head, type, member) \
    list_entry((head)->prev, type, member)

#ifdef __HAVE_TYPEOF
#define list_for_each_entry_safe(entry, safe, head, member)                \
    for (entry = list_entry((head)->next, __typeof__(*entry), member),     \
        safe = list_entry(entry->member.next, __typeof__(*entry), member); \
         &entry->member != (head); entry = safe,                           \
        safe = list_entry(safe->member.next, __typeof__(*entry), member))
#else
#define list_for_each_entry_safe(entry, safe, head, member, type) \
    for (entry = list_entry((head)->next, type, member),          \
        safe = list_entry(entry->member.next, type, member);      \
         &entry->member != (head);                                \
         entry = safe, safe = list_entry(safe->member.next, type, member))
#endif

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
    cache_t *cache = malloc(sizeof(cache_t));
    if (!cache)
        return NULL;
    cache_size_bits = size_bits;
    cache_size = 1 << size_bits;

    for (int i = 0; i < N_CACHE_LIST_TYPES; i++) {
        cache->lists[i] = malloc(sizeof(struct list_head));
        INIT_LIST_HEAD(cache->lists[i]);
        cache->list_size[i] = 0;
    }

    cache->map = malloc(sizeof(hashtable_t));
    if (!cache->map) {
        free(cache->lists);
        free(cache);
        return NULL;
    }
    cache->map->ht_list_head = malloc(cache_size * sizeof(struct hlist_head));
    if (!cache->map->ht_list_head) {
        free(cache->map);
        free(cache->lists);
        free(cache);
        return NULL;
    }
    for (uint32_t i = 0; i < cache_size; i++) {
        INIT_HLIST_HEAD(&cache->map->ht_list_head[i]);
    }

    cache->capacity = cache_size;
    cache->lru_capacity = cache_size / 2;
    return cache;
}

/* Rules of ARC
 * 1. size of LRU_list + size of LFU_list <= c
 * 2. size of LRU_list + size of LRU_ghost_list <= c
 * 3. size of LFU_list + size of LFU_ghost_list <= 2c
 * 4. size of LRU_list + size of LFU_list + size of LRU_ghost_list + size of
 * LFU_ghost_list <= 2c
 */
#define CACHE_ASSERT(cache)                                                 \
    assert(cache->list_size[LRU_list] + cache->list_size[LFU_list] <=       \
           cache->capacity);                                                \
    assert(cache->list_size[LRU_list] + cache->list_size[LRU_ghost_list] <= \
           cache->capacity);                                                \
    assert(cache->list_size[LFU_list] + cache->list_size[LFU_ghost_list] <= \
           2 * cache->capacity);                                            \
    assert(cache->list_size[LRU_list] + cache->list_size[LRU_ghost_list] +  \
               cache->list_size[LFU_list] +                                 \
               cache->list_size[LFU_ghost_list] <=                          \
           2 * cache->capacity);

#define REPLACE_LIST(op1, op2)                                                 \
    if (cache->list_size[LFU_list] &&                                          \
        cache->list_size[LFU_list] op1(cache->capacity -                       \
                                       cache->lru_capacity)) {                 \
        move_to_mru(                                                           \
            cache, list_last_entry(cache->lists[LFU_list], arc_entry_t, list), \
            LFU_ghost_list);                                                   \
    } else if (cache->list_size[LRU_list] &&                                   \
               cache->list_size[LRU_list] op2 cache->lru_capacity) {           \
        move_to_mru(                                                           \
            cache, list_last_entry(cache->lists[LRU_list], arc_entry_t, list), \
            LRU_ghost_list);                                                   \
    }

static inline void move_to_mru(cache_t *cache,
                               arc_entry_t *entry,
                               const cache_list_t type)
{
    cache->list_size[entry->type]--;
    cache->list_size[type]++;
    entry->type = type;
    list_del_init(&entry->list);
    list_add(&entry->list, cache->lists[type]);
}

void *cache_get(cache_t *cache, uint32_t key)
{
    if (!cache->capacity || hlist_empty(&cache->map->ht_list_head[HASH(key)]))
        return NULL;

    arc_entry_t *entry = NULL;
#ifdef __HAVE_TYPEOF
    hlist_for_each_entry (entry, &cache->map->ht_list_head[HASH(key)], ht_list)
#else
    hlist_for_each_entry (entry, &cache->map->ht_list_head[HASH(key)], ht_list,
                          arc_entry_t)
#endif
    {
        if (entry->key == key)
            break;
    }
    if (!entry || entry->key != key)
        return NULL;
    /* cache hit in LRU_list */
    if (entry->type == LRU_list && cache->lru_capacity != cache->capacity)
        move_to_mru(cache, entry, LFU_list);

    /* cache hit in LFU_list */
    if (entry->type == LFU_list)
        move_to_mru(cache, entry, LFU_list);

    /* cache hit in LRU_ghost_list */
    if (entry->type == LRU_ghost_list) {
        if (cache->lru_capacity != cache->capacity) {
            cache->lru_capacity = cache->lru_capacity + 1;
            if (cache->list_size[LFU_list] &&
                cache->list_size[LFU_list] >=
                    (cache->capacity - cache->lru_capacity))
                move_to_mru(
                    cache,
                    list_last_entry(cache->lists[LFU_list], arc_entry_t, list),
                    LFU_ghost_list);
            move_to_mru(cache, entry, LFU_list);
        }
    }

    /* cache hit in LFU_ghost_list */
    if (entry->type == LFU_ghost_list) {
        cache->lru_capacity = cache->lru_capacity ? cache->lru_capacity - 1 : 0;
        REPLACE_LIST(>=, >);
        move_to_mru(cache, entry, LFU_list);
    }

    CACHE_ASSERT(cache);
    /* return NULL if cache miss */
    return entry->value;
}

void *cache_put(cache_t *cache, uint32_t key, void *value)
{
    void *delete_value = NULL;
    assert(cache->list_size[LRU_list] + cache->list_size[LRU_ghost_list] <=
           cache->capacity);
    /* Before adding new element to cach, we should check the status
     * of cache.
     */
    if ((cache->list_size[LRU_list] + cache->list_size[LRU_ghost_list]) ==
        cache->capacity) {
        if (cache->list_size[LRU_list] < cache->capacity) {
            arc_entry_t *delete_target = list_last_entry(
                cache->lists[LRU_ghost_list], arc_entry_t, list);
            list_del_init(&delete_target->list);
            hlist_del_init(&delete_target->ht_list);
            delete_value = delete_target->value;
            free(delete_target);
            cache->list_size[LRU_ghost_list]--;
            if (cache->list_size[LRU_list] &&
                cache->list_size[LRU_list] >= cache->lru_capacity)
                move_to_mru(
                    cache,
                    list_last_entry(cache->lists[LRU_list], arc_entry_t, list),
                    LRU_ghost_list);
        } else {
            arc_entry_t *delete_target =
                list_last_entry(cache->lists[LRU_list], arc_entry_t, list);
            list_del_init(&delete_target->list);
            hlist_del_init(&delete_target->ht_list);
            delete_value = delete_target->value;
            free(delete_target);
            cache->list_size[LRU_list]--;
        }
    } else {
        assert(cache->list_size[LRU_list] + cache->list_size[LRU_ghost_list] <
               cache->capacity);
        uint32_t size =
            cache->list_size[LRU_list] + cache->list_size[LRU_ghost_list] +
            cache->list_size[LFU_list] + cache->list_size[LFU_ghost_list];
        if (size == cache->capacity * 2) {
            arc_entry_t *delete_target = list_last_entry(
                cache->lists[LFU_ghost_list], arc_entry_t, list);
            list_del_init(&delete_target->list);
            hlist_del_init(&delete_target->ht_list);
            delete_value = delete_target->value;
            free(delete_target);
            cache->list_size[LFU_ghost_list]--;
        }
        REPLACE_LIST(>, >=)
    }
    arc_entry_t *new_entry = malloc(sizeof(arc_entry_t));
    new_entry->key = key;
    new_entry->value = value;
    /* check if all cache become LFU */
    if (cache->lru_capacity != 0) {
        new_entry->type = LRU_list;
        list_add(&new_entry->list, cache->lists[LRU_list]);
        cache->list_size[LRU_list]++;
    } else {
        new_entry->type = LRU_ghost_list;
        list_add(&new_entry->list, cache->lists[LRU_ghost_list]);
        cache->list_size[LRU_ghost_list]++;
    }
    hlist_add_head(&new_entry->ht_list, &cache->map->ht_list_head[HASH(key)]);

    CACHE_ASSERT(cache);
    return delete_value;
}

void cache_free(cache_t *cache, void (*callback)(void *))
{
    for (int i = 0; i < N_CACHE_LIST_TYPES; i++) {
        arc_entry_t *entry, *safe;
#ifdef __HAVE_TYPEOF
        list_for_each_entry_safe (entry, safe, cache->lists[i], list)
#else
        list_for_each_entry_safe (entry, safe, cache->lists[i], list,
                                  arc_entry_t)
#endif
            callback(entry->value);
    }
    free(cache->map->ht_list_head);
    free(cache->map);
    free(cache);
}