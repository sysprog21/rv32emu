/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* Memory-efficient red-black tree map implementation.
 *
 * This implementation is optimized for minimal memory overhead while providing
 * O(log n) insertion, deletion, and lookup operations. The design is inspired
 * by the Linux kernel's intrusive data structures and jemalloc's rb.h.
 *
 * Key features:
 * - Color bit stored in least significant bit of pointer (2 pointers per node)
 * - No parent pointer - uses stack-based traversal instead
 * - Support for any data type through generic key/value storage
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Red-black tree node structure.
 *
 * Memory layout optimized to reduce overhead:
 * - Color bit encoded in LSB of right_red pointer
 * - No parent pointer (uses traversal stack instead)
 * - Key and data stored as flexible pointers
 * - Single allocation strategy for node+key+data improves cache locality
 *
 * This design achieves minimal memory footprint (2 pointers + payload)
 * while providing O(log n) operations with excellent cache performance.
 */
typedef struct map_node {
    void *key, *data;           /* Pointer to key/value data */
    struct map_node *left;      /* Left child */
    struct map_node *right_red; /* Right child + color bit in LSB */
} map_node_t;

/* Verify pointer alignment for color bit storage */
#ifdef __STDC_VERSION__
#if __STDC_VERSION__ >= 201112L
_Static_assert(_Alignof(void *) >= 2,
               "Pointer alignment insufficient for color bit storage");
#endif
#endif

/* Comparison result enumeration */
typedef enum {
    MAP_CMP_LESS = -1,
    MAP_CMP_EQUAL = 0,
    MAP_CMP_GREATER = 1
} map_cmp_t;

/* Opaque map handle */
typedef struct map_internal *map_t;

/* Iterator for tree traversal */
typedef struct {
    map_node_t *node; /* Current node */
    map_node_t *prev; /* Previous node (for deletion safety) */
    size_t count;     /* Iteration count */
} map_iter_t;

#define map_iter_value(it, type) (*(type *) (it)->node->data)
#define map_iter_key(it, type) (*(type *) (it)->node->key)

/* Integer comparison - optimized branchless version */
FORCE_INLINE map_cmp_t map_cmp_int(const void *arg0, const void *arg1)
{
    const int a = *(const int *) arg0;
    const int b = *(const int *) arg1;
    return (map_cmp_t) ((a > b) - (a < b));
}

/* Unsigned integer comparison - optimized branchless version */
FORCE_INLINE map_cmp_t map_cmp_uint(const void *arg0, const void *arg1)
{
    const unsigned int a = *(const unsigned int *) arg0;
    const unsigned int b = *(const unsigned int *) arg1;
    return (map_cmp_t) ((a > b) - (a < b));
}

/* Constructor - creates a new map instance
 * @param key_size: Size of key type in bytes
 * @param data_size: Size of value type in bytes
 * @param cmp: Comparison function for ordering keys
 * @return: New map instance or NULL on allocation failure
 */
map_t map_new(size_t key_size,
              size_t data_size,
              map_cmp_t (*cmp)(const void *, const void *));

/* Insert a key-value pair into the map
 * @param obj: Map instance
 * @param key: Pointer to key data
 * @param val: Pointer to value data
 * @return: true if inserted, false if key already exists
 */
bool map_insert(map_t obj, const void *key, const void *val);

/* Find a key in the map
 * @param obj: Map instance
 * @param it: Iterator to store result
 * @param key: Key to search for
 */
void map_find(map_t obj, map_iter_t *it, const void *key);

/* Check if map is empty
 * @param obj: Map instance
 * @return: true if empty, false otherwise
 */
bool map_empty(map_t obj);

/* Check if iterator is at end
 * @param m: Map instance
 * @param it: Iterator to check
 * @return: true if at end, false if valid
 */
bool map_at_end(map_t m, const map_iter_t *it);

/* Remove node at iterator position
 * @param obj: Map instance
 * @param it: Iterator pointing to node to remove
 */
void map_erase(map_t obj, map_iter_t *it);

/* Remove all nodes from map
 * @param obj: Map instance
 */
void map_clear(map_t obj);

/* Destroy map and free all resources
 * @param obj: Map instance to destroy
 */
void map_delete(map_t obj);

/* Convenience macro for map initialization with type safety */
#define map_init(key_type, element_type, cmp_func) \
    map_new(sizeof(key_type), sizeof(element_type), cmp_func)

/* Get size of map (number of elements)
 * @param obj: Map instance
 * @return: Number of elements in map
 */
size_t map_size(map_t obj);

/* Get iterator to first element (smallest key)
 * @param map: Map instance
 * @param it: Iterator to initialize
 */
void map_first(map_t map, map_iter_t *it);

/* Get iterator to last element (largest key)
 * @param map: Map instance
 * @param it: Iterator to initialize
 */
void map_last(map_t map, map_iter_t *it);

/* Move iterator to next element (in-order traversal)
 * @param map: Map instance
 * @param it: Iterator to advance
 */
void map_next(map_t map, map_iter_t *it);

/* Move iterator to previous element (reverse in-order traversal)
 * @param map: Map instance
 * @param it: Iterator to move back
 */
void map_prev(map_t map, map_iter_t *it);
