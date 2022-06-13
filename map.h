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

enum { _CMP_LESS = -1, _CMP_EQUAL = 0, _CMP_GREATER = 1 };

/* Integer comparison */
static inline int map_cmp_int(void *arg0, void *arg1)
{
    int *a = (int *) arg0, *b = (int *) arg1;
    return (*a < *b) ? _CMP_LESS : (*a > *b) ? _CMP_GREATER : _CMP_EQUAL;
}

/* Unsigned integer comparison */
static inline int map_cmp_uint(void *arg0, void *arg1)
{
    unsigned int *a = (unsigned int *) arg0, *b = (unsigned int *) arg1;
    return (*a < *b) ? _CMP_LESS : (*a > *b) ? _CMP_GREATER : _CMP_EQUAL;
}

typedef enum { C_MAP_RED, C_MAP_BLACK, C_MAP_DOUBLE_BLACK } map_color_t;

/*
 * Store the key, data, and values of each element in the tree.
 * This is the main basis of the entire tree aside from the root struct.
 */
typedef struct map_node {
    void *key, *data;
    struct map_node *left, *right, *up;
    map_color_t color;
} map_node_t;

typedef struct {
    struct map_node *prev, *node;
    size_t count;
} map_iter_t;

/*
 * Store access to the head node, as well as the first and last nodes.
 * Keep track of all aspects of the tree. All map functions require a pointer
 * to this struct.
 */
typedef struct map_internal *map_t;

/* Constructor */
map_t map_new(size_t, size_t, int (*)(void *, void *));

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

#define map_iter_value(it, type) (*(type *) (it)->node->data)
