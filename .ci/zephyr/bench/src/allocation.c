/*
 * Copyright (c) 2026 Måns Ansgariusson <mansgariusson@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */
/* Adapted from tests/benchmarks/memory_allocators/src/k_heap.c at
 * Zephyr bbc6385f0a2: fixed alloc/realloc/free with payload checks.
 */
#include <string.h>
#include "bench.h"

K_HEAP_DEFINE(heap, 1024);
const uint32_t bench_iterations = WORK_ITERATIONS;
const uint32_t bench_expected = WORK_ITERATIONS * 384U;
bool bench_run(uint32_t *checksum)
{
    for (uint32_t i = 0; i < WORK_ITERATIONS; ++i) {
        uint8_t *p = k_heap_alloc(&heap, 128, K_NO_WAIT);

        if (!p) {
            return false;
        }
        memset(p, (uint8_t) i, 128);
        uint8_t *q = k_heap_realloc(&heap, p, 256, K_NO_WAIT);

        if (!q) {
            k_heap_free(&heap, p);
            return false;
        }
        for (unsigned j = 0; j < 128; ++j) {
            if (q[j] != (uint8_t) i) {
                k_heap_free(&heap, q);
                return false;
            }
        }
        *checksum += 384;
        k_heap_free(&heap, q);
    }
    return true;
}
