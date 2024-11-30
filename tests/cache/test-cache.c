#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"

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

/*
 * Commands of test-cache:
 * 1. NEW: cache_create(N_CACHE_BITS), the cache size is set to pow(2,
 *         N_CACHE_BITS).
 * 2. GET key: cache_get(cache, key, true)
 * 3. PUT key val: cache_put(cache, key, val)
 * 4. FREE: cache_free(cache, free)
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
    while (getline(&line, &len, fp) != -1) {
        char *arr[3];
        split(arr, line, " ");
        if (!strcmp(arr[0], "GET")) {
            key = (int) strtol(arr[1], NULL, 10);
            ans = cache_get(cache, key, true);
            freq = cache_freq(cache, key);
            print_value(ans, freq);
        } else if (!strcmp(arr[0], "PUT")) {
            key = (int) strtol(arr[1], NULL, 10);
            val = malloc(sizeof(int));
            *val = (int) strtol(arr[2], NULL, 10);
            val = cache_put(cache, key, val);
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
        }
    }
    fclose(fp);
    free(line);

    return 0;
}
