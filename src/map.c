/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* This map implementation has undergone extensive modifications, heavily
 * relying on the rb.h header file from jemalloc.
 * The original rb.h file served as the foundation and source of inspiration
 * for adapting and tailoring it specifically for this map implementation.
 * Therefore, credit and sincere thanks are extended to jemalloc for their
 * invaluable work.
 * Reference:
 *   https://github.com/jemalloc/jemalloc/blob/dev/include/ \
 *   jemalloc/internal/rb.h
 */

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "map.h"

struct map_internal {
    map_node_t *root;           /* Tree root */
    size_t key_size, data_size; /* Size of key/value type */
    size_t size;                /* Number of nodes */
    /* Key comparison function */
    map_cmp_t (*comparator)(const void *, const void *);
};

/* Each red–black tree node requires at least one byte of storage (for its
 * linkage). A one-byte object could support up to 2^{sizeof(void *) * 8} nodes
 * in an address space. However, the red–black tree algorithm guarantees that
 * the tree depth is bounded by 2 * log₂(n), where n is the number of nodes.
 *
 * For operations such as insertion and deletion, a fixed-size array is used to
 * track the path through the tree. RB_MAX_DEPTH is conservatively defined as 16
 * times the size of a pointer to ensure that the array is large enough for any
 * realistic tree, regardless of the theoretical maximum node count.
 */
#define RB_MAX_DEPTH (sizeof(void *) << 4)

/* Color/pointer manipulation macros */
#define RB_COLOR_MASK 1UL
#define RB_PTR_MASK (~RB_COLOR_MASK)

typedef enum { RB_BLACK = 0, RB_RED = 1 } map_color_t;

/* Helper macros for cleaner access patterns */
#define RB_IS_RED(node) (rb_node_get_color(node) == RB_RED)
#define RB_IS_BLACK(node) (rb_node_get_color(node) == RB_BLACK)

/* Prefetch hints for better cache utilization */
#ifdef __builtin_prefetch
#define PREFETCH_READ(addr) __builtin_prefetch((addr), 0, 1)
#define PREFETCH_WRITE(addr) __builtin_prefetch((addr), 1, 1)
#else
#define PREFETCH_READ(addr) ((void) 0)
#define PREFETCH_WRITE(addr) ((void) 0)
#endif

/* Left accessors */
static inline map_node_t *rb_node_get_left(const map_node_t *node)
{
    return node->left;
}

static inline void rb_node_set_left(map_node_t *node, map_node_t *left)
{
    node->left = left;
}

/* Right accessors - using consistent masking */
static inline map_node_t *rb_node_get_right(const map_node_t *node)
{
    return (map_node_t *) (((uintptr_t) node->right_red) & ~RB_COLOR_MASK);
}

static inline void rb_node_set_right(map_node_t *node, map_node_t *right)
{
    node->right_red =
        (map_node_t *) (((uintptr_t) right) |
                        (((uintptr_t) node->right_red) & RB_COLOR_MASK));
}

/* Color accessors */
static inline map_color_t rb_node_get_color(const map_node_t *node)
{
    return ((uintptr_t) node->right_red) & RB_COLOR_MASK;
}

static inline void rb_node_set_color(map_node_t *node, map_color_t color)
{
    node->right_red =
        (map_node_t *) (((uintptr_t) node->right_red & ~RB_COLOR_MASK) | color);
}

static inline void rb_node_set_red(map_node_t *node)
{
    node->right_red = (map_node_t *) (((uintptr_t) node->right_red) | RB_RED);
}

static inline void rb_node_set_black(map_node_t *node)
{
    node->right_red =
        (map_node_t *) (((uintptr_t) node->right_red) & ~RB_COLOR_MASK);
}

/* Node initializer */
static inline void rb_node_init(map_node_t *node)
{
    assert((((uintptr_t) node) & RB_COLOR_MASK) == 0); /* properly aligned */
    node->left = NULL;
    node->right_red = (map_node_t *) RB_RED; /* NULL with red color */
}

/* Internal helper macros */
#define rb_node_rotate_left(x_node, r_node)                      \
    do {                                                         \
        (r_node) = rb_node_get_right((x_node));                  \
        rb_node_set_right((x_node), rb_node_get_left((r_node))); \
        rb_node_set_left((r_node), (x_node));                    \
    } while (0)

#define rb_node_rotate_right(x_node, r_node)                     \
    do {                                                         \
        (r_node) = rb_node_get_left((x_node));                   \
        rb_node_set_left((x_node), rb_node_get_right((r_node))); \
        rb_node_set_right((r_node), (x_node));                   \
    } while (0)

typedef struct {
    map_node_t *node;
    map_cmp_t cmp;
} rb_path_entry_t;

static void rb_remove(map_t rb, map_node_t *node)
{
    rb_path_entry_t path[RB_MAX_DEPTH];
    rb_path_entry_t *pathp = NULL, *nodep = NULL;

    /* Traverse through red-black tree node and find the search target node. */
    path->node = rb->root;
    pathp = path;
    while (pathp->node) {
        map_cmp_t cmp = pathp->cmp =
            (rb->comparator)(node->key, pathp->node->key);
        if (cmp == MAP_CMP_LESS) {
            pathp[1].node = rb_node_get_left(pathp->node);
        } else {
            pathp[1].node = rb_node_get_right(pathp->node);
            if (cmp == MAP_CMP_EQUAL) {
                /* find node's successor, in preparation for swap */
                pathp->cmp = MAP_CMP_GREATER;
                nodep = pathp;
                for (pathp++; pathp->node; pathp++) {
                    pathp->cmp = MAP_CMP_LESS;
                    pathp[1].node = rb_node_get_left(pathp->node);
                }
                break;
            }
        }
        pathp++;
    }
    assert(nodep && nodep->node == node);

    pathp--;
    if (pathp->node != node) {
        /* swap node with its successor */
        map_color_t tcolor = rb_node_get_color(pathp->node);
        rb_node_set_color(pathp->node, rb_node_get_color(node));
        rb_node_set_left(pathp->node, rb_node_get_left(node));

        /* If the node's successor is its right child, the following code may
         * behave incorrectly for the right child pointer.
         * However, it is not a problem as the pointer will be correctly set
         * when the successor is pruned.
         */
        rb_node_set_right(pathp->node, rb_node_get_right(node));
        rb_node_set_color(node, tcolor);

        /* The child pointers of the pruned leaf node are never accessed again,
         * so there is no need to set them to NULL.
         */
        nodep->node = pathp->node;
        pathp->node = node;
        if (nodep == path) {
            rb->root = nodep->node;
        } else {
            if (nodep[-1].cmp == MAP_CMP_LESS)
                rb_node_set_left(nodep[-1].node, nodep->node);
            else
                rb_node_set_right(nodep[-1].node, nodep->node);
        }
    } else {
        map_node_t *left = rb_node_get_left(node);
        if (left) {
            /* node has no successor, but it has a left child.
             * Splice node out, without losing the left child.
             */
            assert(RB_IS_BLACK(node));
            assert(RB_IS_RED(left));
            rb_node_set_black(left);
            if (pathp == path) {
                /* the subtree rooted at the node's left child has not
                 * changed, and it is now the root.
                 */
                rb->root = left;
            } else {
                if (pathp[-1].cmp == MAP_CMP_LESS)
                    rb_node_set_left(pathp[-1].node, left);
                else
                    rb_node_set_right(pathp[-1].node, left);
            }
            return;
        }
        if (pathp == path) {
            /* the tree only contained one node */
            rb->root = NULL;
            return;
        }
    }

    /* The invariant has been established that the node has no right child
     * (morally speaking; the right child was not explicitly nulled out if
     * swapped with its successor). Furthermore, the only nodes with
     * out-of-date summaries exist in path[0], path[1], ..., pathp[-1].
     */
    if (RB_IS_RED(pathp->node)) {
        /* prune red node, which requires no fixup */
        assert(pathp[-1].cmp == MAP_CMP_LESS);
        rb_node_set_left(pathp[-1].node, NULL);
        return;
    }

    /* The node to be pruned is black, so unwind until balance is restored. */
    pathp->node = NULL;
    for (pathp--; (uintptr_t) pathp >= (uintptr_t) path; pathp--) {
        assert(pathp->cmp != MAP_CMP_EQUAL);
        if (pathp->cmp == MAP_CMP_LESS) {
            rb_node_set_left(pathp->node, pathp[1].node);
            if (RB_IS_RED(pathp->node)) {
                map_node_t *right = rb_node_get_right(pathp->node);
                map_node_t *rightleft = rb_node_get_left(right);
                map_node_t *tnode;
                if (rightleft && RB_IS_RED(rightleft)) {
                    /* In the following diagrams, ||, //, and \\
                     * indicate the path to the removed node.
                     *
                     *      ||
                     *    pathp(r)
                     *  //        \
                     * (b)        (b)
                     *           /
                     *          (r)
                     */
                    rb_node_set_black(pathp->node);
                    rb_node_rotate_right(right, tnode);
                    rb_node_set_right(pathp->node, tnode);
                    rb_node_rotate_left(pathp->node, tnode);
                } else {
                    /*      ||
                     *    pathp(r)
                     *  //        \
                     * (b)        (b)
                     *           /
                     *          (b)
                     */
                    rb_node_rotate_left(pathp->node, tnode);
                }

                /* Balance restored, but rotation modified subtree root. */
                assert((uintptr_t) pathp > (uintptr_t) path);
                if (pathp[-1].cmp == MAP_CMP_LESS)
                    rb_node_set_left(pathp[-1].node, tnode);
                else
                    rb_node_set_right(pathp[-1].node, tnode);
                return;
            } else {
                map_node_t *right = rb_node_get_right(pathp->node);
                map_node_t *rightleft = rb_node_get_left(right);
                if (rightleft && RB_IS_RED(rightleft)) {
                    /*      ||
                     *    pathp(b)
                     *  //        \
                     * (b)        (b)
                     *           /
                     *          (r)
                     */
                    map_node_t *tnode;
                    rb_node_set_black(rightleft);
                    rb_node_rotate_right(right, tnode);
                    rb_node_set_right(pathp->node, tnode);
                    rb_node_rotate_left(pathp->node, tnode);
                    /* Balance restored, but rotation modified subtree root,
                     * which may actually be the tree root.
                     */
                    if (pathp == path) {
                        /* set root */
                        rb->root = tnode;
                    } else {
                        if (pathp[-1].cmp == MAP_CMP_LESS)
                            rb_node_set_left(pathp[-1].node, tnode);
                        else
                            rb_node_set_right(pathp[-1].node, tnode);
                    }
                    return;
                } else {
                    /*      ||
                     *    pathp(b)
                     *  //        \
                     * (b)        (b)
                     *           /
                     *          (b)
                     */
                    map_node_t *tnode;
                    rb_node_set_red(pathp->node);
                    rb_node_rotate_left(pathp->node, tnode);
                    pathp->node = tnode;
                }
            }
        } else {
            rb_node_set_right(pathp->node, pathp[1].node);
            map_node_t *left = rb_node_get_left(pathp->node);
            if (RB_IS_RED(left)) {
                map_node_t *tnode;
                map_node_t *leftright = rb_node_get_right(left);
                map_node_t *leftrightleft = rb_node_get_left(leftright);
                if (leftrightleft && RB_IS_RED(leftrightleft)) {
                    /*      ||
                     *    pathp(b)
                     *   /        \\
                     * (r)        (b)
                     *   \
                     *   (b)
                     *   /
                     * (r)
                     */
                    map_node_t *unode;
                    rb_node_set_black(leftrightleft);
                    rb_node_rotate_right(pathp->node, unode);
                    rb_node_rotate_right(pathp->node, tnode);
                    rb_node_set_right(unode, tnode);
                    rb_node_rotate_left(unode, tnode);
                } else {
                    /*      ||
                     *    pathp(b)
                     *   /        \\
                     * (r)        (b)
                     *   \
                     *   (b)
                     *   /
                     * (b)
                     */
                    assert(leftright);
                    rb_node_set_red(leftright);
                    rb_node_rotate_right(pathp->node, tnode);
                    rb_node_set_black(tnode);
                }

                /* Balance restored, but rotation modified subtree root, which
                 * may actually be the tree root.
                 */
                if (pathp == path) {
                    /* set root */
                    rb->root = tnode;
                } else {
                    if (pathp[-1].cmp == MAP_CMP_LESS)
                        rb_node_set_left(pathp[-1].node, tnode);
                    else
                        rb_node_set_right(pathp[-1].node, tnode);
                }
                return;
            } else if (RB_IS_RED(pathp->node)) {
                map_node_t *leftleft = rb_node_get_left(left);
                if (leftleft && RB_IS_RED(leftleft)) {
                    /*        ||
                     *      pathp(r)
                     *     /        \\
                     *   (b)        (b)
                     *   /
                     * (r)
                     */
                    map_node_t *tnode;
                    rb_node_set_black(pathp->node);
                    rb_node_set_red(left);
                    rb_node_set_black(leftleft);
                    rb_node_rotate_right(pathp->node, tnode);
                    /* Balance restored, but rotation modified subtree root. */
                    assert((uintptr_t) pathp > (uintptr_t) path);
                    if (pathp[-1].cmp == MAP_CMP_LESS)
                        rb_node_set_left(pathp[-1].node, tnode);
                    else
                        rb_node_set_right(pathp[-1].node, tnode);
                    return;
                } else {
                    /*        ||
                     *      pathp(r)
                     *     /        \\
                     *   (b)        (b)
                     *   /
                     * (b)
                     */
                    rb_node_set_red(left);
                    rb_node_set_black(pathp->node);
                    /* balance restored */
                    return;
                }
            } else {
                map_node_t *leftleft = rb_node_get_left(left);
                if (leftleft && RB_IS_RED(leftleft)) {
                    /*               ||
                     *             pathp(b)
                     *            /        \\
                     *          (b)        (b)
                     *          /
                     *        (r)
                     */
                    map_node_t *tnode;
                    rb_node_set_black(leftleft);
                    rb_node_rotate_right(pathp->node, tnode);
                    /* Balance restored, but rotation modified subtree root,
                     * which may actually be the tree root.
                     */
                    if (pathp == path) {
                        /* set root */
                        rb->root = tnode;
                    } else {
                        if (pathp[-1].cmp == MAP_CMP_LESS)
                            rb_node_set_left(pathp[-1].node, tnode);
                        else
                            rb_node_set_right(pathp[-1].node, tnode);
                    }
                    return;
                } else {
                    /*               ||
                     *             pathp(b)
                     *            /        \\
                     *          (b)        (b)
                     *          /
                     *        (b)
                     */
                    rb_node_set_red(left);
                }
            }
        }
    }

    /* set root */
    rb->root = path->node;
    assert(RB_IS_BLACK(rb->root));
}

static void rb_destroy_recurse(map_t rb, map_node_t *node)
{
    if (!node)
        return;

    rb_destroy_recurse(rb, rb_node_get_left(node));
    rb_node_set_left((node), NULL);
    rb_destroy_recurse(rb, rb_node_get_right(node));
    rb_node_set_right((node), NULL);
    /* Single free for entire block (node + key + data) */
    free(node);
}

/* Create node with single allocation */
static map_node_t *map_create_node(const void *key,
                                   const void *value,
                                   size_t ksize,
                                   size_t vsize)
{
    /* Calculate aligned offsets more efficiently */
    const size_t align_mask = sizeof(void *) - 1;
    size_t key_offset = (sizeof(map_node_t) + align_mask) & ~align_mask;
    size_t data_offset = (key_offset + ksize + align_mask) & ~align_mask;
    size_t total_size = data_offset + vsize;

    /* Check for overflow */
    if (unlikely(total_size < vsize || total_size < ksize))
        return NULL;

    char *mem = malloc(total_size);
    if (unlikely(!mem))
        return NULL;

    map_node_t *node = (map_node_t *) mem;
    node->key = mem + key_offset;
    node->data = mem + data_offset;

    /* Initialize node linkage */
    rb_node_init(node);

    /* Copy key and value data efficiently */
    if (key)
        memcpy(node->key, key, ksize);
    else
        memset(node->key, 0, ksize);

    if (value)
        memcpy(node->data, value, vsize);
    else
        memset(node->data, 0, vsize);

    return node;
}

/* Constructor - creates a new map instance */
map_t map_new(size_t key_size,
              size_t data_size,
              map_cmp_t (*cmp)(const void *, const void *))
{
    /* Validate sizes to prevent integer overflow in allocation */
    if (key_size == 0 || data_size == 0 || !cmp)
        return NULL;

    /* Prevent overflow: ensure total allocation size is reasonable */
    size_t max_size = SIZE_MAX / 4; /* Conservative limit */
    if (key_size > max_size || data_size > max_size ||
        (key_size + data_size) > max_size - sizeof(map_node_t))
        return NULL;

    map_t tree = malloc(sizeof(struct map_internal));
    if (!tree)
        return NULL;

    tree->key_size = key_size;
    tree->data_size = data_size;
    tree->comparator = cmp;
    tree->root = NULL;
    tree->size = 0;
    return tree;
}

/* Insert with single traversal - hot path */
static inline const map_node_t *rb_insert_unique(map_t rb,
                                                 const void *key,
                                                 rb_path_entry_t *path,
                                                 rb_path_entry_t **pathp_out)
{
    rb_path_entry_t *pathp;

    /* Single traversal to find insertion point or existing key */
    path->node = rb->root;
    size_t depth = 0;
    for (pathp = path; pathp->node && depth < RB_MAX_DEPTH - 1;
         pathp++, depth++) {
        map_cmp_t cmp = pathp->cmp = (rb->comparator)(key, pathp->node->key);
        if (cmp == MAP_CMP_LESS) {
            pathp[1].node = rb_node_get_left(pathp->node);
        } else if (cmp == MAP_CMP_GREATER) {
            pathp[1].node = rb_node_get_right(pathp->node);
        } else {
            /* Key already exists */
            return pathp->node;
        }
    }

    /* Key doesn't exist, return NULL and set pathp for insertion */
    if (depth >= RB_MAX_DEPTH - 1)
        return (const map_node_t *) -1; /* Tree too deep */
    *pathp_out = pathp;
    return NULL;
}

/* Insert a key-value pair into the map */
bool map_insert(map_t obj, const void *key, const void *val)
{
    if (!obj || !key)
        return false;

    rb_path_entry_t path[RB_MAX_DEPTH];
    rb_path_entry_t *pathp;

    /* Single traversal to check existence and get insertion point */
    const map_node_t *existing = rb_insert_unique(obj, key, path, &pathp);
    if (existing == (const map_node_t *) -1)
        return false; /* Tree too deep */
    if (existing)
        return false; /* Key already exists */

    /* Create and insert new node */
    map_node_t *node = map_create_node(key, val, obj->key_size, obj->data_size);
    if (!node)
        return false;

    /* Node already initialized in map_create_node, just set in path */
    pathp->node = node;

    /* Fix up red-black tree properties */
    for (pathp--; (uintptr_t) pathp >= (uintptr_t) path; pathp--) {
        map_node_t *cnode = pathp->node;
        if (pathp->cmp == MAP_CMP_LESS) {
            map_node_t *left = pathp[1].node;
            rb_node_set_left(cnode, left);
            if (RB_IS_BLACK(left))
                break;
            map_node_t *leftleft = rb_node_get_left(left);
            if (leftleft && RB_IS_RED(leftleft)) {
                /* fix up 4-node */
                map_node_t *tnode;
                rb_node_set_black(leftleft);
                rb_node_rotate_right(cnode, tnode);
                cnode = tnode;
            }
        } else {
            map_node_t *right = pathp[1].node;
            rb_node_set_right(cnode, right);
            if (RB_IS_BLACK(right))
                break;
            map_node_t *left = rb_node_get_left(cnode);
            if (left && RB_IS_RED(left)) {
                /* split 4-node */
                rb_node_set_black(left);
                rb_node_set_black(right);
                rb_node_set_red(cnode);
            } else {
                /* lean left */
                map_node_t *tnode;
                map_color_t tcolor = rb_node_get_color(cnode);
                rb_node_rotate_left(cnode, tnode);
                rb_node_set_color(tnode, tcolor);
                rb_node_set_red(cnode);
                cnode = tnode;
            }
        }
        pathp->node = cnode;
    }

    /* Set root and make it black */
    obj->root = path->node;
    rb_node_set_black(obj->root);
    obj->size++;
    return true;
}

/* Get functions, avoiding stack allocation */
void map_find(map_t obj, map_iter_t *it, const void *key)
{
    if (unlikely(!obj || !it)) {
        if (it)
            it->node = NULL;
        return;
    }

    map_node_t *node = obj->root;

    /* Prefetch for large trees */
    if (node && obj->size > 10000) {
        PREFETCH_READ(node->left);
        PREFETCH_READ(rb_node_get_right(node));
    }

    while (node) {
        map_cmp_t cmp = obj->comparator(key, node->key);
        if (cmp == MAP_CMP_EQUAL) {
            it->node = node;
            return;
        }
        node = (cmp == MAP_CMP_LESS) ? node->left : rb_node_get_right(node);
    }
    it->node = NULL;
}

bool map_empty(map_t obj)
{
    return unlikely(!obj) || !obj->root;
}

/* Iteration */
bool map_at_end(map_t m, const map_iter_t *it)
{
    (void) m; /* Suppress unused parameter warning */
    return !(it->node);
}

/* Remove functions */
void map_erase(map_t obj, map_iter_t *it)
{
    if (!obj || !it || !it->node)
        return;

    /* Verify node exists in tree before removal */
    if (obj->size == 0)
        return;

    rb_remove(obj, it->node);
    /* Single free for entire block (node + key + data) */
    free(it->node);
    it->node = NULL;

    /* Prevent underflow */
    if (obj->size > 0)
        obj->size--;
}

/* Empty map */
void map_clear(map_t obj)
{
    if (!obj)
        return;
    rb_destroy_recurse(obj, obj->root);
    obj->root = NULL;
    obj->size = 0;
}

/* Destroy map and free all resources */
void map_delete(map_t obj)
{
    if (!obj)
        return;
    map_clear(obj);
    free(obj);
}

/* Get number of elements in map */
size_t map_size(map_t obj)
{
    return likely(obj) ? obj->size : 0;
}

/* Iterator traversal functions */

void map_first(map_t map, map_iter_t *it)
{
    if (unlikely(!map || !it)) {
        if (it)
            it->node = NULL;
        return;
    }

    map_node_t *node = map->root;
    if (likely(node)) {
        while (node->left)
            node = node->left;
    }

    it->node = node;
    it->prev = NULL;
    it->count = 0;
}

void map_last(map_t map, map_iter_t *it)
{
    if (unlikely(!map || !it)) {
        if (it)
            it->node = NULL;
        return;
    }

    map_node_t *node = map->root;
    if (likely(node)) {
        map_node_t *right;
        while ((right = rb_node_get_right(node)))
            node = right;
    }

    it->node = node;
    it->prev = NULL;
    it->count = 0;
}

void map_next(map_t map, map_iter_t *it)
{
    if (unlikely(!map || !it || !it->node)) {
        if (it)
            it->node = NULL;
        return;
    }

    map_node_t *node = it->node;
    map_node_t *right = rb_node_get_right(node);

    /* If right subtree exists, find leftmost node in right subtree */
    if (right) {
        while (right->left)
            right = right->left;
        it->node = right;
        return;
    }

    /* Find successor by searching from root */
    map_node_t *succ = NULL;
    map_node_t *curr = map->root;

    while (curr) {
        map_cmp_t cmp = map->comparator(it->node->key, curr->key);
        if (cmp == MAP_CMP_LESS) {
            succ = curr;
            curr = curr->left;
        } else if (cmp == MAP_CMP_GREATER) {
            curr = rb_node_get_right(curr);
        } else {
            break;
        }
    }

    it->node = succ;
}

void map_prev(map_t map, map_iter_t *it)
{
    if (!map || !it || !it->node) {
        if (it)
            it->node = NULL;
        return;
    }

    map_node_t *node = it->node;

    /* If left subtree exists, find rightmost node in left subtree */
    if (node->left) {
        node = node->left;
        while (rb_node_get_right(node))
            node = rb_node_get_right(node);
        it->node = node;
        return;
    }

    /* Otherwise, find the first ancestor that is a right child */
    /* We need to traverse up, but we don't have parent pointers */
    /* So we need to find the predecessor by searching from root */

    map_node_t *pred = NULL;
    map_node_t *curr = map->root;

    while (curr) {
        map_cmp_t cmp = map->comparator(it->node->key, curr->key);
        if (cmp == MAP_CMP_GREATER) {
            pred = curr;
            curr = rb_node_get_right(curr);
        } else if (cmp == MAP_CMP_LESS) {
            curr = curr->left;
        } else {
            /* Found the node, predecessor is already set or NULL */
            break;
        }
    }

    it->node = pred;
}
