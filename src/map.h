/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* Cache-efficient red-black tree map implementation.
 *
 * This implementation is optimized for minimal memory overhead while providing
 * O(log n) insertion, deletion, and lookup operations. The design is inspired
 * by the Linux kernel's intrusive data structures.
 *
 * Key features:
 * - Color bit stored in the least significant bit of the right-child pointer
 * - Parent links provide fast erase and ordered traversal
 * - Keys and values share the node allocation
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Red-black tree node structure.
 *
 * Memory layout optimized to reduce overhead:
 * - Color bit encoded in LSB of right_red
 * - Key and value bytes stored inline without per-node payload pointers
 * - Map-owned chunks keep nodes compact and improve allocation locality
 *
 * The key offset lives in the map, keeping the persistent node header to three
 * pointers while retaining parent-linked erase and traversal.
 */
typedef struct map_node {
    struct map_node *left, *right_red, *parent;
} map_node_t;

/* Comparison result enumeration */
typedef enum {
    MAP_CMP_LESS = -1,
    MAP_CMP_EQUAL = 0,
    MAP_CMP_GREATER = 1
} map_cmp_t;

/* Opaque map handle */
typedef struct map_internal *map_t;

/* Iterator for tree traversal.
 *
 * The owning map is carried alongside the node so that map_erase() cannot be
 * handed an iterator belonging to a different map.
 */
typedef struct {
    map_t map;        /* Owning map */
    map_node_t *node; /* Current node */
} map_iter_t;

/* Address of the key and value stored in the node an iterator points at.
 *
 * Both offsets come from the map, which is what makes the typed macros below
 * safe for over-aligned types: map_new_aligned() honors any power-of-two
 * alignment, and the accessor reads back the layout the map was built with
 * rather than one re-derived from the type named at the call site.
 *
 * Use the _ptr forms when you need the stored object's address, for example to
 * hand a caller a pointer into the map. Both return NULL for an end iterator.
 * The key is const because it is the tree's ordering state: rewriting it in
 * place would leave the map unsorted. Values may be modified freely.
 */
const void *map_iter_key_ptr(const map_iter_t *it);
void *map_iter_value_ptr(const map_iter_t *it);

#define map_iter_value(it, type) (*(type *) map_iter_value_ptr(it))
#define map_iter_key(it, type) (*(type const *) map_iter_key_ptr(it))

/* Built-in comparators are specialized by the map implementation. */
map_cmp_t map_cmp_int(const void *arg0, const void *arg1);
map_cmp_t map_cmp_uint(const void *arg0, const void *arg1);

/* Constructor - creates a new map instance
 * @key_size: Size of key type in bytes
 * @data_size: Size of value type in bytes
 * @cmp: Comparison function for ordering keys. Any sign convention
 *       works, results are normalized, so memcmp() and strcmp() can be
 *       passed directly.
 * @return: New map instance or NULL on allocation failure
 */
map_t map_new(size_t key_size,
              size_t data_size,
              map_cmp_t (*cmp)(const void *, const void *));

/* Alignment-aware constructor used by map_init(). */
map_t map_new_aligned(size_t key_size,
                      size_t key_align,
                      size_t data_size,
                      size_t data_align,
                      map_cmp_t (*cmp)(const void *, const void *));

/* Insert a key-value pair into the map
 * @obj: Map instance
 * @key: Pointer to key data
 * @val: Pointer to value data, or NULL to zero-fill the value
 * @return: true if inserted. false covers both a key that already exists and a
 *          failed node allocation, so a caller that must tell them apart
 *          should look the key up first, or use map_set(), whose false means
 *          allocation failure only.
 */
bool map_insert(map_t obj, const void *key, const void *val);

/* Insert a key-value pair or replace the value of an existing key.
 * @return: true on success. Replacing an existing key cannot fail, so false
 *          means the node allocation failed.
 */
bool map_set(map_t obj, const void *key, const void *val);

/* Find a key in the map
 * @obj: Map instance
 * @it: Iterator to store result
 * @key: Key to search for
 */
void map_find(map_t obj, map_iter_t *it, const void *key);

/* Find the smallest key greater than or equal to key.
 *
 * Deliberately not named map_lower_bound(): the companion map_floor() has no
 * standard-library counterpart, and a lower_bound/floor pair reads as if the
 * two were opposites when they are not.
 */
void map_ceil(map_t obj, map_iter_t *it, const void *key);

/* Find the greatest key less than or equal to key. */
void map_floor(map_t obj, map_iter_t *it, const void *key);

/* Check if map is empty
 * @obj: Map instance
 * @return: true if empty, false otherwise
 */
bool map_empty(map_t obj);

/* Check if iterator is at end
 * @it: Iterator to check
 * @return: true if at end, false if valid
 */
bool map_at_end(const map_iter_t *it);

/* Remove the node at the iterator position and invalidate the iterator
 *
 * Every reference to the erased node dies with it: other iterators positioned
 * there, and any pointer from map_iter_key_ptr() or map_iter_value_ptr(). The
 * storage is recycled by later insertions, so a stale reference silently reads
 * whichever entry now occupies it rather than failing. map_clear() and
 * map_delete() invalidate every iterator and pointer into the map.
 *
 * @it: Iterator pointing to node to remove
 */
void map_erase(map_iter_t *it);

/* Remove all nodes from map
 * @obj: Map instance
 */
void map_clear(map_t obj);

/* Destroy map and free all resources
 * @obj: Map instance to destroy
 */
void map_delete(map_t obj);

/* Convenience macro for map initialization with type safety */
#define map_init(key_type, element_type, cmp_func)        \
    map_new_aligned(sizeof(key_type), _Alignof(key_type), \
                    sizeof(element_type), _Alignof(element_type), cmp_func)

/* Get size of map (number of elements)
 * @obj: Map instance
 * @return: Number of elements in map
 */
size_t map_size(map_t obj);

/* Get iterator to first element (smallest key)
 * @map: Map instance
 * @it: Iterator to initialize
 */
void map_first(map_t map, map_iter_t *it);

/* Get iterator to last element (largest key)
 * @map: Map instance
 * @it: Iterator to initialize
 */
void map_last(map_t map, map_iter_t *it);

/* Move iterator to next element (in-order traversal)
 * @it: Iterator to advance
 */
void map_next(map_iter_t *it);

/* Move iterator to previous element (reverse in-order traversal)
 * @it: Iterator to move back
 */
void map_prev(map_iter_t *it);
