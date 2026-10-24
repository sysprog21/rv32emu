/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <dlfcn.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "aot_hash.h"
#include "cache.h"
#include "jit.h"
#include "riscv_private.h"
#if RV32_HAS(SYSTEM)
#include "system.h"
#endif

/* 64-bit FNV-1a parameters, shared by the file and page fingerprints. */
#define AOT_FNV_OFFSET UINT64_C(14695981039346656037)
#define AOT_FNV_PRIME UINT64_C(1099511628211)

/* Compatibility fingerprints are not authentication: libraries are native code.
 */
static bool aot_file_hash(const char *path, uint64_t *hash)
{
    FILE *file = path ? fopen(path, "rb") : NULL;
    if (!file)
        return false;
    uint64_t value = AOT_FNV_OFFSET;
    unsigned char buf[16384];
    size_t n;
    do {
        n = fread(buf, 1, sizeof(buf), file);
        for (size_t i = 0; i < n; i++) {
            value ^= buf[i];
            value *= AOT_FNV_PRIME;
        }
    } while (n == sizeof(buf));
    bool ok = !ferror(file);
    fclose(file);
    *hash = value;
    return ok;
}

static bool aot_identity(riscv_t *rv, uint64_t identity[6])
{
    vm_attr_t *attr = PRIV(rv);
    identity[0] = 3;
    identity[5] = RV_AOT_ABI;
    identity[3] = attr->mem_size;
    identity[4] = attr->allow_misalign;
#if RV32_HAS(SYSTEM_MMIO)
    const char *program = attr->data.system.kernel;
#else
    const char *program = attr->data.user.elf_program;
#endif
    return aot_file_hash("/proc/self/exe", &identity[1]) &&
           aot_file_hash(program, &identity[2]);
}

/* Cache page identity by physical address so aliases agree. SFENCE changes
 * virtual bindings and retains these hashes; FENCE.I makes them stale. FNV is
 * only an index key: binding additionally verifies the complete SHA-256 digest.
 * During collection, hash freshly decoded pages so saved IR and its digest
 * describe the same bytes even when a page contains mutable data. At replay,
 * an exact page snapshot permits lazy revalidation after FENCE.I without
 * recomputing SHA-256 for unchanged pages. Missing snapshot storage simply
 * falls back to hashing, including when its optional allocation fails.
 */
#if RV32_HAS(SYSTEM)
static uint64_t aot_page_hash(riscv_t *rv,
                              uint32_t start,
                              uint32_t end,
                              bool fresh,
                              uint8_t digest[32])
{
    uint32_t page = start & ~(RV_PG_SIZE - 1);
    /* RV32 instruction ranges can wrap past UINT32_MAX. Compare their
     * modular length with the bytes left in the first page, retaining a
     * compressed instruction that ends exactly at address zero.
     */
    if ((uint32_t) (end - start) > RV_PG_SIZE - (start & (RV_PG_SIZE - 1)))
        return 0;
    uint32_t physical;
    if (!mmu_code_translate(rv, page, &physical) ||
        (uint64_t) physical + RV_PG_SIZE > PRIV(rv)->mem->mem_size)
        return 0;
    unsigned slot = (physical / RV_PG_SIZE) % (sizeof(rv->aot_page_hashes) /
                                               sizeof(rv->aot_page_hashes[0]));
    const uint8_t *page_bytes = rv->ram_base + physical;
    uint8_t *snapshot = rv->aot_page_snapshots
                            ? rv->aot_page_snapshots + slot * RV_PG_SIZE
                            : NULL;
    if (rv->aot_page_hashes[slot].hash &&
        rv->aot_page_hashes[slot].physical == physical && !fresh) {
        bool valid = rv->aot_page_hashes[slot].epoch == rv->aot_hash_epoch;
        if (!valid && snapshot) {
            rv->aot_hash_rechecks++;
            valid = !memcmp(snapshot, page_bytes, RV_PG_SIZE);
            if (valid)
                rv->aot_hash_reuses++;
        }
        if (valid) {
            rv->aot_page_hashes[slot].epoch = rv->aot_hash_epoch;
            memcpy(digest, rv->aot_page_hashes[slot].digest, 32);
            rv->aot_hash_hits++;
            return rv->aot_page_hashes[slot].hash;
        }
    }
    rv->aot_hash_misses++;
    if (snapshot) {
        memcpy(snapshot, page_bytes, RV_PG_SIZE);
        /* Hash the saved bytes so the digest and snapshot always agree. */
        page_bytes = snapshot;
    }
    uint64_t hash = AOT_FNV_OFFSET;
    for (unsigned i = 0; i < RV_PG_SIZE; i += sizeof(uint64_t)) {
        uint64_t word;
        memcpy(&word, page_bytes + i, sizeof(word));
        hash = (hash ^ word) * AOT_FNV_PRIME;
    }
    aot_page_digest(page_bytes, digest);
    memcpy(rv->aot_page_hashes[slot].digest, digest, 32);
    hash = hash ? hash : 1;
    rv->aot_page_hashes[slot].physical = physical;
    rv->aot_page_hashes[slot].hash = hash;
    rv->aot_page_hashes[slot].epoch = rv->aot_hash_epoch;
    return hash;
}

uint64_t t2c_aot_page_hash(riscv_t *rv, block_t *block)
{
    return aot_page_hash(rv, block->pc_start, block->pc_end,
                         PRIV(rv)->aot_collect && block->ir_head,
                         block->aot_page_digest);
}
#endif

static bool aot_load_collector(riscv_t *rv)
{
    char path[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length <= 0 || length >= (ssize_t) sizeof(path) - 1)
        return false;
    path[length] = '\0';
    char *base = strrchr(path, '/');
    if (!base || (size_t) (base - path) + sizeof("/libt2c.so") > sizeof(path))
        return false;
    strcpy(base, "/libt2c.so");
    rv->aot_collector = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (rv->aot_collector) {
        const uint64_t *abi =
            dlsym(rv->aot_collector, "rv32_aot_collector_abi");
        const uint32_t *producer =
            dlsym(rv->aot_collector, "rv32_aot_producer_llvm");
        const uint32_t *call_conv =
            dlsym(rv->aot_collector, "rv32_aot_call_conv");
        if (abi && *abi == RV_AOT_ABI && producer && *producer >= 19 &&
            *producer <= 23 && call_conv) {
            rv->aot_producer_llvm = *producer;
            rv->aot_call_conv = *call_conv;
            rv->aot_lift = dlsym(rv->aot_collector, "t2c_compile");
        } else {
            rv_log_error("AOT collector source/configuration ABI mismatch");
        }
    }
    if (!rv->aot_lift) {
        const char *error = dlerror();
        rv_log_error("Cannot load compatible LLVM bitcode collector: %s",
                     error ? error : "missing entry or incompatible ABI");
        if (rv->aot_collector)
            dlclose(rv->aot_collector);
        rv->aot_collector = NULL;
        return false;
    }
    return true;
}


#if !RV32_HAS(SYSTEM)
static void aot_user_slow_path(riscv_t *rv, uint32_t addr, uint32_t flags)
{
    if (flags == AOT_USER_INTERPRET)
        t2c_interpret_block(rv);
    else
        jit_misaligned_trap(rv, addr, flags);
}
#endif

bool t2c_aot_init(riscv_t *rv)
{
    vm_attr_t *attr = PRIV(rv);
    rv->aot_disabled = false;
#if RV32_HAS(SYSTEM)
    rv->aot_misaligned_trap = jit_misaligned_trap;
    rv->aot_insn = t2c_run_outlined_insn;
    rv->aot_signal = &need_handle_signal;
#else
    rv->aot_misaligned_trap = aot_user_slow_path;
#endif
    if (!aot_enabled(rv))
        return true;
    if (!aot_identity(rv, rv->aot_identity)) {
        rv_log_error("Cannot fingerprint AOT executable or guest image");
        return false;
    }
    if (attr->aot_collect) {
        if (!attr->aot_collect_started && mkdir(attr->aot_collect, 0700)) {
            rv_log_error("AOT collection requires a new directory: %s",
                         attr->aot_collect);
            return false;
        }
        attr->aot_collect_started = true;
#if RV32_HAS(SYSTEM)
        if (!aot_load_collector(rv))
            return false;
#endif
        if (!attr->aot_library)
            return true;
    }
    /* A bare file name would be searched for along the library path. */
    char library[PATH_MAX];
    if (snprintf(library, sizeof(library), "%s%s",
                 strchr(attr->aot_library, '/') ? "" : "./",
                 attr->aot_library) >= (int) sizeof(library)) {
        rv_log_error("AOT library path is too long");
        return false;
    }
    rv->aot_handle = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (!rv->aot_handle) {
        rv_log_error("Cannot load AOT library: %s", dlerror());
        return false;
    }
    const uint64_t *found = dlsym(rv->aot_handle, "rv32_aot_identity");
    if (!found || memcmp(rv->aot_identity, found, sizeof(rv->aot_identity))) {
        rv_log_error("AOT identity mismatch (emulator, image, or options)");
        goto reject;
    }
    /* The artifact derives the mask from its table size; probing needs a
     * power-of-two table.
     */
    rv->aot_index = dlsym(rv->aot_handle, "rv32_aot_index");
    const uint32_t *mask = dlsym(rv->aot_handle, "rv32_aot_index_mask");
    if (!rv->aot_index || !mask || (*mask & (*mask + 1))) {
        rv_log_error("AOT artifact lacks a valid entry index");
        goto reject;
    }
    rv->aot_index_mask = *mask;
    rv->aot_enter = dlsym(rv->aot_handle, "rv32_aot_enter");
#if RV32_HAS(SYSTEM)
    rv->aot_pages = calloc((1U << 20) / 64, sizeof(uint64_t));
    if (!rv->aot_pages) {
        rv_log_error("Cannot allocate the AOT page map");
        goto reject;
    }
    for (uint32_t slot = 0; slot <= rv->aot_index_mask; slot++) {
        if (!rv->aot_index[slot].entry)
            continue;
        uint32_t page = rv->aot_index[slot].pc / RV_PG_SIZE;
        rv->aot_pages[page / 64] |= UINT64_C(1) << (page % 64);
    }
    memset(rv->aot_absent_valid, 0, sizeof(rv->aot_absent_valid));
    rv->aot_page_snapshots =
        malloc(ARRAY_SIZE(rv->aot_page_hashes) * RV_PG_SIZE);
#endif
    return true;

reject:
#if RV32_HAS(SYSTEM)
    free(rv->aot_pages);
    rv->aot_pages = NULL;
#endif
    dlclose(rv->aot_handle);
    rv->aot_handle = NULL;
    rv->aot_index = NULL;
    rv->aot_enter = NULL;
    return false;
}

/* Probe the artifact index for PC. Unless any_page is set, the entry must
 * also match the page key and, in system mode, its SHA-256 digest. User-mode
 * entries carry a zero page key and no digest.
 */
static const struct aot_entry *aot_index_scan(const riscv_t *rv,
                                              uint32_t slot,
                                              uint32_t pc,
                                              bool any_page,
                                              uint64_t hash,
                                              const uint8_t *digest)
{
    for (; rv->aot_index[slot].entry; slot = (slot + 1) & rv->aot_index_mask) {
        const struct aot_entry *entry = &rv->aot_index[slot];
        if (entry->pc != pc)
            continue;
        if (any_page)
            return entry;
        if (entry->hash == hash &&
            (!RV32_HAS(SYSTEM) ||
             (entry->digest && !memcmp(entry->digest, digest, 32))))
            return entry;
    }
    return NULL;
}

static const struct aot_entry *aot_index_find(const riscv_t *rv,
                                              uint32_t pc,
                                              bool any_page,
                                              uint64_t hash,
                                              const uint8_t *digest)
{
    /* Fold the high product bits down: an odd multiplier alone permutes the
     * low bits, so masking it would cluster entries at equal page offsets.
     * tools/aot.py builds the table with the same function.
     */
    uint32_t product = (pc >> 1) * JIT_CACHE_HASH_MULTIPLIER;
    uint32_t slot = (product ^ (product >> 16)) & rv->aot_index_mask;
    return aot_index_scan(rv, slot, pc, any_page, hash, digest);
}

#if RV32_HAS(SYSTEM)
/* Only absence from the immutable artifact is cached. Mapping, permission,
 * and page-content mismatches must be retried normally.
 */
#define AOT_ABSENT_SLOTS ARRAY_SIZE(((riscv_t *) 0)->aot_absent_pc)

/* Whether the artifact may hold an entry on the page of pc. Without a page
 * map, as in unit tests that install an index directly, any page may.
 */
static inline bool aot_page_covered(const riscv_t *rv, uint32_t pc)
{
    const uint32_t page = pc / RV_PG_SIZE;
    return !rv->aot_pages ||
           (rv->aot_pages[page / 64] & (UINT64_C(1) << (page % 64)));
}

static bool aot_known_absent(const riscv_t *rv, uint32_t pc)
{
    unsigned slot = (pc >> 1) % AOT_ABSENT_SLOTS;
    return (rv->aot_absent_valid[slot / 64] & (UINT64_C(1) << (slot % 64))) &&
           rv->aot_absent_pc[slot] == pc;
}

static void aot_note_absent(riscv_t *rv, uint32_t pc)
{
    unsigned slot = (pc >> 1) % AOT_ABSENT_SLOTS;
    rv->aot_absent_pc[slot] = pc;
    rv->aot_absent_valid[slot / 64] |= UINT64_C(1) << (slot % 64);
}
#endif

#if RV32_HAS(SYSTEM)
/* The first artifact entry for PC, without translating it, or NULL. The
 * caller fetches the instruction next, so t2c_aot_lookup() hashes the page
 * the hart actually executes, after any fault handler that fetch ran, and
 * resumes the index probe from this entry.
 */
const struct aot_entry *t2c_aot_has_pc(riscv_t *rv)
{
    if (!rv->aot_index || rv->aot_disabled)
        return NULL;
    rv->aot_lookup_calls++;
    if (!aot_page_covered(rv, rv->PC) || aot_known_absent(rv, rv->PC)) {
        rv->aot_absent_hits++;
        return NULL;
    }
    const struct aot_entry *first = aot_index_find(rv, rv->PC, true, 0, NULL);
    if (first)
        return first;
    aot_note_absent(rv, rv->PC);
    rv->aot_lookup_absent++;
    return NULL;
}

/* Match PC's page against the artifact, probing from @first, which
 * t2c_aot_has_pc() returned for PC. Call only after a successful fetch: the
 * fetch fills the iTLB that the page translation then reuses.
 */
void *t2c_aot_lookup(riscv_t *rv, const struct aot_entry *first)
{
    uint8_t digest[32];
    uint64_t hash = aot_page_hash(rv, rv->PC, rv->PC + 2, false, digest);
    if (!hash) {
        rv->aot_lookup_unmapped++;
        return NULL;
    }
    const struct aot_entry *entry =
        aot_index_scan(rv, first - rv->aot_index, rv->PC, false, hash, digest);
    if (!entry) {
        rv->aot_lookup_mismatches++;
        return NULL;
    }
    rv->aot_bindings++;
    return (void *) entry->entry;
}
#endif

void t2c_aot_bind(riscv_t *rv, block_t *block)
{
    block->compiled = true;
    if (!rv->aot_handle || rv->aot_disabled || !block->aot_translatable)
        return;
#if RV32_HAS(SYSTEM)
    if (!block->aot_page_hash || !aot_page_covered(rv, block->pc_start))
        return;
    const struct aot_entry *entry =
        aot_index_find(rv, block->pc_start, false, block->aot_page_hash,
                       block->aot_page_digest);
#else
    const struct aot_entry *entry =
        aot_index_find(rv, block->pc_start, false, 0, NULL);
#endif
    if (!entry)
        return;
    block->func = entry->entry;
    rv->aot_bindings++;
    /* Native code looks entries up in the current address space, also for a
     * block that address spaces share.
     */
    uint64_t key = block->pc_start;
#if RV32_HAS(SYSTEM)
    key |= (uint64_t) BLOCK_SATP(block) << 32;
    if (BLOCK_SATP(block) == SATP_SHARED)
        rv->aot_kernel_shared = true;
#endif
    jit_cache_update(rv->jit_cache, key, block->func, BLOCK_PRIV(block));
    ATOMIC_STORE(&block->hot2, true, ATOMIC_RELEASE);
}

void t2c_aot_collect_block(riscv_t *rv, block_t *block)
{
    if (!rv->aot_lift || block->aot_saved || !block->aot_translatable ||
        rv->aot_error || rv->aot_disabled)
        return;
#if RV32_HAS(SYSTEM)
    /* Kernel image code is collected immediately. Keep filtering incidental
     * user mappings, whose virtual addresses can differ between boots.
     */
    if (!block->aot_page_hash || (block->priv_mode == RV_PRIV_U_MODE &&
                                  block->n_invoke < T2C_PROFILE_THRESHOLD))
        return;
#endif
    block->aot_saved = true;
    char path[PATH_MAX];
#if RV32_HAS(SYSTEM)
    int n = snprintf(path, sizeof(path), "%s/%08x_%016" PRIx64 ".bc",
                     PRIV(rv)->aot_collect, block->pc_start & ~(RV_PG_SIZE - 1),
                     block->aot_page_hash);
#else
    int n = snprintf(path, sizeof(path), "%s/%08x.bc", PRIV(rv)->aot_collect,
                     block->pc_start);
#endif
    if (n <= 0 || (size_t) n >= sizeof(path)) {
        rv->aot_error = true;
        return;
    }
#if !RV32_HAS(SYSTEM)
    if (!access(path, F_OK))
        return;
#endif
    if (!rv->aot_lift(rv, block, path))
        rv->aot_error = true;
    else
        rv->aot_collected++;
}

void t2c_aot_invalidate(riscv_t *rv, bool keep_shared UNUSED)
{
    rv->aot_code_epoch++;
#if !RV32_HAS(SYSTEM)
    rv->aot_disabled = true;
#else
    if (!++rv->aot_hash_epoch)
        memset(rv->aot_page_hashes, 0, sizeof(rv->aot_page_hashes));
#endif
#if RV32_HAS(SYSTEM)
    if (keep_shared) {
        jit_cache_retire_unshared(rv->jit_cache, rv->inline_cache);
    } else {
        jit_cache_clear(rv->jit_cache);
        inline_cache_clear(rv->inline_cache);
        rv->aot_kernel_shared = false;
    }
#else
    jit_cache_clear(rv->jit_cache);
    inline_cache_clear(rv->inline_cache);
#endif
#if !RV32_HAS(SYSTEM)
    /* User blocks survive FENCE.I, so unbind them here. System callers
     * invalidate or free every block next, and a large block cache made this
     * walk a measurable share of a Linux boot.
     */
    block_t *block;
    list_for_each_entry (block, &rv->block_list, list) {
        block->func = NULL;
        block->compiled = false;
        block->hot2 = false;
    }
#endif
}

/* A reboot discards decoded IR, but collection and artifact ownership span
 * the complete emulator lifetime. Finalize the manifest only on destruction.
 */
void t2c_aot_checkpoint(riscv_t *rv)
{
    if (!PRIV(rv)->aot_collect || !rv->aot_lift)
        return;
    block_t *block;
    list_for_each_entry (block, &rv->block_list, list) {
        t2c_aot_collect_block(rv, block);
    }
}

void t2c_aot_exit(riscv_t *rv)
{
    vm_attr_t *attr = PRIV(rv);
    const char *dir = attr->aot_collect;
    if (dir) {
        if (!rv->aot_lift && !aot_load_collector(rv))
            rv->aot_error = true;
        t2c_aot_checkpoint(rv);
        char path[PATH_MAX];
        int n = snprintf(path, sizeof(path), "%s/manifest", dir);
        bool ok = !rv->aot_error && !rv->aot_disabled && rv->aot_collected &&
                  n > 0 && (size_t) n < sizeof(path);
        FILE *file = ok ? fopen(path, "w") : NULL;
        if (file) {
            for (unsigned i = 0; i < 6; i++)
                if (fprintf(file, "%016" PRIx64 "\n", rv->aot_identity[i]) < 0)
                    ok = false;
            if (fprintf(file, "%016" PRIx64 "\n%016" PRIx64 "\n",
                        (uint64_t) rv->aot_producer_llvm,
                        (uint64_t) rv->aot_call_conv) < 0)
                ok = false;
            if (fclose(file))
                ok = false;
        } else {
            ok = false;
        }
        if (!ok) {
            rv_log_error(
                "AOT collection failed or no eligible blocks survived");
            attr->exit_code = EXIT_FAILURE;
        }
        rv->aot_lift = NULL;
        if (rv->aot_collector)
            dlclose(rv->aot_collector);
        rv->aot_collector = NULL;
    }
    if (attr->aot_library)
        fprintf(stderr,
                "AOT: entries=%" PRIu64 " collected=%" PRIu64
                " native_insns=%" PRIu64 " slow_calls=%" PRIu64
                " slow_resumes=%" PRIu64 " hash_hits=%" PRIu64
                " hash_misses=%" PRIu64 " bindings=%" PRIu64 " fences=%" PRIu64
                " hash_rechecks=%" PRIu64 " hash_reuses=%" PRIu64
                " t1_flushes=%" PRIu64 "\n",
                rv->aot_calls, rv->aot_collected, rv->aot_native_insns,
                rv->aot_slow_calls, rv->aot_slow_resumes, rv->aot_hash_hits,
                rv->aot_hash_misses, rv->aot_bindings, rv->aot_fences,
                rv->aot_hash_rechecks, rv->aot_hash_reuses, rv->aot_t1_flushes);
#if RV32_HAS(SYSTEM)
    if (attr->aot_library)
        fprintf(stderr,
                "AOT lookup: calls=%" PRIu64 " absent_hits=%" PRIu64
                " absent=%" PRIu64 " mismatches=%" PRIu64 " unmapped=%" PRIu64
                " dispatches=%" PRIu64 "\n",
                rv->aot_lookup_calls, rv->aot_absent_hits,
                rv->aot_lookup_absent, rv->aot_lookup_mismatches,
                rv->aot_lookup_unmapped, rv->aot_dispatches);
#endif
    t2c_aot_invalidate(rv, false);
#if RV32_HAS(SYSTEM)
    free(rv->aot_page_snapshots);
    rv->aot_page_snapshots = NULL;
    free(rv->aot_pages);
    rv->aot_pages = NULL;
#endif
    if (rv->aot_handle) {
        rv->aot_index = NULL;
        rv->aot_enter = NULL;
        dlclose(rv->aot_handle);
        rv->aot_handle = NULL;
    }
}
