/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* Currently, THRESHOLD is set to identify hot spots. Once the using frequency
 * for a block exceeds the THRESHOLD, the tier-1 JIT compiler process is
 * triggered.
 */
#define THRESHOLD 4096

struct cache;

/** cache_create - create a new cache
 * @size_bits: cache size is 2^size_bits
 * @return: a pointer points to new cache
 */
struct cache *cache_create(uint32_t size_bits);

/**
 * cache_get - retrieve the specified entry from the cache
 * @cache: a pointer points to target cache
 * @key: the key of the specified entry
 * @update: update frequency or not
 * @return: the specified entry or NULL
 */
void *cache_get(const struct cache *cache, uint32_t key, bool update);

/**
 * cache_put - insert a new entry into the cache
 * @cache: a pointer points to target cache
 * @key: the key of the inserted entry
 * @value: the value of the inserted entry
 * @return: the replaced entry or NULL
 */
void *cache_put(struct cache *cache, uint32_t key, void *value);

/**
 * cache_free - free a cache
 * @cache: a pointer points to target cache
 * @callback: a function for freeing cache entry completely
 */
void cache_free(struct cache *cache);

#if RV32_HAS(JIT)
/**
 * cache_hot - check whether the frequency of the cache entry exceeds the
 * threshold or not
 * @cache: a pointer points to target cache
 * @key: the key of the specified entry
 */
bool cache_hot(const struct cache *cache, uint32_t key);

typedef void (*prof_func_t)(void *, uint32_t, FILE *);
void cache_profile(const struct cache *cache,
                   FILE *output_file,
                   prof_func_t func);
typedef void (*clear_func_t)(void *);
void clear_cache_hot(const struct cache *cache, clear_func_t func);
#endif

uint32_t cache_freq(const struct cache *cache, uint32_t key);

#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)

/* Page index for O(1) cache invalidation by virtual address.
 * With page-bounded blocks, each block fits entirely within one 4KB page,
 * allowing direct lookup by page address instead of O(n) scan.
 */
#define PAGE_INDEX_BITS 10
#define PAGE_INDEX_SIZE (1 << PAGE_INDEX_BITS)

/**
 * cache_invalidate_satp - invalidate all blocks matching the given SATP
 * @cache: a pointer to target cache
 * @satp: the SATP value to match
 * @return: number of blocks invalidated
 *
 * This is used by SFENCE.VMA with rs1=0 (global flush) to invalidate
 * JIT-compiled blocks that may contain stale VA→PA mappings.
 */
uint32_t cache_invalidate_satp(struct cache *cache, uint32_t satp);

/**
 * cache_invalidate_va - invalidate blocks within a VA page matching SATP
 * @cache: a pointer to target cache
 * @va: the virtual address (page will be derived)
 * @satp: the SATP value to match
 * @return: number of blocks invalidated
 *
 * This is used by SFENCE.VMA with rs1!=0 (address-specific flush) to
 * invalidate JIT-compiled blocks in a specific virtual page.
 * Uses O(1) page-indexed lookup instead of O(n) scan.
 */
uint32_t cache_invalidate_va(struct cache *cache, uint32_t va, uint32_t satp);
#endif
