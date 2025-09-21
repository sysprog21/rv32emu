/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#include "map.h"
#include "mt19937.h"

/* ANSI color codes */
#define COLOR_GREEN "\033[32m"
#define COLOR_RESET "\033[0m"

/* Helper function for original test */
static void swap(int *x, int *y)
{
    int tmp = *x;
    *x = *y;
    *y = tmp;
}

enum { N_NODES = 10000 };

/* mixed operations test */
static int test_map_mixed_operations(void)
{
    int ret = 0;
    map_t tree = map_init(int, int, map_cmp_int);

    int key[N_NODES], val[N_NODES];

    /* Generate data for insertion */
    for (int i = 0; i < N_NODES; i++) {
        key[i] = i;
        val[i] = mt19937_extract();
    }

    for (int i = 0; i < N_NODES; i++) {
        int pos_a = mt19937_extract() % N_NODES;
        int pos_b = mt19937_extract() % N_NODES;
        swap(&key[pos_a], &key[pos_b]);
        swap(&val[pos_a], &val[pos_b]);
    }

    /* add first 1/2 items */
    for (int i = 0; i < N_NODES / 2; i++) {
        map_iter_t my_it;
        map_insert(tree, key + i, val + i);
        map_find(tree, &my_it, key + i);
        if (!my_it.node) {
            ret = 1;
            goto free_tree;
        }
        assert((*(int *) (my_it.node->data)) == val[i]);
    }

    /* remove first 1/4 items */
    for (int i = 0; i < N_NODES / 4; i++) {
        map_iter_t my_it;
        map_find(tree, &my_it, key + i);
        if (map_at_end(tree, &my_it))
            continue;
        map_erase(tree, &my_it);
        map_find(tree, &my_it, key + i);
        if (my_it.node) {
            ret = 1;
            goto free_tree;
        }
    }

    /* add the rest */
    for (int i = N_NODES / 2 + 1; i < N_NODES; i++) {
        map_iter_t my_it;
        map_insert(tree, key + i, val + i);
        map_find(tree, &my_it, key + i);
        if (!my_it.node) {
            ret = 1;
            goto free_tree;
        }
        assert((*(int *) (my_it.node->data)) == val[i]);
    }

    /* remove 2nd quarter of items */
    for (int i = N_NODES / 4 + 1; i < N_NODES / 2; i++) {
        map_iter_t my_it;
        map_find(tree, &my_it, key + i);
        if (map_at_end(tree, &my_it)) {
            ret = 1;
            goto free_tree;
        }
        map_erase(tree, &my_it);
        map_find(tree, &my_it, key + i);
        if (my_it.node) {
            ret = 1;
            goto free_tree;
        }
    }

free_tree:
    map_clear(tree);
    map_delete(tree);
    return ret;
}

/* Get root node from map for validation */
static map_node_t *get_root(map_t obj)
{
    struct map_internal {
        map_node_t *root;
    };
    struct map_internal *m = (struct map_internal *) obj;
    return m ? m->root : NULL;
}

/* Validate red-black tree properties */
static int validate_rb_properties(map_node_t *node, int *black_height)
{
    if (!node) {
        *black_height = 1;
        return 1;
    }

    int is_red = (uintptr_t) node->right_red & 1;
    map_node_t *left = node->left;
    map_node_t *right = (map_node_t *) ((uintptr_t) node->right_red & ~3);

    if (is_red) {
        if (left && ((uintptr_t) left->right_red & 1)) {
            fprintf(stderr, "Red-red violation: red node has red left child\n");
            return 0;
        }
        if (right && ((uintptr_t) right->right_red & 1)) {
            fprintf(stderr,
                    "Red-red violation: red node has red right child\n");
            return 0;
        }
    }

    int left_height, right_height;
    if (!validate_rb_properties(left, &left_height))
        return 0;
    if (!validate_rb_properties(right, &right_height))
        return 0;

    if (left_height != right_height) {
        fprintf(stderr, "Black height mismatch: left=%d, right=%d\n",
                left_height, right_height);
        return 0;
    }

    *black_height = left_height + (is_red ? 0 : 1);
    return 1;
}

static int test_rb_properties(void)
{
    printf("  Testing red-black tree properties...");

    map_t m = map_init(int, int, map_cmp_int);
    if (!m) {
        fprintf(stderr, "Failed to create map\n");
        return 1;
    }

    for (int i = 1; i <= 100; i++) {
        int val = i * 10;
        if (!map_insert(m, &i, &val)) {
            fprintf(stderr, "Failed to insert %d\n", i);
            map_delete(m);
            return 1;
        }
    }

    map_node_t *root = get_root(m);
    if (!root) {
        fprintf(stderr, "Root is NULL after insertions\n");
        map_delete(m);
        return 1;
    }

    if ((uintptr_t) root->right_red & 1) {
        fprintf(stderr, "Root is not black\n");
        map_delete(m);
        return 1;
    }

    int black_height;
    if (!validate_rb_properties(root, &black_height)) {
        map_delete(m);
        return 1;
    }

    printf(" " COLOR_GREEN "[OK]" COLOR_RESET " (black height: %d)\n",
           black_height);

    map_delete(m);
    return 0;
}

/* Stress tests */

#define STRESS_SIZE 100000

static long get_memory_usage(void)
{
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss / 1024;
}

static long get_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000;
}

static int test_memory_stress(void)
{
    printf("  Testing memory stress (100K elements)...");

    long mem_start = get_memory_usage();

    map_t m = map_init(int, int, map_cmp_int);
    if (!m) {
        fprintf(stderr, "Failed to create map\n");
        return 1;
    }

    for (int i = 0; i < STRESS_SIZE; i++) {
        int val = i * 7;
        if (!map_insert(m, &i, &val)) {
            fprintf(stderr, "Failed to insert %d\n", i);
            map_delete(m);
            return 1;
        }
    }

    long mem_after_insert = get_memory_usage();

    for (int i = 0; i < STRESS_SIZE; i++) {
        map_iter_t it;
        map_find(m, &it, &i);
        if (map_at_end(m, &it) || map_iter_value(&it, int) != i * 7) {
            fprintf(stderr, "Value mismatch at %d\n", i);
            map_delete(m);
            return 1;
        }
    }

    for (int i = 0; i < STRESS_SIZE; i += 2) {
        map_iter_t it;
        map_find(m, &it, &i);
        if (!map_at_end(m, &it))
            map_erase(m, &it);
    }

    for (int i = 1; i < STRESS_SIZE; i += 2) {
        map_iter_t it;
        map_find(m, &it, &i);
        if (map_at_end(m, &it)) {
            fprintf(stderr, "Element %d missing after deletion\n", i);
            map_delete(m);
            return 1;
        }
    }

    map_delete(m);

    printf(" " COLOR_GREEN "[OK]" COLOR_RESET
           " (peak: %ld KB, ~%ld bytes/element)\n",
           mem_after_insert,
           (mem_after_insert - mem_start) * 1024 / STRESS_SIZE);

    return 0;
}

static int test_performance_scaling(void)
{
    printf("  Testing performance scaling...");

    int sizes[] = {1000, 10000, 100000};

    for (int s = 0; s < 3; s++) {
        int size = sizes[s];
        map_t m = map_init(int, int, map_cmp_int);
        if (!m) {
            fprintf(stderr, "Failed to create map\n");
            return 1;
        }

        long start = get_time_us();
        for (int i = 0; i < size; i++) {
            int val = i;
            map_insert(m, &i, &val);
        }
        long insert_time = get_time_us() - start;

        start = get_time_us();
        for (int i = 0; i < size; i++) {
            map_iter_t it;
            map_find(m, &it, &i);
        }
        long lookup_time = get_time_us() - start;

        printf(
            "\n    Size %6d: insert %.2fms (%.1fM ops/s), lookup %.2fms (%.1fM "
            "ops/s)",
            size, insert_time / 1000.0,
            insert_time > 0 ? (double) size / insert_time : 0,
            lookup_time / 1000.0,
            lookup_time > 0 ? (double) size / lookup_time : 0);

        map_delete(m);
    }

    printf("\n  Performance scaling... " COLOR_GREEN "[OK]" COLOR_RESET "\n");
    return 0;
}

/* Edge case tests */

typedef struct {
    char data[1024];
    int checksum;
} large_value_t;

static int test_empty_map(void)
{
    printf("  Testing empty map operations...");

    map_t m = map_init(int, int, map_cmp_int);
    if (!m) {
        fprintf(stderr, "Failed to create map\n");
        return 1;
    }

    if (!map_empty(m) || map_size(m) != 0) {
        fprintf(stderr, "New map not empty\n");
        map_delete(m);
        return 1;
    }

    map_iter_t it;
    map_first(m, &it);
    if (!map_at_end(m, &it)) {
        fprintf(stderr, "First iterator not at end for empty map\n");
        map_delete(m);
        return 1;
    }

    map_last(m, &it);
    if (!map_at_end(m, &it)) {
        fprintf(stderr, "Last iterator not at end for empty map\n");
        map_delete(m);
        return 1;
    }

    int key = 42;
    map_find(m, &it, &key);
    if (!map_at_end(m, &it)) {
        fprintf(stderr, "Find returned non-end for empty map\n");
        map_delete(m);
        return 1;
    }

    map_clear(m);
    if (!map_empty(m)) {
        fprintf(stderr, "Map not empty after clear\n");
        map_delete(m);
        return 1;
    }

    map_delete(m);
    printf(" " COLOR_GREEN "[OK]" COLOR_RESET "\n");
    return 0;
}

static int test_boundary_values(void)
{
    printf("  Testing boundary values...");

    map_t m = map_init(int, int, map_cmp_int);
    if (!m) {
        fprintf(stderr, "Failed to create map\n");
        return 1;
    }

    int min_key = INT_MIN, min_val = -999;
    int max_key = INT_MAX, max_val = 999;
    int zero_key = 0, zero_val = 0;

    if (!map_insert(m, &min_key, &min_val) ||
        !map_insert(m, &max_key, &max_val) ||
        !map_insert(m, &zero_key, &zero_val)) {
        fprintf(stderr, "Failed to insert boundary values\n");
        map_delete(m);
        return 1;
    }

    map_iter_t it;

    map_find(m, &it, &min_key);
    if (map_at_end(m, &it) || map_iter_value(&it, int) != min_val) {
        fprintf(stderr, "INT_MIN value mismatch\n");
        map_delete(m);
        return 1;
    }

    map_find(m, &it, &max_key);
    if (map_at_end(m, &it) || map_iter_value(&it, int) != max_val) {
        fprintf(stderr, "INT_MAX value mismatch\n");
        map_delete(m);
        return 1;
    }

    map_find(m, &it, &zero_key);
    if (map_at_end(m, &it) || map_iter_value(&it, int) != zero_val) {
        fprintf(stderr, "Zero value mismatch\n");
        map_delete(m);
        return 1;
    }

    map_first(m, &it);
    if (*(int *) it.node->key != INT_MIN) {
        fprintf(stderr, "First key is not INT_MIN\n");
        map_delete(m);
        return 1;
    }

    map_last(m, &it);
    if (*(int *) it.node->key != INT_MAX) {
        fprintf(stderr, "Last key is not INT_MAX\n");
        map_delete(m);
        return 1;
    }

    map_delete(m);
    printf(" " COLOR_GREEN "[OK]" COLOR_RESET "\n");
    return 0;
}

int main(int argc, char *argv[])
{
    (void) argc;
    (void) argv;

    mt19937_init(time(NULL));

    printf("Map tests:\n");

    int failed = 0;

    /* Mixed operations test */
    printf("  Testing mixed operations...");
    if (test_map_mixed_operations()) {
        printf(" FAILED\n");
        failed = 1;
    } else {
        printf(" " COLOR_GREEN "[OK]" COLOR_RESET "\n");
    }

    /* Validation tests */
    if (test_rb_properties())
        failed = 1;

    /* Stress tests */
    if (test_memory_stress())
        failed = 1;

    if (test_performance_scaling())
        failed = 1;

    /* Edge case tests */
    if (test_empty_map())
        failed = 1;

    if (test_boundary_values())
        failed = 1;

    return failed;
}
