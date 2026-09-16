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

typedef struct {
    _Alignas(16) uint64_t value;
} aligned_value_t;

_Static_assert(_Alignof(aligned_value_t) > _Alignof(map_node_t),
               "Regression type must be over-aligned");

static map_cmp_t cmp_aligned(const void *arg0, const void *arg1)
{
    const aligned_value_t *a = arg0;
    const aligned_value_t *b = arg1;
    return (map_cmp_t) ((a->value > b->value) - (a->value < b->value));
}

static map_cmp_t cmp_uint64(const void *arg0, const void *arg1)
{
    const uint64_t a = *(const uint64_t *) arg0;
    const uint64_t b = *(const uint64_t *) arg1;
    return (map_cmp_t) ((a > b) - (a < b));
}

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
        assert(map_iter_value(&my_it, int) == val[i]);
    }

    /* remove first 1/4 items */
    for (int i = 0; i < N_NODES / 4; i++) {
        map_iter_t my_it;
        map_find(tree, &my_it, key + i);
        if (map_at_end(&my_it))
            continue;
        map_erase(&my_it);
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
        assert(map_iter_value(&my_it, int) == val[i]);
    }

    /* remove 2nd quarter of items */
    for (int i = N_NODES / 4 + 1; i < N_NODES / 2; i++) {
        map_iter_t my_it;
        map_find(tree, &my_it, key + i);
        if (map_at_end(&my_it)) {
            ret = 1;
            goto free_tree;
        }
        map_erase(&my_it);
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

/* Get root node from map for validation.
 *
 * Reached by climbing the parent links from the smallest node rather than by
 * mirroring a prefix of the private struct, so reordering map_internal cannot
 * silently turn some other field into "the root".
 */
static map_node_t *get_root(map_t obj)
{
    map_iter_t it;
    map_first(obj, &it);
    if (map_at_end(&it))
        return NULL;

    map_node_t *node = it.node;
    while (node->parent)
        node = node->parent;
    return node;
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
    map_node_t *right =
        (map_node_t *) ((uintptr_t) node->right_red & ~(uintptr_t) 1);

    if ((left && left->parent != node) || (right && right->parent != node)) {
        fprintf(stderr, "Broken parent link\n");
        return 0;
    }

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
    if (root->parent) {
        fprintf(stderr, "Root has a parent\n");
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

static int test_mixed_rebalancing(void)
{
    printf("  Testing erase/insert rebalance regression...");

    map_t m = map_init(int, int, map_cmp_int);
    const int keys[] = {8, 747, 776, 880};
    map_iter_t it;

    for (size_t i = 0; i < 3; i++)
        map_insert(m, &keys[i], &keys[i]);
    map_find(m, &it, &keys[0]);
    map_erase(&it);
    map_insert(m, &keys[3], &keys[3]);

    int black_height;
    int valid = validate_rb_properties(get_root(m), &black_height);
    map_delete(m);
    printf(" %s\n", valid ? COLOR_GREEN "[OK]" COLOR_RESET : "FAILED");
    return valid ? 0 : 1;
}

static int test_overaligned_payloads(void)
{
    printf("  Testing over-aligned key/value storage...");

    /* map_new_aligned() is public, so its alignment arguments are untrusted. A
     * non-power-of-two would turn the offset mask into nonsense rather than
     * failing, so it has to be rejected up front.
     */
    if (map_new_aligned(4, 0, 4, 8, map_cmp_int) ||
        map_new_aligned(4, 6, 4, 8, map_cmp_int) ||
        map_new_aligned(4, 8, 4, 0, map_cmp_int) ||
        map_new_aligned(4, 8, 4, 24, map_cmp_int)) {
        fprintf(stderr, "Invalid alignment accepted\n");
        return 1;
    }

    map_t m = map_init(aligned_value_t, aligned_value_t, cmp_aligned);
    if (!m) {
        fprintf(stderr, "Failed to create over-aligned map\n");
        return 1;
    }

    for (uint64_t i = 0; i < 1000; i++) {
        aligned_value_t key = {.value = i};
        aligned_value_t value = {.value = i * 3};
        if (!map_insert(m, &key, &value)) {
            fprintf(stderr, "Failed over-aligned insertion\n");
            map_delete(m);
            return 1;
        }
    }

    map_iter_t it;
    for (map_first(m, &it); !map_at_end(&it); map_next(&it)) {
        if ((uintptr_t) map_iter_key_ptr(&it) % _Alignof(aligned_value_t) ||
            (uintptr_t) &map_iter_value(&it, aligned_value_t) %
                _Alignof(aligned_value_t) ||
            map_iter_value(&it, aligned_value_t).value !=
                map_iter_key(&it, aligned_value_t).value * 3) {
            fprintf(stderr, "Misaligned or corrupt over-aligned payload\n");
            map_delete(m);
            return 1;
        }
    }

    map_delete(m);

    /* The node stride has to satisfy the stricter of the two alignments, so
     * cover a key that is more aligned than its value as well: taking only the
     * value's alignment leaves every key misaligned.
     */
    map_t km = map_init(aligned_value_t, int, cmp_aligned);
    if (!km) {
        fprintf(stderr, "Failed to create over-aligned key map\n");
        return 1;
    }
    for (uint64_t i = 0; i < 1000; i++) {
        aligned_value_t key = {.value = i};
        int value = (int) i * 3;
        if (!map_insert(km, &key, &value)) {
            fprintf(stderr, "Failed over-aligned key insertion\n");
            map_delete(km);
            return 1;
        }
    }
    for (map_first(km, &it); !map_at_end(&it); map_next(&it)) {
        if ((uintptr_t) map_iter_key_ptr(&it) % _Alignof(aligned_value_t)) {
            fprintf(stderr, "Over-aligned key is misaligned\n");
            map_delete(km);
            return 1;
        }
        if (map_iter_value(&it, int) !=
            (int) map_iter_key(&it, aligned_value_t).value * 3) {
            fprintf(stderr, "Corrupt value beside an over-aligned key\n");
            map_delete(km);
            return 1;
        }
    }
    map_delete(km);

    /* Past max_align_t the chunks come from aligned_alloc() instead of
     * malloc(); nothing else in this file reaches that branch, and a malloc
     * that happens to over-align large blocks would hide a regression there.
     */
    map_t om = map_new_aligned(sizeof(uint64_t), 2 * _Alignof(max_align_t),
                               sizeof(uint64_t), 2 * _Alignof(max_align_t),
                               cmp_uint64);
    if (!om) {
        fprintf(stderr, "Failed to create map past max_align_t\n");
        return 1;
    }
    for (uint64_t i = 0; i < 500; i++) {
        uint64_t key = (i << 32) | (499 - i);
        uint64_t value = i * 3;
        if (!map_insert(om, &key, &value)) {
            fprintf(stderr, "Failed insertion past max_align_t\n");
            map_delete(om);
            return 1;
        }
    }
    uint64_t seen = 0;
    for (map_first(om, &it); !map_at_end(&it); map_next(&it)) {
        if ((uintptr_t) map_iter_key_ptr(&it) % (2 * _Alignof(max_align_t))) {
            fprintf(stderr, "Node under-aligned past max_align_t\n");
            map_delete(om);
            return 1;
        }
        if (map_iter_value(&it, uint64_t) != seen * 3) {
            fprintf(stderr, "Corrupt payload past max_align_t\n");
            map_delete(om);
            return 1;
        }
        seen++;
    }
    if (seen != 500) {
        fprintf(stderr, "Lost entries past max_align_t\n");
        map_delete(om);
        return 1;
    }
    map_delete(om);

    printf(" " COLOR_GREEN "[OK]" COLOR_RESET "\n");
    return 0;
}

/* Node storage lifecycle.
 *
 * Nodes come from map-owned chunks and erased ones go on a free list, so the
 * free list and the chunks must be torn down together: a map_clear() that frees
 * the chunks while keeping the free list hands the next insertion a pointer
 * into released memory. Nothing else in this file inserts after a clear, and
 * the sequence needs a prior erase to make the free list non-empty.
 */
static int test_allocator_lifecycle(void)
{
    printf("  Testing node storage lifecycle...");

    map_t m = map_init(int, int, map_cmp_int);
    if (!m) {
        fprintf(stderr, "Failed to create map\n");
        return 1;
    }

    for (int i = 0; i < 64; i++)
        map_insert(m, &i, &i);
    for (int i = 0; i < 64; i++) {
        map_iter_t it;
        map_find(m, &it, &i);
        map_erase(&it);
    }
    map_clear(m);

    /* Reuse the map after the clear: any node handed out here must come from
     * fresh storage, not from a free list left pointing into released chunks.
     */
    for (int i = 0; i < 128; i++) {
        int val = i * 5;
        if (!map_insert(m, &i, &val)) {
            fprintf(stderr, "Insert after clear failed at %d\n", i);
            map_delete(m);
            return 1;
        }
    }
    if (map_size(m) != 128) {
        fprintf(stderr, "Size %zu after reuse, expected 128\n", map_size(m));
        map_delete(m);
        return 1;
    }
    for (int i = 0; i < 128; i++) {
        map_iter_t it;
        map_find(m, &it, &i);
        if (map_at_end(&it) || map_iter_value(&it, int) != i * 5 ||
            map_iter_key(&it, int) != i) {
            fprintf(stderr, "Corrupt entry %d after reuse\n", i);
            map_delete(m);
            return 1;
        }
    }

    /* Erasing everything and refilling must also survive, this time without a
     * clear in between, so recycled nodes are exercised directly.
     */
    for (int i = 0; i < 128; i++) {
        map_iter_t it;
        map_find(m, &it, &i);
        map_erase(&it);
    }
    if (!map_empty(m) || map_size(m)) {
        fprintf(stderr, "Map not empty after erasing everything\n");
        map_delete(m);
        return 1;
    }
    for (int i = 0; i < 128; i++) {
        int val = i * 9;
        map_insert(m, &i, &val);
    }
    int black_height;
    if (!validate_rb_properties(get_root(m), &black_height)) {
        fprintf(stderr, "Tree invalid after refilling from the free list\n");
        map_delete(m);
        return 1;
    }
    map_iter_t it;
    int expect = 0;
    for (map_first(m, &it); !map_at_end(&it); map_next(&it)) {
        if (map_iter_key(&it, int) != expect ||
            map_iter_value(&it, int) != expect * 9) {
            fprintf(stderr, "Recycled node holds stale contents at %d\n",
                    expect);
            map_delete(m);
            return 1;
        }
        expect++;
    }
    if (expect != 128) {
        fprintf(stderr, "Refilled map has %d entries, expected 128\n", expect);
        map_delete(m);
        return 1;
    }

    map_delete(m);
    printf(" " COLOR_GREEN "[OK]" COLOR_RESET "\n");
    return 0;
}

/* Randomized differential test.
 *
 * The tree is only interesting once insertions and erasures interleave: an
 * erasure can leave shapes that a later insertion has to cope with, and that
 * combination is what distinguishes a correct fixup pair from one that merely
 * survives monotone workloads. Every operation is mirrored into a flat shadow
 * array, and the whole structure is re-validated against it.
 */
static int validate_against_model(map_t m, const char *model, int nkeys)
{
    int black_height;
    map_node_t *root = get_root(m);
    if (root && root->parent) {
        fprintf(stderr, "Root has a parent\n");
        return 0;
    }
    if (root && ((uintptr_t) root->right_red & 1)) {
        fprintf(stderr, "Root is not black\n");
        return 0;
    }
    if (!validate_rb_properties(root, &black_height))
        return 0;

    size_t expected = 0;
    for (int i = 0; i < nkeys; i++)
        expected += model[i] != 0;
    if (map_size(m) != expected) {
        fprintf(stderr, "Size %zu, expected %zu\n", map_size(m), expected);
        return 0;
    }
    if (map_empty(m) != (expected == 0)) {
        fprintf(stderr, "map_empty() disagrees with map_size()\n");
        return 0;
    }

    /* Forward traversal must reproduce the model in ascending key order, so
     * this covers ordering, the cached first node, and every parent link a
     * successor walk touches.
     */
    map_iter_t it;
    int idx = 0;
    for (map_first(m, &it); !map_at_end(&it); map_next(&it)) {
        while (idx < nkeys && !model[idx])
            idx++;
        if (idx == nkeys) {
            fprintf(stderr, "Forward traversal yielded extra keys\n");
            return 0;
        }
        if (map_iter_key(&it, int) != idx ||
            map_iter_value(&it, int) != idx * 3) {
            fprintf(stderr, "Forward traversal mismatch at key %d\n", idx);
            return 0;
        }
        idx++;
    }
    while (idx < nkeys && !model[idx])
        idx++;
    if (idx != nkeys) {
        fprintf(stderr, "Forward traversal ended early\n");
        return 0;
    }

    idx = nkeys - 1;
    for (map_last(m, &it); !map_at_end(&it); map_prev(&it)) {
        while (idx >= 0 && !model[idx])
            idx--;
        if (idx < 0) {
            fprintf(stderr, "Backward traversal yielded extra keys\n");
            return 0;
        }
        if (map_iter_key(&it, int) != idx) {
            fprintf(stderr, "Backward traversal mismatch at key %d\n", idx);
            return 0;
        }
        idx--;
    }
    while (idx >= 0 && !model[idx])
        idx--;
    if (idx != -1) {
        fprintf(stderr, "Backward traversal ended early\n");
        return 0;
    }

    for (int k = 0; k < nkeys; k++) {
        int want = -1;
        for (int i = k; i < nkeys; i++)
            if (model[i]) {
                want = i;
                break;
            }
        map_ceil(m, &it, &k);
        if ((map_at_end(&it) ? -1 : map_iter_key(&it, int)) != want) {
            fprintf(stderr, "map_ceil(%d) mismatch\n", k);
            return 0;
        }

        want = -1;
        for (int i = k; i >= 0; i--)
            if (model[i]) {
                want = i;
                break;
            }
        map_floor(m, &it, &k);
        if ((map_at_end(&it) ? -1 : map_iter_key(&it, int)) != want) {
            fprintf(stderr, "map_floor(%d) mismatch\n", k);
            return 0;
        }
    }
    return 1;
}

static int test_random_operations(void)
{
    printf("  Testing randomized insert/erase interleaving...");

    enum { NKEYS = 256, ROUNDS = 20000 };
    char model[NKEYS] = {0};
    map_t m = map_init(int, int, map_cmp_int);
    if (!m) {
        fprintf(stderr, "Failed to create map\n");
        return 1;
    }

    for (int round = 0; round < ROUNDS; round++) {
        int key = mt19937_extract() % NKEYS;
        int val = key * 3;
        bool inserting = mt19937_extract() & 1;

        if (inserting) {
            if (map_insert(m, &key, &val) == (model[key] != 0)) {
                fprintf(stderr, "\nInsert of %d disagrees with model\n", key);
                goto fail;
            }
            model[key] = 1;
        } else {
            map_iter_t it;
            map_find(m, &it, &key);
            if (map_at_end(&it) != !model[key]) {
                fprintf(stderr, "\nFind of %d disagrees with model\n", key);
                goto fail;
            }
            if (!map_at_end(&it)) {
                map_erase(&it);
                model[key] = 0;
                if (!map_at_end(&it)) {
                    fprintf(stderr, "\nIterator alive after erase\n");
                    goto fail;
                }
            }
        }

        /* Full validation is O(n), so amortize it rather than run it every
         * round; a broken fixup survives only a handful of operations.
         */
        if (round % 32 == 0 && !validate_against_model(m, model, NKEYS)) {
            fprintf(stderr, "Validation failed at round %d\n", round);
            goto fail;
        }
    }

    if (!validate_against_model(m, model, NKEYS)) {
        fprintf(stderr, "Final validation failed\n");
        goto fail;
    }

    map_delete(m);
    printf(" " COLOR_GREEN "[OK]" COLOR_RESET " (%d operations)\n", ROUNDS);
    return 0;

fail:
    map_delete(m);
    return 1;
}

/* Stress tests */

#define STRESS_SIZE 100000

/* Peak resident set size in KB. ru_maxrss is bytes on Darwin and kilobytes
 * everywhere else, so the reported footprint was off by 1024x on Linux.
 */
static long get_memory_usage(void)
{
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
    return usage.ru_maxrss / 1024;
#else
    return usage.ru_maxrss;
#endif
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
        if (map_at_end(&it) || map_iter_value(&it, int) != i * 7) {
            fprintf(stderr, "Value mismatch at %d\n", i);
            map_delete(m);
            return 1;
        }
    }

    for (int i = 0; i < STRESS_SIZE; i += 2) {
        map_iter_t it;
        map_find(m, &it, &i);
        if (!map_at_end(&it))
            map_erase(&it);
    }

    for (int i = 1; i < STRESS_SIZE; i += 2) {
        map_iter_t it;
        map_find(m, &it, &i);
        if (map_at_end(&it)) {
            fprintf(stderr, "Element %d missing after deletion\n", i);
            map_delete(m);
            return 1;
        }
    }

    int black_height;
    if (!validate_rb_properties(get_root(m), &black_height)) {
        fprintf(stderr, "Tree invalid after deletion\n");
        map_delete(m);
        return 1;
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
        uint64_t checksum = 0;
        for (int i = 0; i < size; i++) {
            map_iter_t it;
            map_find(m, &it, &i);
            if (map_at_end(&it)) {
                fprintf(stderr, "Lookup missed key %d\n", i);
                map_delete(m);
                return 1;
            }
            checksum += map_iter_value(&it, int);
        }
        long lookup_time = get_time_us() - start;

        if (checksum != (uint64_t) size * (size - 1) / 2) {
            fprintf(stderr, "Lookup checksum mismatch\n");
            map_delete(m);
            return 1;
        }

        start = get_time_us();
        checksum = 0;
        map_iter_t it;
        for (map_first(m, &it); !map_at_end(&it); map_next(&it))
            checksum += map_iter_value(&it, int);
        long iterate_time = get_time_us() - start;

        if (checksum != (uint64_t) size * (size - 1) / 2) {
            fprintf(stderr, "Iteration checksum mismatch\n");
            map_delete(m);
            return 1;
        }

        start = get_time_us();
        for (int i = 0; i < size; i++) {
            map_find(m, &it, &i);
            map_erase(&it);
        }
        long erase_time = get_time_us() - start;

        printf(
            "\n    Size %6d: insert %.2fms (%.1fM ops/s), lookup %.2fms (%.1fM "
            "ops/s)",
            size, insert_time / 1000.0,
            insert_time > 0 ? (double) size / insert_time : 0,
            lookup_time / 1000.0,
            lookup_time > 0 ? (double) size / lookup_time : 0);
        printf(
            "\n                 iterate %.2fms (%.1fM ops/s), erase %.2fms "
            "(%.1fM ops/s)",
            iterate_time / 1000.0,
            iterate_time > 0 ? (double) size / iterate_time : 0,
            erase_time / 1000.0,
            erase_time > 0 ? (double) size / erase_time : 0);

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
    if (!map_at_end(&it)) {
        fprintf(stderr, "First iterator not at end for empty map\n");
        map_delete(m);
        return 1;
    }

    map_last(m, &it);
    if (!map_at_end(&it)) {
        fprintf(stderr, "Last iterator not at end for empty map\n");
        map_delete(m);
        return 1;
    }

    int key = 42;
    map_find(m, &it, &key);
    if (!map_at_end(&it)) {
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
    if (map_at_end(&it) || map_iter_value(&it, int) != min_val) {
        fprintf(stderr, "INT_MIN value mismatch\n");
        map_delete(m);
        return 1;
    }

    map_find(m, &it, &max_key);
    if (map_at_end(&it) || map_iter_value(&it, int) != max_val) {
        fprintf(stderr, "INT_MAX value mismatch\n");
        map_delete(m);
        return 1;
    }

    map_find(m, &it, &zero_key);
    if (map_at_end(&it) || map_iter_value(&it, int) != zero_val) {
        fprintf(stderr, "Zero value mismatch\n");
        map_delete(m);
        return 1;
    }

    map_first(m, &it);
    if (map_iter_key(&it, int) != INT_MIN) {
        fprintf(stderr, "First key is not INT_MIN\n");
        map_delete(m);
        return 1;
    }

    map_last(m, &it);
    if (map_iter_key(&it, int) != INT_MAX) {
        fprintf(stderr, "Last key is not INT_MAX\n");
        map_delete(m);
        return 1;
    }

    map_next(&it);
    if (!map_at_end(&it)) {
        fprintf(stderr, "Iterator advanced past last key\n");
        map_delete(m);
        return 1;
    }

    int below_zero = -1;
    map_ceil(m, &it, &below_zero);
    if (map_iter_key(&it, int) != 0) {
        fprintf(stderr, "Ceil mismatch\n");
        map_delete(m);
        return 1;
    }
    map_floor(m, &it, &below_zero);
    if (map_iter_key(&it, int) != INT_MIN) {
        fprintf(stderr, "Floor mismatch\n");
        map_delete(m);
        return 1;
    }

    int replacement = 42;
    if (!map_set(m, &zero_key, &replacement)) {
        fprintf(stderr, "Failed to replace existing value\n");
        map_delete(m);
        return 1;
    }
    map_find(m, &it, &zero_key);
    if (map_iter_value(&it, int) != replacement || map_size(m) != 3) {
        fprintf(stderr, "Replacement changed map structure\n");
        map_delete(m);
        return 1;
    }

    map_find(m, &it, &min_key);
    map_erase(&it);
    map_first(m, &it);
    if (map_iter_key(&it, int) != 0) {
        fprintf(stderr, "First-key cache stale after erase\n");
        map_delete(m);
        return 1;
    }

    map_find(m, &it, &max_key);
    map_erase(&it);
    map_last(m, &it);
    if (map_iter_key(&it, int) != 0) {
        fprintf(stderr, "Last-key cache stale after erase\n");
        map_delete(m);
        return 1;
    }

    map_delete(m);
    printf(" " COLOR_GREEN "[OK]" COLOR_RESET "\n");
    return 0;
}

int main(int argc, char *argv[])
{
    mt19937_init(argc > 1 ? strtoull(argv[1], NULL, 0) : UINT64_C(0x52425452));

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

    if (test_mixed_rebalancing())
        failed = 1;

    if (test_overaligned_payloads())
        failed = 1;

    if (test_random_operations())
        failed = 1;

    if (test_allocator_lifecycle())
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
