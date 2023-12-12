#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "map.h"
#include "mt19937.h"

static void swap(int *x, int *y)
{
    int tmp = *x;
    *x = *y;
    *y = tmp;
}

enum { N_NODES = 10000 };

/* return 0 on success; non-zero values on failure */
static int test_map_mixed_operations(void)
{
    int ret = 0;
    map_t tree = map_init(int, int, map_cmp_int);

    int key[N_NODES], val[N_NODES];

    /*
     *  Generate data for insertion
     */
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

    mt19937_init(time(NULL));

    return test_map_mixed_operations();
}
