/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* clang-format off */

/*
 * This map implementation has undergone extensive modifications, heavily
 * relying on the rb.h header file from jemalloc.
 * See https://github.com/jemalloc/jemalloc/blob/dev/include/jemalloc/internal/rb.h .
 * The original rb.h file served as the foundation and source of inspiration
 * for adapting and tailoring it specifically for this map implementation.
 * Therefore, credit and sincere thanks are extended to jemalloc for their
 * invaluable work.
 */

/* clang-format on */

#include <assert.h>
#include <memory.h>

#include "map.h"

/*
 * Each node in the red-black tree consumes at least 1 byte of space (for the
 * linkage if nothing else, so there are a maximum of sizeof(void *) << 3 rb
 * tree nodes in any process (and thus, at most sizeof(void *) << 3 nodes in any
 * rb tree). The choice of algorithm bounds the depth of a tree to twice the
 * binary log of the number of elements in the tree; the following bound
 * follows.
 */
#define RB_MAX_DEPTH (sizeof(void *) << 4)

/* Left accessors */
static inline map_node_t *rbnode_get_left(map_node_t *node)
{
    return node->link.left;
}

static inline void rbnode_set_left(map_node_t *node, map_node_t *left)
{
    node->link.left = left;
}

/* Right accessors */
static inline map_node_t *rbnode_get_right(map_node_t *node)
{
    return (map_node_t *) (((uintptr_t) node->link.right_red) & ~3);
}

static inline void rbnode_set_right(map_node_t *node, map_node_t *right)
{
    node->link.right_red =
        (map_node_t *) (((uintptr_t) right) |
                        (((uintptr_t) node->link.right_red) & 1));
}

/* Color accessors */
static inline map_color_t rbnode_get_color(const map_node_t *node)
{
    return (map_color_t) (((uintptr_t) node->link.right_red) & 1);
}

static inline void rbnode_set_color(map_node_t *node, map_color_t color)
{
    node->link.right_red =
        (map_node_t *) (((uintptr_t) node->link.right_red & ~3) |
                        (uintptr_t) color);
}

static inline void rbnode_set_red(map_node_t *node)
{
    node->link.right_red =
        (map_node_t *) (((uintptr_t) node->link.right_red) | 1);
}

static inline void rbnode_set_black(map_node_t *node)
{
    node->link.right_red =
        (map_node_t *) (((uintptr_t) node->link.right_red) & ~3);
}

/* Node initializer */
static inline void rbnode_init(map_node_t *node)
{
    assert((((uintptr_t) node) & (0x1)) == RB_BLACK);
    rbnode_set_left(node, NULL);
    rbnode_set_right(node, NULL);
    rbnode_set_red(node);
}

/* Internal utility macros */
#define rbnode_rotate_left(x_type, x_field, x_node, r_node)    \
    do {                                                       \
        (r_node) = rbnode_get_right((x_node));                 \
        rbnode_set_right((x_node), rbnode_get_left((r_node))); \
        rbnode_set_left((r_node), (x_node));                   \
    } while (0)

#define rbnode_rotate_right(x_type, x_field, x_node, r_node)   \
    do {                                                       \
        (r_node) = rbnode_get_left((x_node));                  \
        rbnode_set_left((x_node), rbnode_get_right((r_node))); \
        rbnode_set_right((r_node), (x_node));                  \
    } while (0)

typedef struct {
    map_node_t *node;
    int cmp;
} rb_path_entry_t;

static map_node_t *rb_search(map_t rbtree, const map_node_t *keynode)
{
    int cmp;
    map_node_t *ret = rbtree->root;
    while (ret && (cmp = (rbtree->cmp)(keynode->key, ret->key)) != 0) {
        if (cmp < 0) {
            ret = rbnode_get_left(ret);
        } else {
            ret = rbnode_get_right(ret);
        }
    }
    return ret;
}

static void rb_insert(map_t rbtree, map_node_t *node)
{
    rb_path_entry_t path[RB_MAX_DEPTH];
    rb_path_entry_t *pathp;
    rbnode_init(node);
    /* Wind. */
    path->node = rbtree->root;
    for (pathp = path; pathp->node; pathp++) {
        int cmp = pathp->cmp = (rbtree->cmp)(node->key, pathp->node->key);
        if (cmp == 0) {
            break; /* If the key matches something, don't insert */
        }
        if (cmp < 0) {
            pathp[1].node = rbnode_get_left(pathp->node);
        } else {
            pathp[1].node = rbnode_get_right(pathp->node);
        }
    }
    pathp->node = node;
    /* A loop invariant we maintain is that all nodes with
     * out-of-date summaries live in path[0], path[1], ..., *pathp.
     * To maintain this, we have to summarize node, since we
     * decrement pathp before the first iteration.
     */
    assert(!rbnode_get_left(node));
    assert(!rbnode_get_right(node));
    /* Unwind. */
    for (pathp--; (uintptr_t) pathp >= (uintptr_t) path; pathp--) {
        map_node_t *cnode = pathp->node;
        if (pathp->cmp < 0) {
            map_node_t *left = pathp[1].node;
            rbnode_set_left(cnode, left);
            if (rbnode_get_color(left) == RB_BLACK)
                return;
            map_node_t *leftleft = rbnode_get_left(left);
            if (leftleft && (rbnode_get_color(leftleft) == RB_RED)) {
                /* Fix up 4-node. */
                map_node_t *tnode;
                rbnode_set_black(leftleft);
                rbnode_rotate_right(map_node_t, link, cnode, tnode);
                cnode = tnode;
            }
        } else {
            map_node_t *right = pathp[1].node;
            rbnode_set_right(cnode, right);
            if (rbnode_get_color(right) == RB_BLACK)
                return;
            map_node_t *left = rbnode_get_left(cnode);
            if (left && (rbnode_get_color(left) == RB_RED)) {
                /* Split 4-node. */
                rbnode_set_black(left);
                rbnode_set_black(right);
                rbnode_set_red(cnode);
            } else {
                /* Lean left. */
                map_node_t *tnode;
                map_color_t tcolor = rbnode_get_color(cnode);
                rbnode_rotate_left(map_node_t, link, cnode, tnode);
                rbnode_set_color(tnode, tcolor);
                rbnode_set_red(cnode);
                cnode = tnode;
            }
        }
        pathp->node = cnode;
    }
    /* Set root, and make it black. */
    rbtree->root = path->node;
    rbnode_set_black(rbtree->root);
}

static void rb_remove(map_t rbtree, map_node_t *node)
{
    rb_path_entry_t path[RB_MAX_DEPTH];
    rb_path_entry_t *pathp = NULL;
    rb_path_entry_t *nodep = NULL;
    /* Wind. */
    path->node = rbtree->root;
    for (pathp = path; pathp->node; pathp++) {
        int cmp = pathp->cmp = (rbtree->cmp)(node->val, pathp->node->val);
        if (cmp < 0) {
            pathp[1].node = rbnode_get_left(pathp->node);
        } else {
            pathp[1].node = rbnode_get_right(pathp->node);
            if (cmp == 0) {
                /* Find node's successor, in preparation for swap. */
                pathp->cmp = 1;
                nodep = pathp;
                for (pathp++; pathp->node; pathp++) {
                    pathp->cmp = -1;
                    pathp[1].node = rbnode_get_left(pathp->node);
                }
                break;
            }
        }
    }
    assert(nodep->node == node);
    pathp--;
    if (pathp->node != node) {
        /* Swap node with its successor. */
        map_color_t tcolor = rbnode_get_color(pathp->node);
        rbnode_set_color(pathp->node, rbnode_get_color(node));
        rbnode_set_left(pathp->node, rbnode_get_left(node));
        /* If node's successor is its right child, the following code
         * will do the wrong thing for the right child pointer.
         * However, it doesn't matter, because the pointer will be
         * properly set when the successor is pruned.
         */
        rbnode_set_right(pathp->node, rbnode_get_right(node));
        rbnode_set_color(node, tcolor);
        /* The pruned leaf node's child pointers are never accessed
         * again, so don't bother setting them to nil.
         */
        nodep->node = pathp->node;
        pathp->node = node;
        if (nodep == path) {
            rbtree->root = nodep->node;
        } else {
            if (nodep[-1].cmp < 0) {
                rbnode_set_left(nodep[-1].node, nodep->node);
            } else {
                rbnode_set_right(nodep[-1].node, nodep->node);
            }
        }
    } else {
        map_node_t *left = rbnode_get_left(node);
        if (left) {
            /* node has no successor, but it has a left child.
             * Splice node out, without losing the left child.
             */
            assert(rbnode_get_color(node) == RB_BLACK);
            assert(rbnode_get_color(left) == RB_RED);
            rbnode_set_black(left);
            if (pathp == path) {
                rbtree->root = left;
                /* Nothing to summarize -- the subtree rooted at the
                 * node's left child hasn't changed, and it's now the
                 * root.
                 */
            } else {
                if (pathp[-1].cmp < 0) {
                    rbnode_set_left(pathp[-1].node, left);
                } else {
                    rbnode_set_right(pathp[-1].node, left);
                }
            }
            return;
        } else if (pathp == path) {
            /* The tree only contained one node. */
            rbtree->root = NULL;
            return;
        }
    }
    /* We've now established the invariant that the node has no right
     * child (well, morally; we didn't bother nulling it out if we
     * swapped it with its successor), and that the only nodes with
     * out-of-date summaries live in path[0], path[1], ..., pathp[-1].
     */
    if (rbnode_get_color(pathp->node) == RB_RED) {
        /* Prune red node, which requires no fixup. */
        assert(pathp[-1].cmp < 0);
        rbnode_set_left(pathp[-1].node, NULL);
        return;
    }
    /* The node to be pruned is black, so unwind until balance is
     * restored.
     */
    pathp->node = NULL;
    for (pathp--; (uintptr_t) pathp >= (uintptr_t) path; pathp--) {
        assert(pathp->cmp != 0);
        if (pathp->cmp < 0) {
            rbnode_set_left(pathp->node, pathp[1].node);
            if (rbnode_get_color(pathp->node) == RB_RED) {
                map_node_t *right = rbnode_get_right(pathp->node);
                map_node_t *rightleft = rbnode_get_left(right);
                map_node_t *tnode;
                if (rightleft && (rbnode_get_color(rightleft) == RB_RED)) {
                    /* In the following diagrams, ||, //, and \\
                     * indicate the path to the removed node.
                     *
                     *      ||
                     *    pathp(r)
                     *  //        \
                     * (b)        (b)
                     *           /
                     *          (r)
                     *
                     */
                    rbnode_set_black(pathp->node);
                    rbnode_rotate_right(map_node_t, link, right, tnode);
                    rbnode_set_right(pathp->node, tnode);
                    rbnode_rotate_left(map_node_t, link, pathp->node, tnode);
                } else {
                    /*      ||
                     *    pathp(r)
                     *  //        \
                     * (b)        (b)
                     *           /
                     *          (b)
                     *
                     */
                    rbnode_rotate_left(map_node_t, link, pathp->node, tnode);
                }
                /* Balance restored, but rotation modified subtree
                 * root.
                 */
                assert((uintptr_t) pathp > (uintptr_t) path);
                if (pathp[-1].cmp < 0) {
                    rbnode_set_left(pathp[-1].node, tnode);
                } else {
                    rbnode_set_right(pathp[-1].node, tnode);
                }
                return;
            } else {
                map_node_t *right = rbnode_get_right(pathp->node);
                map_node_t *rightleft = rbnode_get_left(right);
                if (rightleft && (rbnode_get_color(rightleft) == RB_RED)) {
                    /*      ||
                     *    pathp(b)
                     *  //        \
                     * (b)        (b)
                     *           /
                     *          (r)
                     */
                    map_node_t *tnode;
                    rbnode_set_black(rightleft);
                    rbnode_rotate_right(map_node_t, link, right, tnode);
                    rbnode_set_right(pathp->node, tnode);
                    rbnode_rotate_left(map_node_t, link, pathp->node, tnode);
                    /* Balance restored, but rotation modified
                     * subtree root, which may actually be the tree
                     * root.
                     */
                    if (pathp == path) {
                        /* Set root. */
                        rbtree->root = tnode;
                    } else {
                        if (pathp[-1].cmp < 0) {
                            rbnode_set_left(pathp[-1].node, tnode);
                        } else {
                            rbnode_set_right(pathp[-1].node, tnode);
                        }
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
                    rbnode_set_red(pathp->node);
                    rbnode_rotate_left(map_node_t, link, pathp->node, tnode);
                    pathp->node = tnode;
                }
            }
        } else {
            map_node_t *left;
            rbnode_set_right(pathp->node, pathp[1].node);
            left = rbnode_get_left(pathp->node);
            if (rbnode_get_color(left) == RB_RED) {
                map_node_t *tnode;
                map_node_t *leftright = rbnode_get_right(left);
                map_node_t *leftrightleft = rbnode_get_left(leftright);
                if (leftrightleft &&
                    (rbnode_get_color(leftrightleft) == RB_RED)) {
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
                    rbnode_set_black(leftrightleft);
                    rbnode_rotate_right(map_node_t, link, pathp->node, unode);
                    rbnode_rotate_right(map_node_t, link, pathp->node, tnode);
                    rbnode_set_right(unode, tnode);
                    rbnode_rotate_left(map_node_t, link, unode, tnode);
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
                    rbnode_set_red(leftright);
                    rbnode_rotate_right(map_node_t, link, pathp->node, tnode);
                    rbnode_set_black(tnode);
                }
                /* Balance restored, but rotation modified subtree
                 * root, which may actually be the tree root.
                 */
                if (pathp == path) {
                    /* Set root. */
                    rbtree->root = tnode;
                } else {
                    if (pathp[-1].cmp < 0) {
                        rbnode_set_left(pathp[-1].node, tnode);
                    } else {
                        rbnode_set_right(pathp[-1].node, tnode);
                    }
                }
                return;
            } else if (rbnode_get_color(pathp->node) == RB_RED) {
                map_node_t *leftleft = rbnode_get_left(left);
                if (leftleft && (rbnode_get_color(leftleft) == RB_RED)) {
                    /*        ||
                     *      pathp(r)
                     *     /        \\
                     *   (b)        (b)
                     *   /
                     * (r)
                     */
                    map_node_t *tnode;
                    rbnode_set_black(pathp->node);
                    rbnode_set_red(left);
                    rbnode_set_black(leftleft);
                    rbnode_rotate_right(map_node_t, link, pathp->node, tnode);
                    /* Balance restored, but rotation modified
                     * subtree root.
                     */
                    assert((uintptr_t) pathp > (uintptr_t) path);
                    if (pathp[-1].cmp < 0) {
                        rbnode_set_left(pathp[-1].node, tnode);
                    } else {
                        rbnode_set_right(pathp[-1].node, tnode);
                    }
                    return;
                } else {
                    /*        ||
                     *      pathp(r)
                     *     /        \\
                     *   (b)        (b)
                     *   /
                     * (b)
                     */
                    rbnode_set_red(left);
                    rbnode_set_black(pathp->node);
                    /* Balance restored. */
                    return;
                }
            } else {
                map_node_t *leftleft = rbnode_get_left(left);
                if (leftleft && (rbnode_get_color(leftleft) == RB_RED)) {
                    /*               ||
                     *             pathp(b)
                     *            /        \\
                     *          (b)        (b)
                     *          /
                     *        (r)
                     */
                    map_node_t *tnode;
                    rbnode_set_black(leftleft);
                    rbnode_rotate_right(map_node_t, link, pathp->node, tnode);
                    /* Balance restored, but rotation modified        */
                    /* subtree root, which may actually be the tree   */
                    /* root.                                          */
                    if (pathp == path) {
                        /* Set root. */
                        rbtree->root = tnode;
                    } else {
                        if (pathp[-1].cmp < 0) {
                            rbnode_set_left(pathp[-1].node, tnode);
                        } else {
                            rbnode_set_right(pathp[-1].node, tnode);
                        }
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
                    rbnode_set_red(left);
                }
            }
        }
    }
    /* Set root. */
    rbtree->root = path->node;
    assert(rbnode_get_color(rbtree->root) == RB_BLACK);
}

static void rb_destroy_recurse(map_t rbtree, map_node_t *node)
{
    if (!node)
        return;
    rb_destroy_recurse(rbtree, rbnode_get_left(node));
    rbnode_set_left((node), NULL);
    rb_destroy_recurse(rbtree, rbnode_get_right(node));
    rbnode_set_right((node), NULL);
    free(node);
}

static map_node_t *map_create_node(void *key,
                                   void *value,
                                   size_t ksize,
                                   size_t vsize)
{
    map_node_t *node = malloc(sizeof(map_node_t));

    /* Allocate memory for the keys and values */
    node->key = malloc(ksize);
    node->val = malloc(vsize);

    /*
     * Copy over the key and values
     *
     * If the parameter passed in is NULL, make the element blank instead of
     * a segfault.
     */
    if (!key)
        memset(node->key, 0, ksize);
    else
        memcpy(node->key, key, ksize);

    if (!value)
        memset(node->val, 0, vsize);
    else
        memcpy(node->val, value, vsize);

    return node;
}

static void map_delete_node(map_t UNUSED, map_node_t *node)
{
    free(node->key);
    free(node->val);
    free(node);
}

/* Constructor */
map_t map_new(size_t s1, size_t s2, int (*cmp)(const void *, const void *))
{
    map_t tree = (map_t) malloc(sizeof(map_head_t));
    tree->key_size = s1;
    tree->val_size = s2;
    tree->cmp = cmp;
    tree->root = NULL;
    return tree;
}

/* Add function */
bool map_insert(map_t obj, void *key, void *val)
{
    map_node_t *node = map_create_node(key, val, obj->key_size, obj->val_size);
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
bool map_at_end(map_t UNUSED, map_iter_t *it)
{
    return !it->node;
}

/* Remove functions */
void map_erase(map_t obj, map_iter_t *it)
{
    if (!it->node)
        return;
    rb_remove(obj, it->node);
    map_delete_node(obj, it->node);
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
