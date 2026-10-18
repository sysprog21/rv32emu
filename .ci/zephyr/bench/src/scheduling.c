/*
 * Copyright (c) 2012-2015 Wind River Systems, Inc.
 * Copyright (c) 2020,2023 Intel Corporation.
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Adapted from Zephyr bbc6385f0a2 tests/benchmarks/latency_measure/src/
 * thread_switch_yield.c, sema_test_signal_release.c, mutex_lock_unlock.c.
 * Fixed work replaces cycle sampling; recursive mutex depth is retained.
 */
#include "bench.h"
K_SEM_DEFINE(request, 0, 1);
K_SEM_DEFINE(reply, 0, 1);
K_MUTEX_DEFINE(mutex);
K_THREAD_STACK_DEFINE(stack, 1024);
static struct k_thread worker;
K_THREAD_STACK_DEFINE(yield_stack_a, 1024);
K_THREAD_STACK_DEFINE(yield_stack_b, 1024);
static struct k_thread yield_a, yield_b;
static uint32_t yield_count;
static bool yield_valid;
static uint32_t received;
const uint32_t bench_iterations = WORK_ITERATIONS;
const uint32_t bench_expected = WORK_ITERATIONS * 5U;
static void entry(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    for (unsigned i = 0; i < WORK_ITERATIONS; ++i) {
        k_sem_take(&request, K_FOREVER);
        ++received;
        k_sem_give(&reply);
    }
}
static void yield_entry(void *a, void *b, void *c)
{
    unsigned parity = (uintptr_t) a;

    ARG_UNUSED(b);
    ARG_UNUSED(c);
    for (unsigned i = 0; i < WORK_ITERATIONS; ++i) {
        if (yield_count != i * 2 + parity) {
            yield_valid = false;
        }
        ++yield_count;
        k_yield();
    }
}
bool bench_run(uint32_t *checksum)
{
    yield_count = 0;
    yield_valid = true;
    k_thread_create(&yield_a, yield_stack_a,
                    K_THREAD_STACK_SIZEOF(yield_stack_a), yield_entry,
                    (void *) 0, NULL, NULL, K_PRIO_COOP(1), 0, K_FOREVER);
    k_thread_create(&yield_b, yield_stack_b,
                    K_THREAD_STACK_SIZEOF(yield_stack_b), yield_entry,
                    (void *) 1, NULL, NULL, K_PRIO_COOP(1), 0, K_FOREVER);
    /* Both equal-priority peers must be runnable before the first yield. */
    k_sched_lock();
    k_thread_start(&yield_a);
    k_thread_start(&yield_b);
    k_sched_unlock();
    if ((k_thread_join(&yield_a, K_FOREVER) != 0) ||
        (k_thread_join(&yield_b, K_FOREVER) != 0) || (!yield_valid) ||
        (yield_count != WORK_ITERATIONS * 2)) {
        return false;
    }
    *checksum += yield_count;
    received = 0;
    k_thread_create(&worker, stack, K_THREAD_STACK_SIZEOF(stack), entry, NULL,
                    NULL, NULL, K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
    for (unsigned i = 0; i < WORK_ITERATIONS; ++i) {
        k_sem_give(&request);
        if ((k_sem_take(&reply, K_FOREVER) != 0) || (received != i + 1)) {
            return false;
        }
        ++*checksum;
    }
    if (k_thread_join(&worker, K_FOREVER) != 0) {
        return false;
    }
    for (unsigned i = 0; i < WORK_ITERATIONS; ++i) {
        if (k_mutex_lock(&mutex, K_NO_WAIT) != 0) {
            return false;
        }
        ++*checksum;
    }
    for (unsigned i = 0; i < WORK_ITERATIONS; ++i) {
        if (k_mutex_unlock(&mutex) != 0) {
            return false;
        }
        ++*checksum;
    }
    return (received == WORK_ITERATIONS) && (k_sem_count_get(&request) == 0) &&
           (k_sem_count_get(&reply) == 0);
}
