#include "map.h"
#include <memory.h>

static map_node *map_create_node(void *key,
                                 void *value,
                                 size_t ksize,
                                 size_t vsize)
{
    map_node *node = malloc(sizeof(map_node));

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

static void map_delete_node(map_t UNUSED, map_node *node)
{
    free(node->key);
    free(node->val);
    free(node);
}

/* Constructor */
map_t map_new(size_t s1,
              size_t s2,
              int (*cmp)(const map_node *, const map_node *))
{
    map_t tree = (map_internal_t *) malloc(sizeof(map_internal_t));
    tree->key_size = s1;
    tree->val_size = s2;
    internal_map_new(tree);
    return tree;
}

/* Add function */
bool map_insert(map_t obj, void *key, void *val)
{
    map_node *node = map_create_node(key, val, obj->key_size, obj->val_size);
    internal_map_insert(obj, node);
    return true;
}

/* Get functions */
void map_find(map_t obj, map_iter_t *it, void *key)
{
    map_node *tmp_node = (map_node *) malloc(sizeof(map_node));
    tmp_node->key = key;
    it->node = internal_map_search(obj, tmp_node);
    free(tmp_node);
}

bool map_empty(map_t obj)
{
    return (NULL == obj->root);
}

/* Iteration */
bool map_at_end(map_t UNUSED, map_iter_t *it)
{
    return (NULL == it->node);
}

/* Remove functions */
void map_erase(map_t obj, map_iter_t *it)
{
    if (NULL == it->node)
        return;
    internal_map_remove(obj, it->node);
    map_delete_node(obj, it->node);
}

/* Empty map */
void map_clear(map_t obj)
{
    internal_map_destroy(obj, cb, NULL);
    // FIXME: Not freeing all nodes
}

/* Destructor */
void map_delete(map_t obj)
{
    map_clear(obj);
    free(obj);
}
