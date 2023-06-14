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
 *   https://github.com/jemalloc/jemalloc/blob/dev/include/jemalloc/internal/rb.h
 */

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "map.h"

/* TODO: Avoid relying on key_size and data_size */
struct map_internal {
    map_node_t *root;

    /* properties */
    size_t key_size, data_size;

    map_cmp_t (*comparator)(const void *, const void *);
};

/* Each node in the red-black tree consumes at least 1 byte of space (for the
 * linkage if nothing else), so there are a maximum of sizeof(void *) << 3
 * red-black tree nodes in any process (and thus, at most sizeof(void *) << 3
 * nodes in any red-black tree). The choice of algorithm bounds the depth of
 * a tree to twice the binary logarithm (base 2) of the number of elements in
 * the tree; the following bound applies.
 */
#define RB_MAX_DEPTH (sizeof(void *) << 4)

typedef enum { RB_BLACK = 0, RB_RED } map_color_t;

/* Left accessors */
static inline map_node_t *rb_node_get_left(const map_node_t *node)
{
    return node->left;
}

static inline void rb_node_set_left(map_node_t *node, map_node_t *left)
{
    node->left = left;
}

/* Right accessors */
static inline map_node_t *rb_node_get_right(const map_node_t *node)
{
    return (map_node_t *) (((uintptr_t) node->right_red) & ~3);
}

static inline void rb_node_set_right(map_node_t *node, map_node_t *right)
{
    node->right_red = (map_node_t *) (((uintptr_t) right) |
                                      (((uintptr_t) node->right_red) & 1));
}

/* Color accessors */
static inline map_color_t rb_node_get_color(const map_node_t *node)
{
    return ((uintptr_t) node->right_red) & 1;
}

static inline void rb_node_set_color(map_node_t *node, map_color_t color)
{
    node->right_red =
        (map_node_t *) (((uintptr_t) node->right_red & ~3) | color);
}

static inline void rb_node_set_red(map_node_t *node)
{
    node->right_red = (map_node_t *) (((uintptr_t) node->right_red) | 1);
}

static inline void rb_node_set_black(map_node_t *node)
{
    node->right_red = (map_node_t *) (((uintptr_t) node->right_red) & ~3);
}

/* Node initializer */
static inline void rb_node_init(map_node_t *node)
{
    assert((((uintptr_t) node) & (0x1)) == 0); /* a pointer without marker */
    rb_node_set_left(node, NULL);
    rb_node_set_right(node, NULL);
    rb_node_set_red(node);
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

static inline map_node_t *rb_search(map_t rb, const map_node_t *node)
{
    map_node_t *ret = rb->root;
    while (ret) {
        map_cmp_t cmp = (rb->comparator)(node->key, ret->key);
        switch (cmp) {
        case _CMP_EQUAL:
            return ret;
        case _CMP_LESS:
            ret = rb_node_get_left(ret);
            break;
        case _CMP_GREATER:
            ret = rb_node_get_right(ret);
            break;
        default:
            __UNREACHABLE;
            break;
        }
    }
    return ret;
}

static void rb_insert(map_t rb, map_node_t *node)
{
    rb_path_entry_t path[RB_MAX_DEPTH];
    rb_path_entry_t *pathp;
    rb_node_init(node);

    /* Traverse through red-black tree node and find the search target node. */
    path->node = rb->root;
    for (pathp = path; pathp->node; pathp++) {
        map_cmp_t cmp = pathp->cmp =
            (rb->comparator)(node->key, pathp->node->key);
        switch (cmp) {
        case _CMP_LESS:
            pathp[1].node = rb_node_get_left(pathp->node);
            break;
        case _CMP_GREATER:
            pathp[1].node = rb_node_get_right(pathp->node);
            break;
        default:
            /* igore duplicate key */
            __UNREACHABLE;
            break;
        }
    }
    pathp->node = node;

    assert(!rb_node_get_left(node));
    assert(!rb_node_get_right(node));

    /* Go from target node back to root node and fix color accordingly */
    for (pathp--; (uintptr_t) pathp >= (uintptr_t) path; pathp--) {
        map_node_t *cnode = pathp->node;
        if (pathp->cmp == _CMP_LESS) {
            map_node_t *left = pathp[1].node;
            rb_node_set_left(cnode, left);
            if (rb_node_get_color(left) == RB_BLACK)
                return;
            map_node_t *leftleft = rb_node_get_left(left);
            if (leftleft && (rb_node_get_color(leftleft) == RB_RED)) {
                /* fix up 4-node */
                map_node_t *tnode;
                rb_node_set_black(leftleft);
                rb_node_rotate_right(cnode, tnode);
                cnode = tnode;
            }
        } else {
            map_node_t *right = pathp[1].node;
            rb_node_set_right(cnode, right);
            if (rb_node_get_color(right) == RB_BLACK)
                return;
            map_node_t *left = rb_node_get_left(cnode);
            if (left && (rb_node_get_color(left) == RB_RED)) {
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

    /* set root, and make it black */
    rb->root = path->node;
    rb_node_set_black(rb->root);
}

static void rb_remove(map_t rb, map_node_t *node)
{
    rb_path_entry_t path[RB_MAX_DEPTH];
    rb_path_entry_t *pathp = NULL, *nodep = NULL;

    /* Traverse through red-black tree node and find the search target node. */
    path->node = rb->root;
    pathp = path;
    while (pathp->node) {
        map_cmp_t cmp = pathp->cmp =
            (rb->comparator)(node->data, pathp->node->data);
        if (cmp == _CMP_LESS) {
            pathp[1].node = rb_node_get_left(pathp->node);
        } else {
            pathp[1].node = rb_node_get_right(pathp->node);
            if (cmp == _CMP_EQUAL) {
                /* find node's successor, in preparation for swap */
                pathp->cmp = _CMP_GREATER;
                nodep = pathp;
                for (pathp++; pathp->node; pathp++) {
                    pathp->cmp = _CMP_LESS;
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
            if (nodep[-1].cmp == _CMP_LESS)
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
            assert(rb_node_get_color(node) == RB_BLACK);
            assert(rb_node_get_color(left) == RB_RED);
            rb_node_set_black(left);
            if (pathp == path) {
                /* the subtree rooted at the node's left child has not
                 * changed, and it is now the root.
                 */
                rb->root = left;
            } else {
                if (pathp[-1].cmp == _CMP_LESS)
                    rb_node_set_left(pathp[-1].node, left);
                else
                    rb_node_set_right(pathp[-1].node, left);
            }
            return;
        } else if (pathp == path) {
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
    if (rb_node_get_color(pathp->node) == RB_RED) {
        /* prune red node, which requires no fixup */
        assert(pathp[-1].cmp == _CMP_LESS);
        rb_node_set_left(pathp[-1].node, NULL);
        return;
    }

    /* The node to be pruned is black, so unwind until balance is restored. */
    pathp->node = NULL;
    for (pathp--; (uintptr_t) pathp >= (uintptr_t) path; pathp--) {
        assert(pathp->cmp != _CMP_EQUAL);
        if (pathp->cmp == _CMP_LESS) {
            rb_node_set_left(pathp->node, pathp[1].node);
            if (rb_node_get_color(pathp->node) == RB_RED) {
                map_node_t *right = rb_node_get_right(pathp->node);
                map_node_t *rightleft = rb_node_get_left(right);
                map_node_t *tnode;
                if (rightleft && (rb_node_get_color(rightleft) == RB_RED)) {
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
                if (pathp[-1].cmp == _CMP_LESS)
                    rb_node_set_left(pathp[-1].node, tnode);
                else
                    rb_node_set_right(pathp[-1].node, tnode);
                return;
            } else {
                map_node_t *right = rb_node_get_right(pathp->node);
                map_node_t *rightleft = rb_node_get_left(right);
                if (rightleft && (rb_node_get_color(rightleft) == RB_RED)) {
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
                        if (pathp[-1].cmp == _CMP_LESS)
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
            if (rb_node_get_color(left) == RB_RED) {
                map_node_t *tnode;
                map_node_t *leftright = rb_node_get_right(left);
                map_node_t *leftrightleft = rb_node_get_left(leftright);
                if (leftrightleft &&
                    (rb_node_get_color(leftrightleft) == RB_RED)) {
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
                    if (pathp[-1].cmp == _CMP_LESS)
                        rb_node_set_left(pathp[-1].node, tnode);
                    else
                        rb_node_set_right(pathp[-1].node, tnode);
                }
                return;
            } else if (rb_node_get_color(pathp->node) == RB_RED) {
                map_node_t *leftleft = rb_node_get_left(left);
                if (leftleft && (rb_node_get_color(leftleft) == RB_RED)) {
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
                    if (pathp[-1].cmp == _CMP_LESS)
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
                if (leftleft && (rb_node_get_color(leftleft) == RB_RED)) {
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
                        if (pathp[-1].cmp == _CMP_LESS)
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
    assert(rb_node_get_color(rb->root) == RB_BLACK);
}

static void rb_destroy_recurse(map_t rb, map_node_t *node)
{
    if (!node)
        return;

    rb_destroy_recurse(rb, rb_node_get_left(node));
    rb_node_set_left((node), NULL);
    rb_destroy_recurse(rb, rb_node_get_right(node));
    rb_node_set_right((node), NULL);
    free(node->key);
    free(node->data);
    free(node);
}

static map_node_t *map_create_node(void *key,
                                   void *value,
                                   size_t ksize,
                                   size_t vsize)
{
    map_node_t *node = malloc(sizeof(map_node_t));
    assert(node);

    /* allocate memory for the keys and data */
    node->key = malloc(ksize), node->data = malloc(vsize);
    assert(node->key);
    assert(node->data);

    /* copy over the key and values.
     * If the parameter passed in is NULL, make the element blank instead of
     * a segfault.
     */
    if (!key)
        memset(node->key, 0, ksize);
    else
        memcpy(node->key, key, ksize);

    if (!value)
        memset(node->data, 0, vsize);
    else
        memcpy(node->data, value, vsize);

    return node;
}

/* Constructor */
map_t map_new(size_t s1,
              size_t s2,
              map_cmp_t (*cmp)(const void *, const void *))
{
    map_t tree = malloc(sizeof(struct map_internal));
    assert(tree);

    tree->key_size = s1, tree->data_size = s2;
    tree->comparator = cmp;
    tree->root = NULL;
    return tree;
}

/* Add function */
bool map_insert(map_t obj, void *key, void *val)
{
    map_node_t *node = map_create_node(key, val, obj->key_size, obj->data_size);
    rb_insert(obj, node);
    return true;
}

/* Get functions */
void map_find(map_t obj, map_iter_t *it, void *key)
{
    map_node_t tmp_node = {.key = key};
    it->node = rb_search(obj, &tmp_node);
}

bool map_empty(map_t obj)
{
    return !obj->root;
}

/* Iteration */
bool map_at_end(map_t m UNUSED, map_iter_t *it)
{
    return !(it->node);
}

/* Remove functions */
void map_erase(map_t obj, map_iter_t *it)
{
    if (!it->node)
        return;

    rb_remove(obj, it->node);
    free(it->node->key);
    free(it->node->data);
    free(it->node);
}

/* Empty map */
void map_clear(map_t obj)
{
    rb_destroy_recurse(obj, obj->root);
    obj->root = NULL;
}

/* Destructor */
void map_delete(map_t obj)
{
    map_clear(obj);
    free(obj);
}
