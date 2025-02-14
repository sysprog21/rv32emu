#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>

/* Obtain the system's notion of the current Greenwich time.
 * TODO: manipulate current time zone.
 */
void rv_gettimeofday(struct timeval *tv);

/* Retrieve the value used by a clock which is specified by clock_id. */
void rv_clock_gettime(struct timespec *tp);

#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)

typedef uint64_t rv_hash_key_t;

#define HASH_FUNC_IMPL(name, size_bits, size)                      \
    FORCE_INLINE rv_hash_key_t name(rv_hash_key_t val)             \
    {                                                              \
        /* 0x61c8864680b583eb is 64-bit golden ratio */            \
        return (val * 0x61c8864680b583ebull >> (64 - size_bits)) & \
               ((size) - (1));                                     \
    }
#else

typedef uint32_t rv_hash_key_t;

/* This hashing routine is adapted from Linux kernel.
 * See
 * https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/include/linux/hash.h
 */
#define HASH_FUNC_IMPL(name, size_bits, size)                           \
    FORCE_INLINE rv_hash_key_t name(rv_hash_key_t val)                  \
    {                                                                   \
        /* 0x61C88647 is 32-bit golden ratio */                         \
        return (val * 0x61C88647 >> (32 - size_bits)) & ((size) - (1)); \
    }
#endif

/* sanitize_path returns the shortest path name equivalent to path
 * by purely lexical processing. It applies the following rules
 * iteratively until no further processing can be done:
 *
 *  1. Replace multiple slashes with a single slash.
 *  2. Eliminate each . path name element (the current directory).
 *  3. Eliminate each inner .. path name element (the parent directory)
 *     along with the non-.. element that precedes it.
 *  4. Eliminate .. elements that begin a rooted path:
 *     that is, replace "/.." by "/" at the beginning of a path.
 *
 * The returned path ends in a slash only if it is the root "/".
 *
 * If the result of this process is an empty string, Clean
 * returns the string ".".
 *
 * See also Rob Pike, “Lexical File Names in Plan 9 or
 * Getting Dot-Dot Right,”
 * https://9p.io/sys/doc/lexnames.html
 *
 * Reference:
 * https://cs.opensource.google/go/go/+/refs/tags/go1.21.4:src/path/path.go;l=51
 */
char *sanitize_path(const char *input);

static inline uintptr_t align_up(uintptr_t sz, size_t alignment)
{
    uintptr_t mask = alignment - 1;
    if (likely((alignment & mask) == 0))
        return ((sz + mask) & ~mask);
    return (((sz + mask) / alignment) * alignment);
}

/* Linux-like List API */

struct list_head {
    struct list_head *prev, *next;
};

static inline void INIT_LIST_HEAD(struct list_head *head)
{
    head->next = head->prev = head;
}

static inline bool list_empty(const struct list_head *head)
{
    return head->next == head;
}

static inline void list_add(struct list_head *node, struct list_head *head)
{
    struct list_head *next = head->next;

    next->prev = node;
    node->next = next;
    node->prev = head;
    head->next = node;
}

static inline void list_del(struct list_head *node)
{
    struct list_head *next = node->next, *prev = node->prev;

    next->prev = prev;
    prev->next = next;
}

static inline void list_del_init(struct list_head *node)
{
    list_del(node);
    INIT_LIST_HEAD(node);
}

#define list_entry(node, type, member) container_of(node, type, member)

#define list_first_entry(head, type, member) \
    list_entry((head)->next, type, member)

#define list_last_entry(head, type, member) \
    list_entry((head)->prev, type, member)

#ifdef __HAVE_TYPEOF
#define list_for_each_entry(entry, head, member)                       \
    for (entry = list_entry((head)->next, __typeof__(*entry), member); \
         &entry->member != (head);                                     \
         entry = list_entry(entry->member.next, __typeof__(*entry), member))

#define list_for_each_entry_safe(entry, safe, head, member)                \
    for (entry = list_entry((head)->next, __typeof__(*entry), member),     \
        safe = list_entry(entry->member.next, __typeof__(*entry), member); \
         &entry->member != (head); entry = safe,                           \
        safe = list_entry(safe->member.next, __typeof__(*entry), member))
#else
#define list_for_each_entry(entry, head, member, type)   \
    for (entry = list_entry((head)->next, type, member); \
         &entry->member != (head);                       \
         entry = list_entry(entry->member.next, type, member))

#define list_for_each_entry_safe(entry, safe, head, member, type) \
    for (entry = list_entry((head)->next, type, member),          \
        safe = list_entry(entry->member.next, type, member);      \
         &entry->member != (head);                                \
         entry = safe, safe = list_entry(safe->member.next, type, member))
#endif

#define SET_SIZE_BITS 10
#define SET_SIZE (1 << SET_SIZE_BITS)
#define SET_SLOTS_SIZE 32

#if RV32_HAS(JIT) && RV32_HAS(SYSTEM)
/*
 * Use composed key in JIT. The higher 32 bits stores the value of supervisor
 * address translation and protection (SATP) register, and the lower 32 bits
 * stores the program counter (PC) as same as userspace simulation.
 */
#define RV_HASH_KEY(block) \
    ((((rv_hash_key_t) block->satp) << 32) | (rv_hash_key_t) block->pc_start)
#else
#define RV_HASH_KEY(block) ((rv_hash_key_t) block->pc_start)
#endif

/* The set consists of SET_SIZE buckets, with each bucket containing
 * SET_SLOTS_SIZE slots.
 */
typedef struct {
    rv_hash_key_t table[SET_SIZE][SET_SLOTS_SIZE];
} set_t;

/**
 * set_reset - clear a set
 * @set: a pointer points to target set
 */
void set_reset(set_t *set);

/**
 * set_add - insert a new element into the set
 * @set: a pointer points to target set
 * @key: the key of the inserted entry
 */
bool set_add(set_t *set, rv_hash_key_t key);

/**
 * set_has - check whether the element exist in the set or not
 * @set: a pointer points to target set
 * @key: the key of the inserted entry
 */
bool set_has(set_t *set, rv_hash_key_t key);
