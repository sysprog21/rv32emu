#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "riscv_private.h"

/* In JIT builds cache.c reads the block fields of the values it holds, so
 * each value is a zeroed block, and the integer under test lives in its first
 * field, which cache.c never reads.
 */
static int *value_new(int v)
{
    int *val = calloc(1, sizeof(block_t));
    assert(val);
    *val = v;
    return val;
}

static void print_value(int *val, int freq)
{
    if (val)
        printf("%d %d\n", *val, freq);
    else
        printf("NULL %d\n", freq);
}

static void split(char **arr, char *str, const char *del)
{
    char *s = strtok(str, del);

    while (s) {
        *arr++ = s;
        s = strtok(NULL, del);
    }
}

#define N_CACHE_BITS 4
#define N_MULTI_BITS 10
/* one key more than a cache of pow(2, N_CACHE_BITS) holds */
#define N_MULTI_KEYS ((1 << N_CACHE_BITS) + 1)

/*
 * cache_free leaves the values to the caller, so MULTI keeps every value it
 * allocates, and frees them all once every cache is gone.
 */
static int *multi_vals[2 * N_MULTI_KEYS];
static int n_multi_vals;

static void multi_put(struct cache *cache, int key, int v)
{
    assert(n_multi_vals < (int) ARRAY_SIZE(multi_vals));
    int *val = value_new(v);
    multi_vals[n_multi_vals++] = val;

    uint32_t freq;
    val = cache_put(cache, key, val, &freq);
    if (val)
        printf("REPLACE %d\n", *val);
}

static void multi_get(const struct cache *cache, int key)
{
    cache_lookup_t got = cache_get_with_freq(cache, key, true);
    print_value(got.value, (int) got.freq);
}

/*
 * Cache a is created before the larger cache b. When the hash width was shared
 * by all caches and set by the latest cache_create, this order made a index
 * past its map, which only a sanitizer reliably notices. The smaller cache c,
 * created once b holds its keys, then made b look them up in other buckets of
 * its map and miss them, which the output shows. Each cache has to keep its
 * own map, entries and replacement order.
 */
static void test_multi(void)
{
    struct cache *a = cache_create(N_CACHE_BITS);
    assert(a);
    printf("NEW CACHE\n");
    struct cache *b = cache_create(N_MULTI_BITS);
    assert(b);
    printf("NEW CACHE\n");

    for (int key = 1; key < N_MULTI_KEYS; key++)
        multi_put(a, key, key);
    for (int key = 1; key < N_MULTI_KEYS; key++)
        multi_get(a, key);
    /* cache_get does not reorder, so the first key put is the one replaced */
    multi_put(a, N_MULTI_KEYS, N_MULTI_KEYS);
    multi_get(a, 1);

    /* b holds the same keys, more of them than a does, without replacing */
    for (int key = 1; key <= N_MULTI_KEYS; key++)
        multi_put(b, key, 100 + key);
    for (int key = 1; key <= N_MULTI_KEYS; key++)
        multi_get(b, key);

    struct cache *c = cache_create(N_CACHE_BITS);
    assert(c);
    printf("NEW CACHE\n");

    for (int key = 1; key <= N_MULTI_KEYS; key++)
        multi_get(a, key);
    for (int key = 1; key <= N_MULTI_KEYS; key++)
        multi_get(b, key);

    cache_free(c);
    printf("FREE CACHE\n");
    cache_free(a);
    printf("FREE CACHE\n");
    cache_free(b);
    printf("FREE CACHE\n");
    while (n_multi_vals)
        free(multi_vals[--n_multi_vals]);
}

/*
 * Commands of test-cache:
 * 1. NEW: cache_create(N_CACHE_BITS), the cache size is set to pow(2,
 *         N_CACHE_BITS).
 * 2. GET key: cache_get(cache, key, true)
 * 3. PUT key val: cache_put(cache, key, val)
 * 4. FREE: cache_free(cache, free)
 * 5. MULTI: cache_create(N_CACHE_BITS), then cache_create(N_MULTI_BITS), and
 *           put and get keys in both, then get them again after another
 *           cache_create(N_CACHE_BITS), before freeing them and their values.
 */
int main(int argc, char *argv[])
{
    if (argc < 2)
        return 1; /* Fail */

    FILE *fp = fopen(argv[1], "r");
    assert(fp);

    char *line = NULL;
    size_t len = 0;
    struct cache *cache = NULL;
    int key, freq, *ans, *val;
    uint32_t put_freq;
    while (getline(&line, &len, fp) != -1) {
        char *arr[3];
        split(arr, line, " ");
        if (!strcmp(arr[0], "GET")) {
            key = (int) strtol(arr[1], NULL, 10);
            cache_lookup_t got = cache_get_with_freq(cache, key, true);
            ans = got.value;
            freq = (int) got.freq;
            print_value(ans, freq);
        } else if (!strcmp(arr[0], "PUT")) {
            key = (int) strtol(arr[1], NULL, 10);
            val = value_new((int) strtol(arr[2], NULL, 10));
            val = cache_put(cache, key, val, &put_freq);
            if (val) {
                printf("REPLACE %d\n", *val);
                free(val);
            }
        } else if (!strcmp(arr[0], "NEW\n")) {
            cache = cache_create(N_CACHE_BITS);
            assert(cache);
            printf("NEW CACHE\n");
        } else if (!strcmp(arr[0], "FREE\n")) {
            cache_free(cache);
            printf("FREE CACHE\n");
        } else if (!strcmp(arr[0], "MULTI\n")) {
            test_multi();
        }
    }
    fclose(fp);
    free(line);

    return 0;
}
