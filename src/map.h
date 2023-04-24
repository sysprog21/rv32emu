/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/*
 * C Implementation for C++ std::map using red-black tree.
 *
 * Any data type can be stored in a map, just like std::map.
 * A map instance requires the specification of two file types:
 *   1. the key;
 *   2. what data type the tree node will store;
 *
 * It will also require a comparison function to sort the tree.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct map_node map_node_t;
struct map_node {
    void *key;
    void *val;
    struct {
        map_node_t *left, *right_red;
    } link;
};

typedef struct {
    map_node_t *root;
    int (*cmp)(const void *, const void *);
    size_t key_size;
    size_t val_size;
} map_head_t;

typedef map_head_t *map_t;

typedef struct {
    map_node_t *prev, *node;
    size_t count;
} map_iter_t;

#define map_iter_value(it, type) (*(type *) (it)->node->val)

enum { _CMP_LESS = -1, _CMP_EQUAL = 0, _CMP_GREATER = 1 };

typedef enum { RB_BLACK = 0, RB_RED } map_color_t;

/* Integer comparison */
static inline int map_cmp_int(const void *arg0, const void *arg1)
{
    int *a = (int *) arg0;
    int *b = (int *) arg1;
    return (*a < *b) ? _CMP_LESS : (*a > *b) ? _CMP_GREATER : _CMP_EQUAL;
}

/* Unsigned integer comparison */
static inline int map_cmp_uint(const void *arg0, const void *arg1)
{
    unsigned int *a = (unsigned int *) arg0;
    unsigned int *b = (unsigned int *) arg1;
    return (*a < *b) ? _CMP_LESS : (*a > *b) ? _CMP_GREATER : _CMP_EQUAL;
}

/* Constructor */
map_t map_new(size_t, size_t, int (*)(const void *, const void *));

/* Add function */
bool map_insert(map_t, void *, void *);

/* Get functions */
void map_find(map_t, map_iter_t *, void *);
bool map_empty(map_t);

/* Iteration */
bool map_at_end(map_t, map_iter_t *);

/* Remove functions */
void map_erase(map_t, map_iter_t *);
void map_clear(map_t);

/* Destructor */
void map_delete(map_t);

#define map_init(key_type, element_type, __func) \
    map_new(sizeof(key_type), sizeof(element_type), __func)
