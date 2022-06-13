/*
 * C Implementation for C++ std::map using red-black tree.
 *
 * Any data type can be stored in a c_map, just like std::map.
 * A c_map instance requires the specification of two file types.
 *   1. the key;
 *   2. what data type the tree node will store;
 * It will also require a comparison function to sort the tree.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

enum { _CMP_LESS = -1, _CMP_EQUAL = 0, _CMP_GREATER = 1 };

/* Integer comparison */
static inline int c_map_cmp_int(void *arg0, void *arg1)
{
    int *a = (int *) arg0, *b = (int *) arg1;
    return (*a < *b) ? _CMP_LESS : (*a > *b) ? _CMP_GREATER : _CMP_EQUAL;
}

/* Unsigned integer comparison */
static inline int c_map_cmp_uint(void *arg0, void *arg1)
{
    unsigned int *a = (unsigned int *) arg0, *b = (unsigned int *) arg1;
    return (*a < *b) ? _CMP_LESS : (*a > *b) ? _CMP_GREATER : _CMP_EQUAL;
}

typedef enum { C_MAP_RED, C_MAP_BLACK, C_MAP_DOUBLE_BLACK } c_map_color_t;

/*
 * Store the key, data, and values of each element in the tree.
 * This is the main basis of the entire tree aside from the root struct.
 */
typedef struct c_map_node {
    void *key, *data;
    struct c_map_node *left, *right, *up;
    c_map_color_t color;
} c_map_node_t;

typedef struct {
    struct c_map_node *prev, *node;
    size_t count;
} c_map_iter_t;

/*
 * Store access to the head node, as well as the first and last nodes.
 * Keep track of all aspects of the tree. All c_map functions require a pointer
 * to this struct.
 */
typedef struct c_map_internal *c_map_t;

/* Constructor */
c_map_t c_map_new(size_t, size_t, int (*)(void *, void *));

/* Add function */
bool c_map_insert(c_map_t, void *, void *);

/* Get functions */
void c_map_find(c_map_t, c_map_iter_t *, void *);
bool c_map_empty(c_map_t);

/* Iteration */
bool c_map_at_end(c_map_t, c_map_iter_t *);

/* Remove functions */
void c_map_erase(c_map_t, c_map_iter_t *);
void c_map_clear(c_map_t);

/* Destructor */
void c_map_delete(c_map_t);

#define c_map_init(key_type, element_type, __func) \
    c_map_new(sizeof(key_type), sizeof(element_type), __func)

#define c_map_iter_value(it, type) (*(type *) (it)->node->data)
