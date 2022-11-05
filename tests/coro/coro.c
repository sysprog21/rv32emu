#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "context.h"

#define ITERATIONS (1000 * 1000)

#define STACKSIZE 32768

context_t thread1, thread2, thread3;

static void thread1_fun(void *data)
{
    struct timeval tv0, tv;
    gettimeofday(&tv0, NULL);

    for (int i = 0; i < ITERATIONS; ++i)
        switch_context(thread1, thread2);

    gettimeofday(&tv, NULL);

    double t = tv.tv_sec - tv0.tv_sec + 1e-6 * (tv.tv_usec - tv0.tv_usec);
    double f = 3 * ITERATIONS / t;

    printf("%d context switches in %.1f s, %.1f/s, %.1f ns each\n",
           3 * ITERATIONS, t, f, 1e9 / f);
}

static void thread2_fun(void *data)
{
    /* thread 2 -> thread 1 */
    switch_context(thread2, thread1);

    /* thread 2 -> thread 1 */
    switch_context(thread2, thread1);

    /* thread 2 -> thread 3 */
    switch_context(thread2, thread3);

    /* thread 2 -> thread 1 */
    switch_context(thread2, thread1);

    /* thread 2 -> thread 3 */
    for (int i = 0;; ++i)
        switch_context(thread2, thread3);
}

static void thread3_fun(void *data)
{
    /* thread 3 -> thread 2 */
    switch_context(thread3, thread2);

    /* thread 3 -> thread 1 */
    switch_context(thread3, thread1);

    /* thread 3 -> thread 1 */
    for (int i = 0;; ++i)
        switch_context(thread3, thread1);
}

#define ALIGN 64 /* cache line */
static void *aligned_malloc(int size)
{
    void *mem = malloc(size + ALIGN + sizeof(void *));
    void **ptr =
        (void **) (((uintptr_t) mem + ALIGN + sizeof(void *)) & ~(ALIGN - 1));
    ptr[-1] = mem;
    return ptr;
}

static void aligned_free(void *ptr)
{
    free(((void **) ptr)[-1]);
}

#define RED_ZONE 128
static void init_context(context_t ctx, void (*entry)(void *data), void *data)
{
    void *stack = aligned_malloc(STACKSIZE + RED_ZONE + 8) - 8;
    initialize_context(ctx, stack, STACKSIZE, entry, data);
}

int main()
{
    init_context(thread2, thread2_fun, (void *) 0xDEEEECAF);
    init_context(thread3, thread3_fun, (void *) 0xF000000D);
    thread1_fun((void *) 0xBABEBABE);

    return 0;
}
