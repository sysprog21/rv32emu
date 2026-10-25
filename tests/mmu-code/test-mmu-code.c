/* Code fingerprint translation must follow fetch mappings without side effects
 * and must never fall back to a permission-blind page walk.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "system.h"

bool need_retranslate;

/* The iTLB tag an S-mode fetch from vpn hits. */
#define S_FETCH_TAG(vpn) \
    ((vpn) | ITLB_EXEC_TAG | (RV_PRIV_S_MODE << ITLB_PRIV_SHIFT))

int main(void)
{
    static riscv_t rv;
    static uint32_t ram[16384];
    memory_t memory = {.mem_base = (uint8_t *) ram, .mem_size = sizeof(ram)};
    vm_attr_t attr = {.mem = &memory, .mem_size = sizeof(ram)};
    rv.data = &attr;
    /* Kernel code: S-mode fetches use the supervisor iTLB tags seeded below. */
    rv.priv_mode = RV_PRIV_S_MODE;
    uint32_t physical;
    const uint32_t address = 0x00405024;
    assert(mmu_code_translate(&rv, address, &physical));
    assert(physical == address);

    /* Bare mode ignores any leftover tagged instruction entry. */
    rv.itlb[(address >> RV_PG_SHIFT) & TLB_MASK].vpn =
        S_FETCH_TAG(address >> RV_PG_SHIFT);
    rv.itlb[(address >> RV_PG_SHIFT) & TLB_MASK].ppn = 0x3000;
    assert(mmu_code_translate(&rv, address, &physical));
    assert(physical == address);
    mmu_tlb_flush_all(&rv);

    rv.csr_satp = 0x80000001;
    assert(!mmu_code_translate(&rv, address, &physical));
    uint32_t *root = ram + RV_PG_SIZE / 4;
    uint32_t *leaf = ram + 2 * RV_PG_SIZE / 4;
    root[1] = (2 << 10) | PTE_V;
    leaf[5] = (3 << 10) | PTE_V | PTE_R | PTE_X;
    const uint32_t original = leaf[5];
    /* A mapping no fetch has used yet is not proven executable. */
    assert(!mmu_code_translate(&rv, address, &physical));
    assert(leaf[5] == original);
    uint32_t vpn = address >> RV_PG_SHIFT;
    tlb_entry_t *entry = &rv.itlb[vpn & TLB_MASK];
    assert(!entry->valid); /* A failed lookup must not fill the TLB. */
    entry->vpn = S_FETCH_TAG(vpn);
    entry->ppn = 0x3000;
    entry->valid = 1;
    entry->level = TLB_PAGE_LEVEL_4K;
    leaf[5] = (4 << 10) | PTE_V | PTE_R | PTE_X;

    /* Until SFENCE, code follows the existing fetch translation. The debugger
     * retains its raw page-table view, independent of this optimization.
     */
    assert(mmu_code_translate(&rv, address, &physical));
    assert(physical == 0x3024);
    assert(mmu_debug_translate(&rv, address, &physical));
    assert(physical == 0x4024);
    need_retranslate = true;
    assert(!mmu_code_translate(&rv, address, &physical));
    need_retranslate = false;
    mmu_tlb_flush(&rv, address);
    assert(!entry->valid);
    assert(!mmu_code_translate(&rv, address, &physical));

    /* Superpage entries cache a single exact 4 KiB slice. */
    root[1] = PTE_V | PTE_R | PTE_X;
    entry->vpn = S_FETCH_TAG(vpn);
    entry->ppn = 0x5000;
    entry->valid = 1;
    entry->level = TLB_PAGE_LEVEL_SUPER;
    rv.tlb_super_slices[1] = 1;
    assert(mmu_code_translate(&rv, address, &physical));
    assert(physical == 0x5024);
    uint32_t alias = address + (TLB_MASK + 1) * RV_PG_SIZE;
    assert(!mmu_code_translate(&rv, alias, &physical));
    mmu_tlb_flush_all(&rv);
    assert(!entry->valid);

    /* A Sv32 physical address above 4 GiB must never wrap into low RAM. */
    root[1] = (2 << 10) | PTE_V;
    leaf[5] = (UINT32_C(1) << 30) | PTE_V | PTE_R | PTE_X;
    assert(!mmu_debug_translate(&rv, address, &physical));
    assert(!mmu_code_translate(&rv, address, &physical));
    assert(!(leaf[5] & (PTE_A | PTE_D)));
    rv.csr_satp = 0x803fffff;
    assert(!mmu_code_translate(&rv, address, &physical));
    puts("Code translation, iTLB invalidation, and side-effect checks passed");
    return 0;
}
