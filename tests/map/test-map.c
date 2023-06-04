#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "map.h"

static void swap(int *x, int *y)
{
    int tmp = *x;
    *x = *y;
    *y = tmp;
}

enum { N_NODES = 10000 };

/* return 0 on success; non-zero values on failure */
static int test_map_mixed_operations()
{
    int ret = 0;
    map_t tree = map_init(int, int, map_cmp_uint);

    int key[N_NODES], val[N_NODES];

    /*
     *  Generate data for insertion
     */
    for (int i = 0; i < N_NODES; i++) {
        key[i] = i;
        val[i] = i + 1;
    }

    /* TODO: This is not a reconmended way to randomize stuff, just a simple
     * test. Using MT19937 might be better
     */
    for (int i = 0; i < N_NODES; i++) {
        int pos_a = rand() % N_NODES;
        int pos_b = rand() % N_NODES;
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
            ret = 1; /* test fail */
            goto free_tree;
        }
        assert((*(int *) (my_it.node->data)) == val[i]);
    }


    /* remove 2nd quarter of items */
    for (int i = N_NODES / 4 + 1; i < N_NODES / 2; i++) {
        map_iter_t my_it;
        map_find(tree, &my_it, key + i);
        if (map_at_end(tree, &my_it)) {
            ret = 1; /* test fail */
            goto free_tree;
        }
        map_erase(tree, &my_it);
        map_find(tree, &my_it, key + i);
        if (my_it.node) {
            ret = 1; /* test fail */
            goto free_tree;
        }
    }

free_tree:
    map_clear(tree);
    map_delete(tree);
    return ret;
}

int main(int argc, char *argv[])
{
    (void) argc;
    (void) argv;

    srand((unsigned) time(NULL));

    return test_map_mixed_operations();
}
