/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "map.h"
#include "utils.h"

/* Chunks only need to be reachable for freeing; the cursor that carves nodes
 * out of the newest one lives in the map, since that is the only chunk it ever
 * applies to. The header is one pointer and node_align is at least that, so the
 * first node sits exactly node_align bytes into the chunk.
 */
typedef struct map_chunk {
    struct map_chunk *next;
} map_chunk_t;

_Static_assert(sizeof(map_chunk_t) <= sizeof(void *),
               "Chunk header must fit in the first node_align bytes");

struct map_internal {
    map_node_t *root, *first, *last;
    map_node_t *free_nodes;
    map_chunk_t *chunks;
    char *bump, *bump_end;
    size_t key_size, data_size, key_offset, data_offset, node_align;
    size_t node_stride, size;
    map_cmp_t (*comparator)(const void *, const void *);
};

typedef enum { RB_BLACK = 0, RB_RED = 1 } map_color_t;

#define RB_COLOR_MASK ((uintptr_t) 1)

/* Stealing the low bit of a child pointer only works while node addresses have
 * it to spare. Nodes are handed out at node_align, which map_new_aligned()
 * floors at sizeof(void *). That floor also covers the node header itself,
 * whose pointer members cannot need a stricter alignment than their size.
 */
_Static_assert(sizeof(void *) > RB_COLOR_MASK &&
                   !(sizeof(void *) & (sizeof(void *) - 1)) &&
                   _Alignof(map_node_t) <= sizeof(void *),
               "Node alignment leaves no room for the color bit");

map_cmp_t map_cmp_int(const void *arg0, const void *arg1)
{
    const int a = *(const int *) arg0;
    const int b = *(const int *) arg1;
    return (map_cmp_t) ((a > b) - (a < b));
}

map_cmp_t map_cmp_uint(const void *arg0, const void *arg1)
{
    const unsigned int a = *(const unsigned int *) arg0;
    const unsigned int b = *(const unsigned int *) arg1;
    return (map_cmp_t) ((a > b) - (a < b));
}

/* Comparing through the function pointer costs an indirect call per tree level,
 * which also pins the comparator load inside the descent loop. Taking the
 * comparator as an argument lets each caller load it once into a register
 * before descending, and recognizing the built-ins turns the common case into
 * an inlined compare.
 */
static inline map_cmp_t map_compare(map_cmp_t (*cmp)(const void *,
                                                     const void *),
                                    const void *left,
                                    const void *right)
{
    if (likely(cmp == map_cmp_int))
        return map_cmp_int(left, right);
    if (likely(cmp == map_cmp_uint))
        return map_cmp_uint(left, right);

    /* The descent compares against the enumerators, so a comparator that
     * reports magnitude rather than sign (memcmp, strcmp) has to be folded back
     * to -1/0/1. The built-ins above already are, and skip this.
     */
    const int result = cmp(left, right);
    return result < 0 ? MAP_CMP_LESS
                      : (result > 0 ? MAP_CMP_GREATER : MAP_CMP_EQUAL);
}

static inline void *node_key(map_t map, map_node_t *node)
{
    return (char *) node + map->key_offset;
}

static inline void *node_data(map_t map, map_node_t *node)
{
    return (char *) node + map->data_offset;
}

/* Unlike node_color(), these take a live node: every call site either holds a
 * loop invariant or has already tested the link. node_color() and
 * node_set_color() keep their NULL checks because the fixups rely on a NULL
 * child reading as black.
 */
static inline map_node_t *node_right(const map_node_t *node)
{
    return (map_node_t *) ((uintptr_t) node->right_red & ~RB_COLOR_MASK);
}

static inline void node_set_right(map_node_t *node, map_node_t *right)
{
    node->right_red =
        (map_node_t *) ((uintptr_t) right |
                        ((uintptr_t) node->right_red & RB_COLOR_MASK));
}

static inline map_color_t node_color(const map_node_t *node)
{
    return node ? (map_color_t) ((uintptr_t) node->right_red & RB_COLOR_MASK)
                : RB_BLACK;
}

static inline void node_set_color(map_node_t *node, map_color_t color)
{
    if (node)
        node->right_red = (map_node_t *) ((uintptr_t) node_right(node) | color);
}

static inline map_node_t *node_min(map_node_t *node)
{
    while (node->left)
        node = node->left;
    return node;
}

static inline map_node_t *node_max(map_node_t *node)
{
    map_node_t *right;
    while ((right = node_right(node)))
        node = right;
    return node;
}

static map_node_t *node_next(map_node_t *node)
{
    if (node_right(node))
        return node_min(node_right(node));

    map_node_t *parent = node->parent;
    while (parent && node == node_right(parent)) {
        node = parent;
        parent = parent->parent;
    }
    return parent;
}

static map_node_t *node_prev(map_node_t *node)
{
    if (node->left)
        return node_max(node->left);

    map_node_t *parent = node->parent;
    while (parent && node == parent->left) {
        node = parent;
        parent = parent->parent;
    }
    return parent;
}

static void transplant(map_t map, map_node_t *old, map_node_t *replacement)
{
    if (!old->parent)
        map->root = replacement;
    else if (old == old->parent->left)
        old->parent->left = replacement;
    else
        node_set_right(old->parent, replacement);
    if (replacement)
        replacement->parent = old->parent;
}

static void rotate_left(map_t map, map_node_t *node)
{
    map_node_t *right = node_right(node);
    map_node_t *middle = right->left;

    node_set_right(node, middle);
    if (middle)
        middle->parent = node;

    transplant(map, node, right);
    right->left = node;
    node->parent = right;
}

static void rotate_right(map_t map, map_node_t *node)
{
    map_node_t *left = node->left;
    map_node_t *middle = node_right(left);

    node->left = middle;
    if (middle)
        middle->parent = node;

    transplant(map, node, left);
    node_set_right(left, node);
    node->parent = left;
}

static bool valid_alignment(size_t align)
{
    return align && !(align & (align - 1));
}

/* align_up() cannot report the overflow, so guard it here and let the caller
 * fail the construction rather than wrap around.
 */
static bool align_offset(size_t offset, size_t align, size_t *result)
{
    if (offset > SIZE_MAX - (align - 1))
        return false;
    *result = align_up(offset, align);
    return true;
}

/* Chunks stay allocated until map_clear(); erased nodes return to the free list
 * rather than to the allocator. Per-chunk live counts would be needed only if
 * long-lived maps commonly shrink without being refilled.
 */
static bool grow_chunks(map_t map)
{
    size_t capacity = 16384 / map->node_stride;
    if (!capacity)
        capacity = 1;
    if (capacity > (SIZE_MAX - map->node_align) / map->node_stride)
        return false;
    size_t alloc_size = map->node_align + capacity * map->node_stride;

    map_chunk_t *chunk = map->node_align <= _Alignof(max_align_t)
                             ? malloc(alloc_size)
                             : aligned_alloc(map->node_align, alloc_size);
    if (!chunk)
        return false;

    chunk->next = map->chunks;
    map->chunks = chunk;
    map->bump = (char *) chunk + map->node_align;
    map->bump_end = map->bump + capacity * map->node_stride;
    return true;
}

static map_node_t *alloc_node(map_t map)
{
    if (map->free_nodes) {
        map_node_t *node = map->free_nodes;
        map->free_nodes = node->left;
        return node;
    }

    if (map->bump == map->bump_end && !grow_chunks(map))
        return NULL;

    /* The chunk is allocated at node_align and both the header offset and
     * node_stride are multiples of it, so the cursor is always aligned. Cast
     * through void * to say so: the compiler only sees char * arithmetic and
     * would otherwise warn under -Wcast-align.
     */
    map_node_t *node = (map_node_t *) (void *) map->bump;
    map->bump += map->node_stride;
    return node;
}

/* memmove because a replacement value may point into the node itself, as
 * map_iter_value_ptr() hands out exactly that address.
 */
static void node_store_value(map_t map, map_node_t *node, const void *value)
{
    void *data = node_data(map, node);
    if (value)
        memmove(data, value, map->data_size);
    else
        memset(data, 0, map->data_size);
}

static map_node_t *map_create_node(map_t map,
                                   const void *key,
                                   const void *value)
{
    map_node_t *node = alloc_node(map);
    if (unlikely(!node))
        return NULL;

    node->left = NULL;
    node->right_red = (map_node_t *) RB_RED;
    node->parent = NULL;
    memcpy(node_key(map, node), key, map->key_size);
    node_store_value(map, node, value);
    return node;
}

/* Restore the red-black invariants after linking a red node into the tree.
 *
 * Both this and remove_fixup() are the textbook bottom-up algorithms driven by
 * the parent links. Keeping insertion and removal in the same family matters: a
 * left-leaning insert paired with this general removal silently accepts the
 * right-leaning red links removal produces and then corrupts the tree.
 */
static void insert_fixup(map_t map, map_node_t *node)
{
    map_node_t *parent;
    while ((parent = node->parent) && node_color(parent) == RB_RED) {
        /* A red parent is never the root, so the grandparent exists. */
        map_node_t *grand = parent->parent;
        if (parent == grand->left) {
            map_node_t *uncle = node_right(grand);
            if (node_color(uncle) == RB_RED) {
                node_set_color(parent, RB_BLACK);
                node_set_color(uncle, RB_BLACK);
                node_set_color(grand, RB_RED);
                node = grand;
                continue;
            }
            if (node == node_right(parent)) {
                node = parent;
                rotate_left(map, node);
                parent = node->parent;
            }
            node_set_color(parent, RB_BLACK);
            node_set_color(grand, RB_RED);
            rotate_right(map, grand);
        } else {
            map_node_t *uncle = grand->left;
            if (node_color(uncle) == RB_RED) {
                node_set_color(parent, RB_BLACK);
                node_set_color(uncle, RB_BLACK);
                node_set_color(grand, RB_RED);
                node = grand;
                continue;
            }
            if (node == parent->left) {
                node = parent;
                rotate_right(map, node);
                parent = node->parent;
            }
            node_set_color(parent, RB_BLACK);
            node_set_color(grand, RB_RED);
            rotate_left(map, grand);
        }
    }
    node_set_color(map->root, RB_BLACK);
}

static bool map_put(map_t map, const void *key, const void *value, bool replace)
{
    if (!map || !key)
        return false;

    map_cmp_t (*const comparator)(const void *, const void *) = map->comparator;
    map_node_t *walk = map->root, *parent = NULL;
    const size_t key_offset = map->key_offset;
    map_cmp_t cmp = MAP_CMP_EQUAL;

    /* Filling a map in ascending or descending key order is common here:
     * sequential file descriptors, sorted address ranges. The largest node has
     * no right child and the smallest has no left child, so a key outside the
     * current range can be linked straight onto the cached extreme instead of
     * walking down from the root.
     */
    if (map->last &&
        map_compare(comparator, key, (char *) map->last + key_offset) ==
            MAP_CMP_GREATER) {
        parent = map->last;
        cmp = MAP_CMP_GREATER;
        walk = NULL;
    } else if (map->first &&
               map_compare(comparator, key, (char *) map->first + key_offset) ==
                   MAP_CMP_LESS) {
        parent = map->first;
        cmp = MAP_CMP_LESS;
        walk = NULL;
    }

    while (walk) {
        cmp = map_compare(comparator, key, (char *) walk + key_offset);
        if (cmp == MAP_CMP_EQUAL) {
            if (replace)
                node_store_value(map, walk, value);
            return replace;
        }
        parent = walk;
        walk = cmp == MAP_CMP_LESS ? walk->left : node_right(walk);
    }

    map_node_t *node = map_create_node(map, key, value);
    if (unlikely(!node))
        return false;

    /* The new node is the smallest exactly when it lands as the left child of
     * the current smallest, and the mirror holds for the largest, so the cached
     * extremes fall out of the link step instead of being tracked down the
     * descent.
     */
    node->parent = parent;
    if (!parent) {
        map->root = map->first = map->last = node;
    } else if (cmp == MAP_CMP_LESS) {
        parent->left = node;
        if (parent == map->first)
            map->first = node;
    } else {
        node_set_right(parent, node);
        if (parent == map->last)
            map->last = node;
    }

    insert_fixup(map, node);
    map->size++;
    return true;
}

/* Repair the deficient black height rooted at node, whose parent is given
 * explicitly because node may be NULL.
 *
 * The sibling is never NULL here: node is short one black node, so the sibling
 * subtree has a black height of at least one and therefore a real root.
 */
static void remove_fixup(map_t map, map_node_t *node, map_node_t *parent)
{
    while (node != map->root && node_color(node) == RB_BLACK) {
        if (node == parent->left) {
            map_node_t *sibling = node_right(parent);
            if (node_color(sibling) == RB_RED) {
                node_set_color(sibling, RB_BLACK);
                node_set_color(parent, RB_RED);
                rotate_left(map, parent);
                sibling = node_right(parent);
            }

            if (node_color(sibling->left) == RB_BLACK &&
                node_color(node_right(sibling)) == RB_BLACK) {
                node_set_color(sibling, RB_RED);
                node = parent;
                parent = node->parent;
            } else {
                if (node_color(node_right(sibling)) == RB_BLACK) {
                    node_set_color(sibling->left, RB_BLACK);
                    node_set_color(sibling, RB_RED);
                    rotate_right(map, sibling);
                    sibling = node_right(parent);
                }
                node_set_color(sibling, node_color(parent));
                node_set_color(parent, RB_BLACK);
                node_set_color(node_right(sibling), RB_BLACK);
                rotate_left(map, parent);
                node = map->root;
                parent = NULL;
            }
        } else {
            map_node_t *sibling = parent->left;
            if (node_color(sibling) == RB_RED) {
                node_set_color(sibling, RB_BLACK);
                node_set_color(parent, RB_RED);
                rotate_right(map, parent);
                sibling = parent->left;
            }

            if (node_color(node_right(sibling)) == RB_BLACK &&
                node_color(sibling->left) == RB_BLACK) {
                node_set_color(sibling, RB_RED);
                node = parent;
                parent = node->parent;
            } else {
                if (node_color(sibling->left) == RB_BLACK) {
                    node_set_color(node_right(sibling), RB_BLACK);
                    node_set_color(sibling, RB_RED);
                    rotate_left(map, sibling);
                    sibling = parent->left;
                }
                node_set_color(sibling, node_color(parent));
                node_set_color(parent, RB_BLACK);
                node_set_color(sibling->left, RB_BLACK);
                rotate_right(map, parent);
                node = map->root;
                parent = NULL;
            }
        }
    }
    node_set_color(node, RB_BLACK);
}

static void rb_remove(map_t map, map_node_t *node)
{
    map_node_t *moved = node;
    map_node_t *child;
    map_node_t *child_parent;
    map_color_t removed_color = node_color(moved);

    if (!node->left || !node_right(node)) {
        child = node->left ? node->left : node_right(node);
        child_parent = node->parent;
        transplant(map, node, child);
    } else {
        moved = node_min(node_right(node));
        removed_color = node_color(moved);
        child = node_right(moved);
        if (moved->parent == node) {
            child_parent = moved;
            if (child)
                child->parent = moved;
        } else {
            child_parent = moved->parent;
            transplant(map, moved, child);
            node_set_right(moved, node_right(node));
            node_right(moved)->parent = moved;
        }

        transplant(map, node, moved);
        moved->left = node->left;
        moved->left->parent = moved;
        node_set_color(moved, node_color(node));
    }

    if (removed_color == RB_BLACK)
        remove_fixup(map, child, child_parent);
}

map_t map_new_aligned(size_t key_size,
                      size_t key_align,
                      size_t data_size,
                      size_t data_align,
                      map_cmp_t (*cmp)(const void *, const void *))
{
    if (!key_size || !data_size || !cmp || !valid_alignment(key_align) ||
        !valid_alignment(data_align))
        return NULL;

    size_t data_offset;
    if (!align_offset(sizeof(map_node_t), data_align, &data_offset) ||
        data_size > SIZE_MAX - data_offset)
        return NULL;
    size_t data_end = data_offset + data_size;
    size_t key_offset;
    if (!align_offset(data_end, key_align, &key_offset) ||
        key_size > SIZE_MAX - key_offset)
        return NULL;
    size_t node_size = key_offset + key_size;
    size_t node_stride;
    size_t node_align = key_align > data_align ? key_align : data_align;
    if (node_align < sizeof(void *))
        node_align = sizeof(void *);
    if (!align_offset(node_size, node_align, &node_stride))
        return NULL;

    /* Nodes are handed out at a fixed stride inside each chunk, so a
     * power-of-two stride makes every node land on the same few cache sets. One
     * extra alignment unit staggers them. Measured on 100k random-key lookups
     * this is worth 2-4%, and it costs 8 bytes per node: for the common
     * int-keyed, int-valued map, 40 bytes instead of 32. Drop the bump if
     * footprint matters more than random-access speed.
     */
    if (!(node_stride & (node_stride - 1))) {
        if (node_stride > SIZE_MAX - node_align)
            return NULL;
        node_stride += node_align;
    }

    map_t map = malloc(sizeof(*map));
    if (!map)
        return NULL;

    *map = (struct map_internal) {
        .key_size = key_size,
        .data_size = data_size,
        .key_offset = key_offset,
        .data_offset = data_offset,
        .node_align = node_align,
        .node_stride = node_stride,
        .comparator = cmp,
    };
    return map;
}

map_t map_new(size_t key_size,
              size_t data_size,
              map_cmp_t (*cmp)(const void *, const void *))
{
    return map_new_aligned(key_size, _Alignof(void *), data_size,
                           _Alignof(void *), cmp);
}

bool map_insert(map_t obj, const void *key, const void *val)
{
    return map_put(obj, key, val, false);
}

bool map_set(map_t obj, const void *key, const void *val)
{
    return map_put(obj, key, val, true);
}

/* Park an iterator at the end of obj, which may itself be NULL. */
static inline void iter_end(map_t obj, map_iter_t *it)
{
    it->map = obj;
    it->node = NULL;
}

void map_find(map_t obj, map_iter_t *it, const void *key)
{
    if (unlikely(!obj || !it || !key)) {
        if (it)
            iter_end(obj, it);
        return;
    }

    map_cmp_t (*const comparator)(const void *, const void *) = obj->comparator;
    const size_t key_offset = obj->key_offset;
    map_node_t *node = obj->root;
    while (node) {
        map_cmp_t cmp =
            map_compare(comparator, key, (char *) node + key_offset);
        if (cmp == MAP_CMP_EQUAL)
            break;
        node = cmp == MAP_CMP_LESS ? node->left : node_right(node);
    }
    it->map = obj;
    it->node = node;
}

/* Shared descent for the two bound queries: record every node that qualifies as
 * a bound and keep narrowing. The two differ only in which comparison
 * disqualifies a node, so pass that as skip.
 */
static void map_bound(map_t obj,
                      map_iter_t *it,
                      const void *key,
                      map_cmp_t skip)
{
    if (unlikely(!obj || !it || !key)) {
        if (it)
            iter_end(obj, it);
        return;
    }

    map_cmp_t (*const comparator)(const void *, const void *) = obj->comparator;
    const size_t key_offset = obj->key_offset;
    map_node_t *node = obj->root, *bound = NULL;

    while (node) {
        map_cmp_t cmp =
            map_compare(comparator, key, (char *) node + key_offset);
        if (cmp != skip)
            bound = node;
        if (cmp == MAP_CMP_EQUAL)
            break;
        node = cmp == MAP_CMP_LESS ? node->left : node_right(node);
    }
    it->map = obj;
    it->node = bound;
}

void map_ceil(map_t obj, map_iter_t *it, const void *key)
{
    map_bound(obj, it, key, MAP_CMP_GREATER);
}

void map_floor(map_t obj, map_iter_t *it, const void *key)
{
    map_bound(obj, it, key, MAP_CMP_LESS);
}

bool map_empty(map_t obj)
{
    return !obj || !obj->root;
}

bool map_at_end(const map_iter_t *it)
{
    return !it || !it->node;
}

const void *map_iter_key_ptr(const map_iter_t *it)
{
    if (unlikely(!it || !it->map || !it->node))
        return NULL;
    return node_key(it->map, it->node);
}

void *map_iter_value_ptr(const map_iter_t *it)
{
    if (unlikely(!it || !it->map || !it->node))
        return NULL;
    return node_data(it->map, it->node);
}

void map_erase(map_iter_t *it)
{
    if (!it || !it->node || !it->map || !it->map->size)
        return;

    map_t obj = it->map;
    map_node_t *node = it->node;
    if (node == obj->first)
        obj->first = node_next(node);
    if (node == obj->last)
        obj->last = node_prev(node);

    rb_remove(obj, node);
    node->left = obj->free_nodes;
    obj->free_nodes = node;
    it->node = NULL;
    obj->size--;
}

void map_clear(map_t obj)
{
    if (!obj)
        return;
    map_chunk_t *chunk = obj->chunks;
    while (chunk) {
        map_chunk_t *next = chunk->next;
        free(chunk);
        chunk = next;
    }
    obj->root = obj->first = obj->last = NULL;
    obj->free_nodes = NULL;
    obj->chunks = NULL;
    obj->bump = obj->bump_end = NULL;
    obj->size = 0;
}

void map_delete(map_t obj)
{
    if (!obj)
        return;
    map_clear(obj);
    free(obj);
}

size_t map_size(map_t obj)
{
    return obj ? obj->size : 0;
}

void map_first(map_t map, map_iter_t *it)
{
    if (it) {
        it->map = map;
        it->node = map ? map->first : NULL;
    }
}

void map_last(map_t map, map_iter_t *it)
{
    if (it) {
        it->map = map;
        it->node = map ? map->last : NULL;
    }
}

void map_next(map_iter_t *it)
{
    if (it && it->node)
        it->node = node_next(it->node);
}

void map_prev(map_iter_t *it)
{
    if (it && it->node)
        it->node = node_prev(it->node);
}
