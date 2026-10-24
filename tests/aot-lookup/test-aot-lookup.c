/* Check that only immutable PC absence can bypass page validation. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "jit.h"
#include "riscv_private.h"
#include "system.h"

static bool mapped = true;
static unsigned translations;
static uint32_t mapped_page;

/* Page mappings are controlled explicitly; no guest faults are delivered. */
bool mmu_code_translate(riscv_t *rv UNUSED, uint32_t vaddr, uint32_t *paddr)
{
    translations++;
    *paddr = vaddr == mapped_page ? 0 : vaddr;
    return mapped;
}

static void target(riscv_t *rv UNUSED) {}

/* The dispatcher probes the PC, fetches the instruction, then binds. */
static void *lookup(riscv_t *rv)
{
    const struct aot_entry *first = t2c_aot_has_pc(rv);
    return first ? t2c_aot_lookup(rv, first) : NULL;
}

int main(void)
{
    static riscv_t rv;
    static uint8_t ram[RV_PG_SIZE];
    memory_t memory = {.mem_base = ram, .mem_size = sizeof(ram)};
    vm_attr_t attr = {.mem = &memory};
    rv.data = &attr;
    rv.ram_base = ram;
    rv.aot_hash_epoch = 1;
    block_t block = {.pc_start = 0, .pc_end = 2};
    uint64_t hash = t2c_aot_page_hash(&rv, &block);
    assert(hash);
    uint8_t digest[32];
    memcpy(digest, block.aot_page_digest, sizeof(digest));
    struct aot_entry entries[8] = {
        {.pc = 0, .hash = hash, .entry = target, .digest = digest}};
    rv.aot_index = entries;
    rv.aot_index_mask = ARRAY_SIZE(entries) - 1;

    /* PC zero is a valid entry, not an empty negative-cache tag. */
    assert(lookup(&rv) == (void *) target);
    rv.PC = 20; /* Collides with PC zero in the artifact hash table. */
    assert(!lookup(&rv));
    unsigned before = translations;
    for (unsigned i = 0; i < 200000; i++)
        assert(!lookup(&rv));
    assert(rv.aot_absent_hits == 200000 && translations == before);
    rv.csr_satp = 0x80000001;
    rv.aot_hash_epoch++;
    assert(!lookup(&rv));
    assert(rv.aot_absent_hits == 200001 && translations == before);

    /* Direct-mapped replacement must retain complete PC tags. */
    rv.PC += 2048;
    assert(!lookup(&rv));
    rv.PC = 20;
    assert(!lookup(&rv));
    assert(rv.aot_lookup_absent == 3);

    rv.PC = 0;
    assert(lookup(&rv) == (void *) target);
    digest[0] ^= 1;
    assert(!lookup(&rv));
    assert(!lookup(&rv));
    assert(rv.aot_lookup_mismatches == 2);
    digest[0] ^= 1;
    assert(lookup(&rv) == (void *) target);

    /* A temporary translation failure must not poison the PC cache. */
    mapped = false;
    assert(!lookup(&rv));
    assert(!lookup(&rv));
    assert(rv.aot_lookup_unmapped == 2);
    mapped = true;
    assert(lookup(&rv) == (void *) target);
    /* A two-byte instruction can end exactly at the RV32 address-space
     * boundary. A four-byte instruction there spans two virtual pages and
     * cannot belong to a single-page native graph, even though end wraps.
     */
    block.pc_start = RV_PG_SIZE - 2;
    block.pc_end = RV_PG_SIZE;
    assert(t2c_aot_page_hash(&rv, &block));
    block.pc_end = RV_PG_SIZE + 2;
    assert(!t2c_aot_page_hash(&rv, &block));
    mapped_page = UINT32_C(0xfffff000);
    block.pc_start = UINT32_C(0xfffffffe);
    block.pc_end = 0;
    assert(t2c_aot_page_hash(&rv, &block));
    block.pc_end = 2;
    assert(!t2c_aot_page_hash(&rv, &block));
    puts("AOT lookup absence, collisions, retry and page-bound checks passed");
    return 0;
}
