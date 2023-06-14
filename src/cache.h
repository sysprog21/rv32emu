/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

struct cache;

/** cache_create - crate a new cache
 * @size_bits: cache size is 2^size_bits
 * @return: a pointer points to new cache
 */
struct cache *cache_create(int size_bits);

/**
 * cache_get - retrieve the specified entry from the cache
 * @cache: a pointer points to target cache
 * @key: the key of the specified entry
 * @return: the specified entry or NULL
 */
void *cache_get(struct cache *cache, uint32_t key);

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
void cache_free(struct cache *cache, void (*callback)(void *));

#if RV32_HAS(JIT)
bool cache_hot(struct cache *cache, uint32_t key);

uint8_t *code_cache_lookup(struct cache *cache, uint32_t key);

uint8_t *code_cache_add(struct cache *cache,
                        uint64_t key,
                        uint8_t *code,
                        size_t sz,
                        uint64_t align);
#endif