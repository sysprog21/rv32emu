/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */
#pragma once

#include <stddef.h>

struct mpool;

/**
 * mpool_create - create a new memory pool
 * @pool_size: the size of memory pool
 * @chunk_size: the size of memory chunk, pool would be divided into several
 * chunks
 */
struct mpool *mpool_create(size_t pool_size, size_t chunk_size);

/**
 * mpool_alloc - allocate a memory chunk from target memory pool
 * @mp: a pointer points to the target memory pool
 */
void *mpool_alloc(struct mpool *mp);

/**
 * mpool_calloc - allocate a memory chunk from target memory pool and set it to
 * zero
 * @mp: a pointer points to the target memory pool
 */
void *mpool_calloc(struct mpool *mp);

/**
 * mpool_free - free a memory pool
 * @mp: a pointer points to target memory pool
 * @target: a pointer points to the target memory chunk
 */
void mpool_free(struct mpool *mp, void *target);

/**
 * mpool_destory - destory a memory pool
 * @mp: a pointer points to the target memory pool
 */
void mpool_destory(struct mpool *mp);
