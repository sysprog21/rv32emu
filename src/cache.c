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
#include "riscv.h"
#include "riscv_private.h"
#include "utils.h"

static uint32_t cache_size, cache_size_bits;

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
    bool alive; /* indicates whether this cache is alive or a history of evicted
                   cache in hash map */
    uint32_t key;
    uint32_t freq;
    struct list_head list;
    struct hlist_node ht_list;
#if RV32_HAS(JIT) && RV32_HAS(SYSTEM) && RV32_HAS(BLOCK_CHAINING)
    /* link in the page index while the entry is alive */
    struct hlist_node page_node;
#endif
#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
    /* link in the address-space index while the entry is alive */
    struct hlist_node satp_node;
#endif
} cache_entry_t;

typedef struct {
    struct hlist_head *ht_list_head;
} hashtable_t;

/*
 * The cache utilizes the degenerated adaptive replacement cache (ARC), which
 * has only least-recently-used (LRU) and ignores least-frequently-used (LFU)
 * part. The frequently used cache will be compiled to the binary of target
 * platform by the just-in-time (JIT) compiler, so that it doesn't need to be
 * preserved in cache anymore. When the cache is full, the least used cache is
 * going to be evicted to the ghost list as the history. If the key of the
 * inserted entry matches the one in the ghost list, the history will be
 * detached and freed, and the stored information will be inherited by the new
 * entry.
 */

typedef struct cache {
    struct list_head list;       /* list of live cache */
    struct list_head ghost_list; /* list of evicted cache */
    /* Entries dropped from the history, kept for reuse: every block inserted
     * needs one, and taking it from here spares the allocator, which the T2C
     * thread keeps busy with LLVM.
     */
    struct list_head free_list;
    hashtable_t map; /* hash map which contains both live and evicted cache */
    uint32_t size;
    uint32_t ghost_list_size;
    uint32_t capacity;
#if RV32_HAS(JIT) && RV32_HAS(SYSTEM) && RV32_HAS(BLOCK_CHAINING)
    /* Page index for invalidation by virtual address. Each bucket links the
     * live entries whose blocks start in the pages hashed to it, through the
     * entries themselves, so indexing a block needs no allocation and dropping
     * one needs no walk: a page holds hundreds of blocks, and workloads that
     * start many processes evict blocks constantly.
     */
    struct hlist_head page_index[PAGE_INDEX_SIZE];
#endif
#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
    /* Address-space index for invalidation by satp, linked the same way. A full
     * SFENCE.VMA, which Linux issues several times in the life of each process,
     * then visits the blocks of one address space rather than every cached
     * block, each a likely cache miss.
     */
    struct hlist_head satp_index[SATP_INDEX_SIZE];
#endif
} cache_t;

#if RV32_HAS(JIT) && RV32_HAS(SYSTEM) && RV32_HAS(BLOCK_CHAINING)
/* Forward declarations for page index functions */
static void page_index_insert(cache_t *cache, cache_entry_t *entry);
#endif
#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
static void satp_index_insert(cache_t *cache, cache_entry_t *entry);
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
#ifndef __clang_analyzer__
    struct hlist_node *first = h->first;
    n->next = first;
    if (first)
        first->pprev = &n->next;

    h->first = n;
    n->pprev = &h->first;
#endif
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

/* clang-format off */
#ifdef __HAVE_TYPEOF
#define hlist_for_each_entry(pos, head, member)                              \
    for (pos = hlist_entry_safe((head)->first, typeof(*(pos)), member); pos; \
         pos = hlist_entry_safe((pos)->member.next, typeof(*(pos)), member))

#define hlist_for_each_entry_safe(pos, n, head, member)               \
    for (pos = hlist_entry_safe((head)->first, typeof(*pos), member); \
         pos && ({ n = pos->member.next; 1; });                       \
         pos = hlist_entry_safe(n, typeof(*pos), member))
#else
#define hlist_for_each_entry(pos, head, member, type)              \
    for (pos = hlist_entry_safe((head)->first, type, member); pos; \
         pos = hlist_entry_safe((pos)->member.next, type, member))

#define hlist_for_each_entry_safe(pos, n, head, member, type) \
    for (pos = hlist_entry_safe((head)->first, type, member); \
         pos && ({ n = pos->member.next; 1; });               \
         pos = hlist_entry_safe(n, type, member))
#endif
/* clang-format on */

cache_t *cache_create(uint32_t size_bits)
{
    /* Prevent integer overflow in 1 << (size_bits + 2) */
    if (size_bits >= 30)
        return NULL;

    cache_t *cache = malloc(sizeof(cache_t));
    if (!cache)
        return NULL;

    /* The map holds evicted history as well as live entries, up to twice the
     * capacity, and is searched on every block dispatch: give it four buckets
     * per live entry so that a lookup rarely walks a chain.
     */
    cache_size_bits = size_bits + 2;
    cache_size = 1 << cache_size_bits;

    INIT_LIST_HEAD(&cache->list);
    INIT_LIST_HEAD(&cache->ghost_list);
    INIT_LIST_HEAD(&cache->free_list);
    cache->size = 0;
    cache->ghost_list_size = 0;
    cache->capacity = 1 << size_bits;

    /* Check for overflow in size calculation */
    size_t alloc_size = cache_size * sizeof(struct hlist_head);
    if (alloc_size / sizeof(struct hlist_head) != cache_size)
        goto fail_cache;

    cache->map.ht_list_head = malloc(alloc_size);
    if (!cache->map.ht_list_head)
        goto fail_cache;

    for (uint32_t i = 0; i < cache_size; i++)
        INIT_HLIST_HEAD(&cache->map.ht_list_head[i]);

#if RV32_HAS(JIT) && RV32_HAS(SYSTEM) && RV32_HAS(BLOCK_CHAINING)
    for (uint32_t i = 0; i < PAGE_INDEX_SIZE; i++)
        INIT_HLIST_HEAD(&cache->page_index[i]);
#endif
#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
    for (uint32_t i = 0; i < SATP_INDEX_SIZE; i++)
        INIT_HLIST_HEAD(&cache->satp_index[i]);
#endif

    return cache;

fail_cache:
    free(cache);
    return NULL;
}

/* Locate the live entry for @key, or NULL on a miss.
 *
 * cache_put keeps at most one entry per key, reviving a ghost rather than
 * adding a second, so the first key match is the only candidate. Every lookup
 * shares one hash computation and one bucket walk.
 */
static cache_entry_t *cache_find(const cache_t *cache, uint32_t key)
{
    if (unlikely(!cache->capacity))
        return NULL;

    const uint32_t hash = cache_hash(key);
    if (hlist_empty(&cache->map.ht_list_head[hash]))
        return NULL;

    cache_entry_t *entry = NULL;
#ifdef __HAVE_TYPEOF
    hlist_for_each_entry (entry, &cache->map.ht_list_head[hash], ht_list)
#else
    hlist_for_each_entry (entry, &cache->map.ht_list_head[hash], ht_list,
                          cache_entry_t)
#endif
    {
        if (entry->key == key)
            break;
    }

    if (!entry || entry->key != key || !entry->alive)
        return NULL;
    return entry;
}

void *cache_get(const cache_t *cache, uint32_t key, bool update)
{
    return cache_get_with_freq(cache, key, update).value;
}

/* A zeroed entry, as calloc() would return, preferably a recycled one */
static cache_entry_t *cache_entry_new(cache_t *cache)
{
    if (list_empty(&cache->free_list))
        return calloc(1, sizeof(cache_entry_t));
    cache_entry_t *entry =
        list_first_entry(&cache->free_list, cache_entry_t, list);
    list_del(&entry->list);
    memset(entry, 0, sizeof(*entry));
    return entry;
}

/* Keep an entry that is no longer in any list or map for reuse */
static void cache_entry_recycle(cache_t *cache, cache_entry_t *entry)
{
    list_add(&entry->list, &cache->free_list);
}

/*
 * When the size of ghost list reaches the limit, the oldest history is going to
 * be dropped. The stored information will be lost forever.
 */
FORCE_INLINE void cache_ghost_list_update(cache_t *cache)
{
    if (cache->ghost_list_size <= cache->capacity)
        return;

    cache_entry_t *entry =
        list_last_entry(&cache->ghost_list, cache_entry_t, list);
    assert(!entry->alive);
    hlist_del_init(&entry->ht_list);
    list_del_init(&entry->list);
    cache->ghost_list_size--;
    cache_entry_recycle(cache, entry);
}

/*
 * For a cache insertion, it might be the one which:
 * - evicts the least recently used cache
 * - updates the existing cache
 * - retrieves the information from the history in the glost list
 */
void *cache_put(cache_t *cache, uint32_t key, void *value, uint32_t *freq)
{
    assert(freq);
    *freq = 0;
    assert(cache->size <= cache->capacity);

    cache_entry_t *replaced = NULL, *revived = NULL, *entry;
#ifdef __HAVE_TYPEOF
    hlist_for_each_entry (entry, &cache->map.ht_list_head[cache_hash(key)],
                          ht_list)
#else
    hlist_for_each_entry (entry, &cache->map.ht_list_head[cache_hash(key)],
                          ht_list, cache_entry_t)
#endif
    {
        if (entry->key != key)
            continue;
        if (!entry->alive) {
            revived = entry;
            break;
        }
        /* update the existing cache */
        if (entry->value != value) {
            replaced = entry;
            break;
        }
        /* should not put an identical block to cache */
        assert(NULL);
        __UNREACHABLE;
    }

    /* get the entry to be replaced if cache is full */
    if (!replaced && cache->size == cache->capacity) {
        replaced = list_last_entry(&cache->list, cache_entry_t, list);
        assert(replaced);
    }

    void *replaced_value = NULL;
    if (replaced) {
        assert(replaced->alive);

        replaced_value = replaced->value;
#if RV32_HAS(JIT) && RV32_HAS(SYSTEM) && RV32_HAS(BLOCK_CHAINING)
        /* Remove replaced block from page index before eviction */
        hlist_del_init(&replaced->page_node);
#endif
#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
        hlist_del_init(&replaced->satp_node);
#endif
        replaced->alive = false;
        list_del_init(&replaced->list);
        cache->size--;
        list_add(&replaced->list, &cache->ghost_list);
        cache->ghost_list_size++;
    }

    cache_entry_t *new_entry = cache_entry_new(cache);
    if (unlikely(!new_entry)) {
        /* Allocation failed - restore replaced entry if exists */
        if (replaced) {
            replaced->alive = true;
            list_del_init(&replaced->list);
            list_add(&replaced->list, &cache->list);
            cache->size++;
            cache->ghost_list_size--;
#if RV32_HAS(JIT) && RV32_HAS(SYSTEM) && RV32_HAS(BLOCK_CHAINING)
            page_index_insert(cache, replaced);
#endif
#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
            satp_index_insert(cache, replaced);
#endif
        }
        return NULL;
    }
    assert(new_entry);

    INIT_LIST_HEAD(&new_entry->list);
    INIT_HLIST_NODE(&new_entry->ht_list);
    new_entry->key = key;
    new_entry->value = value;
    new_entry->alive = true;

    if (!revived) {
        new_entry->freq = 1;
    } else {
        new_entry->freq = revived->freq + 1;
        hlist_del_init(&revived->ht_list);
        list_del_init(&revived->list);
        cache->ghost_list_size--;
        cache_entry_recycle(cache, revived);
    }

    list_add(&new_entry->list, &cache->list);
    hlist_add_head(&new_entry->ht_list,
                   &cache->map.ht_list_head[cache_hash(key)]);

    cache->size++;
    *freq = new_entry->freq;

#if RV32_HAS(JIT) && RV32_HAS(SYSTEM) && RV32_HAS(BLOCK_CHAINING)
    /* Page index for O(1) invalidation - blocks are page-terminated
     * and use fallthrough chaining for non-branch block boundaries.
     */
    page_index_insert(cache, new_entry);
#endif
#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
    satp_index_insert(cache, new_entry);
#endif

    cache_ghost_list_update(cache);

    assert(cache->size <= cache->capacity);
    assert(cache->ghost_list_size <= cache->capacity);
    return replaced_value;
}

void cache_free(cache_t *cache)
{
    /* Free all live cache entries */
    cache_entry_t *entry, *safe;
#ifdef __HAVE_TYPEOF
    list_for_each_entry_safe (entry, safe, &cache->list, list)
#else
    list_for_each_entry_safe (entry, safe, &cache->list, list, cache_entry_t)
#endif
        free(entry);
    /* Free all ghost (evicted history) cache entries */
#ifdef __HAVE_TYPEOF
    list_for_each_entry_safe (entry, safe, &cache->ghost_list, list)
#else
    list_for_each_entry_safe (entry, safe, &cache->ghost_list, list,
                              cache_entry_t)
#endif
        free(entry);
#ifdef __HAVE_TYPEOF
    list_for_each_entry_safe (entry, safe, &cache->free_list, list)
#else
    list_for_each_entry_safe (entry, safe, &cache->free_list, list,
                              cache_entry_t)
#endif
        free(entry);
    free(cache->map.ht_list_head);
    free(cache);
}

/*
 * FIXME: In system simulation, there might be several identical PC from
 * different processes. We need to check the SATP CSR to update the correct
 * entry.
 */
/* When the frequency of use for a specific block exceeds the predetermined
 * THRESHOLD, the block is dispatched to the code generator to generate C
 * code. The generated C code is then compiled into machine code by the
 * target compiler.
 */
cache_lookup_t cache_get_with_freq(const cache_t *cache,
                                   uint32_t key,
                                   bool update)
{
    cache_lookup_t result = {0};
    cache_entry_t *entry = cache_find(cache, key);
    if (!entry)
        return result;

    if (update)
        entry->freq++;
    result.value = entry->value;
    result.freq = entry->freq;
    return result;
}

#if RV32_HAS(JIT)
void cache_profile(const struct cache *cache,
                   FILE *output_file,
                   prof_func_t func)
{
    assert(cache);
    assert(func);
    assert(output_file);

    cache_entry_t *entry;
#ifdef __HAVE_TYPEOF
    list_for_each_entry (entry, &cache->list, list)
#else
    list_for_each_entry (entry, &cache->list, list, cache_entry_t)
#endif
    {
        func(entry->value, entry->freq, output_file);
    }
}

/* Disable UBSAN function pointer type check for indirect calls. When T2C is
 * enabled, t2c_dispose_block_engine is compiled with LLVM's cflags which can
 * cause function type metadata mismatch, triggering false positive UBSAN
 * errors when called via clear_func_t.
 */
DISABLE_UBSAN_FUNC
void clear_cache_hot(const struct cache *cache, clear_func_t func)
{
    assert(cache);
    assert(func);

    cache_entry_t *entry = NULL;
#ifdef __HAVE_TYPEOF
    list_for_each_entry (entry, &cache->list, list)
#else
    list_for_each_entry (entry, &cache->list, list, cache_entry_t)
#endif
    {
        func(entry->value);
    }
}
#endif

#if RV32_HAS(JIT) && RV32_HAS(SYSTEM) && RV32_HAS(BLOCK_CHAINING)
/* Page index functions for O(1) cache invalidation.
 * Requires BLOCK_CHAINING for page-terminated blocks.
 */

/* Hash function for page index using golden ratio multiplicative hash */
HASH_FUNC_IMPL(page_index_hash, PAGE_INDEX_BITS, PAGE_INDEX_SIZE)

/* Link a live entry into the bucket of the page its block starts in */
static void page_index_insert(cache_t *cache, cache_entry_t *entry)
{
    const block_t *block = entry->value;
    const uint32_t page = block->pc_start & ~(RV_PG_SIZE - 1);
    hlist_add_head(&entry->page_node,
                   &cache->page_index[page_index_hash(page >> RV_PG_SHIFT)]);
}
#endif /* RV32_HAS(JIT) && RV32_HAS(SYSTEM) && RV32_HAS(BLOCK_CHAINING) */

#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
/* Thread safety note: These invalidation functions assume single-threaded
 * execution. The rv32emu JIT operates in a single-threaded model where
 * compilation and execution do not occur concurrently. If this assumption
 * changes, appropriate locking must be added around cache->list traversal.
 */

HASH_FUNC_IMPL(satp_index_hash, SATP_INDEX_BITS, SATP_INDEX_SIZE)

/* Mark a block invalidated, counting it in *compiled when T2C compiled code for
 * it: only then can the T2C caches hold entries to clear.
 */
static inline void block_invalidate(block_t *block, uint32_t *compiled UNUSED)
{
    block->invalidated = true;
#if RV32_HAS(T2C)
    if (block->func)
        (*compiled)++;
    /* Reset hot2 to prevent T2C execution of invalidated blocks, so that
     * rv_step() falls through to re-translation.
     */
    ATOMIC_STORE(&block->hot2, false, ATOMIC_RELEASE);
#endif
}

/* Drop an entry whose block was just invalidated from both indexes, so that
 * later flushes, which Linux issues several times per process, visit only the
 * blocks built since. The entry stays in the cache until the block is rebuilt,
 * which replaces it; unlinking it again then does nothing.
 */
static void entry_unindex(cache_entry_t *entry)
{
#if RV32_HAS(BLOCK_CHAINING)
    hlist_del_init(&entry->page_node);
#endif
    hlist_del_init(&entry->satp_node);
}

/* Link a live entry into the bucket of its block's address space */
static void satp_index_insert(cache_t *cache, cache_entry_t *entry)
{
    const block_t *block = entry->value;
    hlist_add_head(&entry->satp_node,
                   &cache->satp_index[satp_index_hash(block->satp)]);
}

uint32_t cache_invalidate_satp(cache_t *cache,
                               uint32_t satp,
                               uint32_t *n_compiled)
{
    if (n_compiled)
        *n_compiled = 0;
    if (unlikely(!cache->capacity))
        return 0;

    uint32_t count = 0, compiled = 0;
    cache_entry_t *entry = NULL;
    struct hlist_node *next;
    struct hlist_head *head = &cache->satp_index[satp_index_hash(satp)];
#ifdef __HAVE_TYPEOF
    hlist_for_each_entry_safe(entry, next, head, satp_node)
#else
    hlist_for_each_entry_safe(entry, next, head, satp_node, cache_entry_t)
#endif
    {
        block_t *block = (block_t *) entry->value;
        if (block->satp == satp && !block->invalidated) {
            block_invalidate(block, &compiled);
            entry_unindex(entry);
            count++;
        }
    }
    if (n_compiled)
        *n_compiled = compiled;
    return count;
}

uint32_t cache_invalidate_all(cache_t *cache)
{
    uint32_t count = 0, compiled = 0;
    cache_entry_t *entry = NULL;
#ifdef __HAVE_TYPEOF
    list_for_each_entry (entry, &cache->list, list)
#else
    list_for_each_entry (entry, &cache->list, list, cache_entry_t)
#endif
    {
        block_t *block = (block_t *) entry->value;
        if (!block || block->invalidated)
            continue;
        block_invalidate(block, &compiled);
        entry_unindex(entry);
        count++;
    }
    return count;
}

bool cache_has_va(const cache_t *cache, uint32_t va, uint32_t satp)
{
#if RV32_HAS(BLOCK_CHAINING)
    const uint32_t va_page = va & ~(RV_PG_SIZE - 1);
    const cache_entry_t *entry;
    const struct hlist_head *head =
        &cache->page_index[page_index_hash(va_page >> RV_PG_SHIFT)];
#ifdef __HAVE_TYPEOF
    hlist_for_each_entry (entry, head, page_node)
#else
    hlist_for_each_entry (entry, head, page_node, cache_entry_t)
#endif
    {
        const block_t *block = entry->value;
        if (block->satp == satp && !block->invalidated &&
            (block->pc_start & ~(RV_PG_SIZE - 1)) == va_page)
            return true;
    }
    return false;
#else
    (void) cache, (void) va, (void) satp;
    return true;
#endif
}

uint32_t cache_invalidate_va(cache_t *cache,
                             uint32_t va,
                             uint32_t satp,
                             uint32_t *n_compiled)
{
    if (n_compiled)
        *n_compiled = 0;
    if (unlikely(!cache->capacity))
        return 0;

    uint32_t va_page = va & ~(RV_PG_SIZE - 1);
    uint32_t count = 0, compiled = 0;

#if RV32_HAS(BLOCK_CHAINING)
    /* With page-bounded blocks, each block fits entirely within one 4KB page,
     * so only the bucket for this page needs checking.
     */
    cache_entry_t *pentry;
    struct hlist_node *next;
    struct hlist_head *head =
        &cache->page_index[page_index_hash(va_page >> RV_PG_SHIFT)];
#ifdef __HAVE_TYPEOF
    hlist_for_each_entry_safe(pentry, next, head, page_node)
#else
    hlist_for_each_entry_safe(pentry, next, head, page_node, cache_entry_t)
#endif
    {
        block_t *block = (block_t *) pentry->value;
        /* Verify block belongs to this page (hash collision check) */
        if (block->satp != satp || block->invalidated ||
            (block->pc_start & ~(RV_PG_SIZE - 1)) != va_page)
            continue;
        block_invalidate(block, &compiled);
        entry_unindex(pentry);
        count++;
    }
#else
    /* Without block chaining, blocks may span pages and no page index is
     * kept, so scan every block.
     */
    cache_entry_t *entry = NULL;
#ifdef __HAVE_TYPEOF
    list_for_each_entry (entry, &cache->list, list)
#else
    list_for_each_entry (entry, &cache->list, list, cache_entry_t)
#endif
    {
        block_t *block = (block_t *) entry->value;
        if (!block || block->satp != satp || block->invalidated)
            continue;

        /* Check if target VA page overlaps with block's address range.
         * A block may span multiple pages, so we check if va_page falls
         * within [block_start_page, block_end_page].
         *
         * Note: pc_end is exclusive (address after last instruction), so we
         * use (pc_end - 1) to get the page containing the last byte. This
         * avoids false invalidation when pc_end falls exactly on a page
         * boundary.
         */
        uint32_t block_start_page = block->pc_start & ~(RV_PG_SIZE - 1);
        uint32_t last_byte = block->pc_end > block->pc_start ? block->pc_end - 1
                                                             : block->pc_start;
        uint32_t block_end_page = last_byte & ~(RV_PG_SIZE - 1);
        if (va_page >= block_start_page && va_page <= block_end_page) {
            block_invalidate(block, &compiled);
            entry_unindex(entry);
            count++;
        }
    }
#endif /* RV32_HAS(BLOCK_CHAINING) */

    if (n_compiled)
        *n_compiled = compiled;
    return count;
}
#endif /* RV32_HAS(JIT) && RV32_HAS(SYSTEM) */
