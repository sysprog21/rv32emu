/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system.h"

#if !RV32_HAS(ELF_LOADER)
void emu_update_uart_interrupts(riscv_t *rv)
{
    vm_attr_t *attr = PRIV(rv);
    u8250_update_interrupts(attr->uart);
    if (attr->uart->pending_intrs)
        attr->plic->active |= IRQ_UART_BIT;
    else
        attr->plic->active &= ~IRQ_UART_BIT;
    plic_update_interrupts(attr->plic);
}

void emu_update_vblk_interrupts(riscv_t *rv)
{
    vm_attr_t *attr = PRIV(rv);
    for (int i = 0; i < attr->vblk_cnt; i++) {
        if (attr->vblk[i]->interrupt_status)
            attr->plic->active |= IRQ_VBLK_BIT(attr->vblk_irq_base, i);
        else
            attr->plic->active &= ~IRQ_VBLK_BIT(attr->vblk_irq_base, i);
        plic_update_interrupts(attr->plic);
    }
}

void emu_update_vrng_interrupts(riscv_t *rv)
{
    vm_attr_t *attr = PRIV(rv);

    if (!attr->vrng)
        return;

    if (attr->vrng->interrupt_status)
        attr->plic->active |= IRQ_VRNG_BIT(attr->vrng_irq);
    else
        attr->plic->active &= ~IRQ_VRNG_BIT(attr->vrng_irq);

    plic_update_interrupts(attr->plic);
}

#if RV32_HAS(VIRTIO_NET)
void emu_update_vnet_interrupts(riscv_t *rv)
{
    vm_attr_t *attr = PRIV(rv);

    if (!attr->vnet)
        return;

    if (attr->vnet->interrupt_status)
        attr->plic->active |= IRQ_VNET_BIT(attr->vnet_irq);
    else
        attr->plic->active &= ~IRQ_VNET_BIT(attr->vnet_irq);

    plic_update_interrupts(attr->plic);
}
#endif

#if RV32_HAS(VIRTIO_SND)
void emu_update_vsnd_interrupts(riscv_t *rv)
{
    vm_attr_t *attr = PRIV(rv);

    if (!attr->vsnd)
        return;

    /*
     * TX completion runs asynchronously in the sound completion thread.
     * Read the interrupt status atomically before propagating it to the PLIC.
     */
    uint32_t interrupt_status =
        __atomic_load_n(&attr->vsnd->interrupt_status, __ATOMIC_ACQUIRE);

    if (interrupt_status)
        attr->plic->active |= IRQ_VSND_BIT(attr->vsnd_irq);
    else
        attr->plic->active &= ~IRQ_VSND_BIT(attr->vsnd_irq);

    plic_update_interrupts(attr->plic);
}
#endif

#if RV32_HAS(GOLDFISH_RTC)
void emu_update_rtc_interrupts(riscv_t *rv)
{
    vm_attr_t *attr = PRIV(rv);
    if (attr->rtc->interrupt_status)
        attr->plic->active |= IRQ_RTC_BIT;
    else
        attr->plic->active &= ~IRQ_RTC_BIT;
    plic_update_interrupts(attr->plic);
}
#endif /* RV32_HAS(GOLDFISH_RTC) */
#endif

static bool ppn_is_valid(riscv_t *rv, uint32_t ppn)
{
    vm_attr_t *attr = PRIV(rv);
    const uint32_t nr_pg_max = attr->mem_size / RV_PG_SIZE;
    return ppn < nr_pg_max;
}

/* Whether S-mode data accesses may currently reach U pages. */
static inline bool mmu_sum_window(const riscv_t *rv)
{
    return mmu_data_priv(rv) == RV_PRIV_S_MODE &&
           (rv->csr_sstatus & SSTATUS_SUM);
}

/* Record a dTLB slot stamped for a U page in S mode; see sum_user_slots. */
static inline void mmu_note_stamp(riscv_t *rv, uint32_t idx, uint8_t perm)
{
    if ((perm & PTE_U) && mmu_data_priv(rv) == RV_PRIV_S_MODE)
        rv->sum_user_slots |= UINT64_C(1) << idx;
}

void mmu_access_context_changed(riscv_t *rv)
{
    const uint32_t context = mmu_access_context_value(rv);
    if (rv->mmu_access_context != context) {
        rv->mmu_access_context = context;
        T1C_COUNT(tlb_ctx_changes, 1);
    }
    /* The S stamps of U pages were valid only while SUM was set. Park them:
     * the C lookup rechecks permission live, and refreshes the stamp.
     */
    if (rv->sum_user_slots && !mmu_sum_window(rv)) {
        _Static_assert(TLB_SIZE <= 64, "sum_user_slots holds one bit per slot");
        for (uint32_t idx = 0; idx < TLB_SIZE; idx++) {
            if (!(rv->sum_user_slots >> idx & 1))
                continue;
            if (rv->dtlb[idx].valid)
                rv->dtlb[idx].valid = MMU_CONTEXT_PARKED;
#if RV32_HAS(JIT)
            rv->jit_dtlb[idx].read_tag = UINT32_MAX;
            rv->jit_dtlb[idx].write_tag = UINT32_MAX;
#endif
        }
        rv->sum_user_slots = 0;
    }
}

void mmu_tlb_flush_all(riscv_t *rv)
{
    rv->sum_user_slots = 0;
    mmu_access_context_changed(rv);
    memset(rv->dtlb, 0, sizeof(rv->dtlb));
#if RV32_HAS(JIT)
    /* Zero is a valid U-mode RAM tag for page zero, never an empty slot. */
    memset(rv->jit_dtlb, 0xff, sizeof(rv->jit_dtlb));
#endif
    memset(rv->itlb, 0, sizeof(rv->itlb));
    memset(rv->tlb_super_slices, 0, sizeof(rv->tlb_super_slices));
}

/* The VPN of a TLB entry; iTLB tags carry ITLB_EXEC_TAG. */
static inline uint32_t tlb_entry_vpn(const tlb_entry_t *entry)
{
    return entry->vpn & ITLB_VPN_MASK;
}

/* The 4 MiB region of a TLB entry. */
static inline uint32_t tlb_entry_region(const tlb_entry_t *entry)
{
    return tlb_entry_vpn(entry) >> 10;
}

/* Invalidate a valid entry. An iTLB hit compares the tagged VPN alone, so the
 * tag goes with valid; dTLB VPNs never carry it.
 */
static inline void tlb_invalidate(riscv_t *rv, tlb_entry_t *entry)
{
    if (entry->level == TLB_PAGE_LEVEL_SUPER)
        rv->tlb_super_slices[tlb_entry_region(entry)]--;
    entry->valid = 0;
    entry->vpn &= ITLB_VPN_MASK;
}

/* Invalidate a dTLB slot together with the RAM tags derived from it. */
static inline void dtlb_invalidate(riscv_t *rv, uint32_t idx)
{
    tlb_invalidate(rv, &rv->dtlb[idx]);
#if RV32_HAS(JIT)
    rv->jit_dtlb[idx].read_tag = UINT32_MAX;
    rv->jit_dtlb[idx].write_tag = UINT32_MAX;
#endif
}

static inline bool tlb_entry_in_superpage(const tlb_entry_t *entry,
                                          uint32_t vpn1)
{
    return entry->valid && entry->level == TLB_PAGE_LEVEL_SUPER &&
           tlb_entry_region(entry) == vpn1;
}

void mmu_tlb_flush(riscv_t *rv, uint32_t vaddr)
{
    uint32_t vpn = vaddr >> RV_PG_SHIFT;
    uint32_t idx = vpn & TLB_MASK;

    if (rv->dtlb[idx].valid && rv->dtlb[idx].vpn == vpn)
        dtlb_invalidate(rv, idx);

    if (rv->itlb[idx].valid && tlb_entry_vpn(&rv->itlb[idx]) == vpn)
        tlb_invalidate(rv, &rv->itlb[idx]);

    /* The leaf PTE for vaddr may be a 4 MiB superpage, which both TLBs cache
     * as separate 4 KiB slices. Flushing vaddr orders updates to that leaf,
     * so drop every cached slice of the superpage that contains it.
     */
    const uint32_t vpn1 = vpn >> 10;
    for (int i = 0; i < TLB_SIZE && rv->tlb_super_slices[vpn1]; i++) {
        if (tlb_entry_in_superpage(&rv->dtlb[i], vpn1))
            dtlb_invalidate(rv, i);
        if (tlb_entry_in_superpage(&rv->itlb[i], vpn1))
            tlb_invalidate(rv, &rv->itlb[i]);
    }
}

static inline void bit_set(uint32_t *map, uint32_t i)
{
    map[i / 32] |= UINT32_C(1) << (i % 32);
}

static inline bool bit_test(const uint32_t *map, uint32_t i)
{
    return map[i / 32] & (UINT32_C(1) << (i % 32));
}

#if !RV32_HAS(JIT)
static inline uint32_t code_page_slot(uint32_t vpn)
{
    return (vpn ^ (vpn >> 13)) & (CODE_PAGE_FILTER_BITS - 1);
}
#endif

#if RV32_HAS(JIT)
/* A page of decoded code and the page-table entries that mapped it: physical
 * offsets and values of the root entry and, below a non-leaf one, the leaf.
 * Equal entries mean the same translation and fetch permission, so code
 * decoded through them stays valid across SFENCE.VMA; changed contents of the
 * page itself are for FENCE.I to retire.
 */
struct code_map {
    uint32_t satp, vpn;
    uint32_t root, root_pte;
    uint32_t leaf, leaf_pte;
};
#define CODE_MAP_CAPACITY 8192
_Static_assert(CODE_MAP_CAPACITY <= UINT16_MAX, "slots hold record indexes");
/* The records are followed by a hashed index of the latest one per page and
 * space, then by chains of every record per page, for page fences: each
 * bucket heads a chain through the records' next links, as index + 1.
 */
#define CODE_MAP_SLOT_BITS 13
#define CODE_MAP_SLOTS (1U << CODE_MAP_SLOT_BITS)
#define CODE_MAP_PAGE_BUCKETS 4096
#define CODE_MAP_SIZE                                               \
    (CODE_MAP_CAPACITY * sizeof(struct code_map) +                  \
     (CODE_MAP_SLOTS + CODE_MAP_PAGE_BUCKETS + CODE_MAP_CAPACITY) * \
         sizeof(uint16_t))
HASH_FUNC_IMPL(code_map_hash, CODE_MAP_SLOT_BITS, CODE_MAP_SLOTS)
/* rv->code_space_epochs has one slot per hash of an address space. */
HASH_FUNC_IMPL(code_space_hash, 6, 64)

static inline uint16_t *code_map_slot(riscv_t *rv, uint32_t satp, uint32_t vpn)
{
    uint16_t *slots = (uint16_t *) (rv->code_maps + CODE_MAP_CAPACITY);
    return &slots[code_map_hash((rv_hash_key_t) satp << 32 | vpn)];
}

static inline uint16_t *code_map_page_heads(riscv_t *rv)
{
    return (uint16_t *) (rv->code_maps + CODE_MAP_CAPACITY) + CODE_MAP_SLOTS;
}

static inline uint16_t *code_map_page_next(riscv_t *rv)
{
    return code_map_page_heads(rv) + CODE_MAP_PAGE_BUCKETS;
}

static inline void code_map_page_link(riscv_t *rv, uint32_t idx)
{
    uint16_t *head = &code_map_page_heads(
        rv)[rv->code_maps[idx].vpn % CODE_MAP_PAGE_BUCKETS];
    code_map_page_next(rv)[idx] = *head;
    *head = idx + 1;
}

/* Relink the chains after records moved. */
static void code_map_page_relink(riscv_t *rv, uint32_t n)
{
    memset(code_map_page_heads(rv), 0,
           CODE_MAP_PAGE_BUCKETS * sizeof(uint16_t));
    for (uint32_t i = 0; i < n; i++)
        code_map_page_link(rv, i);
}
#define CODE_MAP_NO_LEAF UINT32_MAX
/* Accessed and dirty updates do not change a translation. */
#define CODE_MAP_PTE_MASK (~(uint32_t) (PTE_A | PTE_D))
/* A masked entry never has the accessed bit, so this one never matches. */
#define CODE_MAP_STALE UINT32_MAX

static uint32_t code_map_pte(const riscv_t *rv, uint32_t offset)
{
    return *(const uint32_t *) (PRIV(rv)->mem->mem_base + offset) &
           CODE_MAP_PTE_MASK;
}

/* The page table at ppn, or NULL outside guest RAM. */
static const uint32_t *code_map_table(const riscv_t *rv, uint32_t ppn)
{
    return ppn_is_valid((riscv_t *) rv, ppn)
               ? (const uint32_t *) (PRIV(rv)->mem->mem_base +
                                     (ppn << RV_PG_SHIFT))
               : NULL;
}

/* The root entry mapping vpn in the current address space, or NULL. */
static const uint32_t *code_map_root_entry(const riscv_t *rv, uint32_t vpn)
{
    if (!mmu_paging(rv, RV_PRIV_S_MODE))
        return NULL;
    const uint32_t *root = code_map_table(rv, rv->csr_satp & MASK(22));
    return root ? root + (vpn >> 10) : NULL;
}

/* The offset of a record's root entry, or CODE_MAP_NO_LEAF when it has none
 * now. A shared record names the entry of whichever space is current.
 */
static uint32_t code_map_root(const riscv_t *rv, const struct code_map *map)
{
    if (map->satp != SATP_SHARED)
        return map->root;
    const uint32_t *pte = code_map_root_entry(rv, map->vpn);
    return pte ? (uint32_t) ((const uint8_t *) pte - PRIV(rv)->mem->mem_base)
               : CODE_MAP_NO_LEAF;
}

/* Whether vpn is mapped globally in the current address space: a global
 * root entry covers every mapping below it.
 */
static bool code_page_global(const riscv_t *rv, uint32_t vpn)
{
    const uint32_t *pte = code_map_root_entry(rv, vpn);
    if (!pte || !(*pte & PTE_V))
        return false;
    if ((*pte & PTE_G) || (*pte & (PTE_R | PTE_W | PTE_X)))
        return *pte & PTE_G;
    const uint32_t *table = code_map_table(rv, *pte >> (RV_PG_SHIFT - 2));
    return table && (table[vpn & MASK(10)] & PTE_G);
}

/* Whether the entries mapping the record's page still hold what they did. */
static bool code_map_current(const riscv_t *rv, const struct code_map *map)
{
    const uint32_t root = code_map_root(rv, map);
    return root != CODE_MAP_NO_LEAF &&
           code_map_pte(rv, root) == map->root_pte &&
           (map->leaf == CODE_MAP_NO_LEAF ||
            code_map_pte(rv, map->leaf) == map->leaf_pte);
}

/* Record the entries mapping vpn for the address space key satp. */
static void code_map_record(riscv_t *rv, uint32_t vpn, uint32_t satp)
{
    if (rv->code_maps_overflow)
        return;
    if (!rv->code_maps) {
        rv->code_maps = calloc(1, CODE_MAP_SIZE);
        if (!rv->code_maps) {
            rv->code_maps_overflow = true;
            return;
        }
    }
    /* Each page is decoded again after every eviction of its blocks, so find
     * its record through the index. A slot names the latest record hashed
     * there; one that another page took merely misses, and the page is
     * recorded again. A record whose entries have changed since does not
     * describe this decode: were it kept alone, a later fence that restored
     * its entries would pass the check and keep code from the other mapping.
     * Record the page again then, unless the record already fails its check.
     */
    const uint32_t n = rv->n_code_maps;
    uint16_t *slot = code_map_slot(rv, satp, vpn);
    if (*slot < n && rv->code_maps[*slot].vpn == vpn &&
        rv->code_maps[*slot].satp == satp &&
        (rv->code_maps[*slot].root_pte == CODE_MAP_STALE ||
         code_map_current(rv, &rv->code_maps[*slot])))
        return;
    if (n == CODE_MAP_CAPACITY) {
        rv->code_maps_overflow = true;
        return;
    }

    const uint8_t *base = PRIV(rv)->mem->mem_base;
    const uint32_t *pte = code_map_root_entry(rv, vpn);
    if (!pte) {
        rv->code_maps_overflow = true;
        return;
    }
    struct code_map *map = &rv->code_maps[n];
    map->satp = satp;
    map->vpn = vpn;
    map->root = (const uint8_t *) pte - base;
    map->root_pte = *pte & CODE_MAP_PTE_MASK;
    map->leaf = CODE_MAP_NO_LEAF;
    map->leaf_pte = 0;
    if ((*pte & (PTE_V | PTE_R | PTE_W | PTE_X)) == PTE_V) {
        const uint32_t *table = code_map_table(rv, *pte >> (RV_PG_SHIFT - 2));
        if (!table) {
            rv->code_maps_overflow = true;
            return;
        }
        pte = table + (vpn & MASK(10));
        map->leaf = (const uint8_t *) pte - base;
        map->leaf_pte = *pte & CODE_MAP_PTE_MASK;
    }
    /* Until a fence, the fetch may have used an older translation than the
     * page tables now hold. Unless the iTLB entry it used agrees with them,
     * make the record fail its first check, retiring the code then.
     */
    const uint32_t leaf_pte =
        map->leaf == CODE_MAP_NO_LEAF ? map->root_pte : map->leaf_pte;
    uint32_t ppn = leaf_pte >> (RV_PG_SHIFT - 2) << RV_PG_SHIFT;
    /* A misaligned superpage faults, as mmu_walk() decides, however the
     * slice bits combine with its PPN.
     */
    bool aligned = true;
    if (map->leaf == CODE_MAP_NO_LEAF) {
        aligned = !(ppn & (MASK(10) << RV_PG_SHIFT));
        ppn |= (vpn & MASK(10)) << RV_PG_SHIFT;
    }
    const tlb_entry_t *entry = &rv->itlb[vpn & TLB_MASK];
    if (!(leaf_pte & PTE_V) || !aligned || !entry->valid ||
        tlb_entry_vpn(entry) != vpn || entry->ppn != ppn ||
        entry->perm != (leaf_pte & (PTE_R | PTE_W | PTE_X | PTE_U)))
        map->root_pte = CODE_MAP_STALE;
    rv->n_code_maps = n + 1;
    *slot = n;
    code_map_page_link(rv, n);
}

/* Drop the records of space from the first n, and return how many remain;
 * *checked becomes how many of those came before index i.
 */
static uint32_t code_maps_drop_space(riscv_t *rv,
                                     uint32_t space,
                                     uint32_t n,
                                     uint32_t i,
                                     uint32_t *checked)
{
    struct code_map *maps = rv->code_maps;
    uint32_t kept = 0, before = 0;
    for (uint32_t j = 0; j < n; j++) {
        if (maps[j].satp == space)
            continue;
        if (j < i)
            before++;
        maps[kept] = maps[j];
        *code_map_slot(rv, maps[kept].satp, maps[kept].vpn) = kept;
        kept++;
    }
    code_map_page_relink(rv, kept);
    *checked = before;
    return kept;
}

bool mmu_check_code_maps(riscv_t *rv,
                         void (*stale)(riscv_t *, uint32_t),
                         bool switched,
                         uint32_t vpn)
{
    if (rv->code_maps_overflow)
        return false;
    /* Only the current address space and shared code can run before the next
     * switch, so a fence checks those, and a switch checks the space entered
     * if a fence came since its last check. A slot collision only forces a
     * check. A fence checks every space when records run short, retiring
     * those of exited processes, whose page tables were reused.
     */
    /* After a wrap, an old slot could look current; forget them all. */
    if (!switched && !++rv->code_fence_epoch) {
        memset(rv->code_space_epochs, 0, sizeof(rv->code_space_epochs));
        rv->code_fence_epoch = 1;
    }
    const bool check_all =
        !switched && rv->n_code_maps > CODE_MAP_CAPACITY / 4 * 3;
    const uint32_t satp = rv->csr_satp;
    __typeof__(rv->code_space_epochs[0]) *seen =
        &rv->code_space_epochs[code_space_hash(satp)];
    const bool check_current =
        !switched || seen->satp != satp || seen->epoch != rv->code_fence_epoch;
    seen->satp = satp;
    seen->epoch = rv->code_fence_epoch;
    struct code_map *maps = rv->code_maps;
    uint32_t n = rv->n_code_maps;
    /* A page fence, outside superpage regions holding code, follows that
     * page's chain instead of visiting every record.
     */
    if (!maps)
        return true; /* nothing was recorded, so nothing can be stale */
    if (vpn != CODE_MAP_ALL_PAGES && !check_all &&
        !bit_test(rv->code_superpages, vpn >> 10)) {
    rescan:
        for (uint32_t e = code_map_page_heads(rv)[vpn % CODE_MAP_PAGE_BUCKETS];
             e; e = code_map_page_next(rv)[e - 1]) {
            const struct code_map *map = &maps[e - 1];
            if (map->vpn != vpn ||
                (map->satp != SATP_SHARED && map->satp != satp) ||
                code_map_current(rv, map))
                continue;
            const uint32_t space = map->satp;
            uint32_t checked;
            stale(rv, space);
            n = code_maps_drop_space(rv, space, n, 0, &checked);
            goto rescan;
        }
        rv->n_code_maps = n;
        return true;
    }
    for (uint32_t i = 0; i < n;) {
        const struct code_map *map = &maps[i];
        if (!check_all && map->satp != SATP_SHARED &&
            (!check_current || map->satp != satp)) {
            i++;
            continue;
        }
        /* A page fence can change that page's leaf, which for a superpage is
         * the root entry of its whole region.
         */
        if (!check_all && vpn != CODE_MAP_ALL_PAGES && map->vpn != vpn &&
            (map->leaf != CODE_MAP_NO_LEAF || map->vpn >> 10 != vpn >> 10)) {
            i++;
            continue;
        }
        if (code_map_current(rv, map)) {
            i++;
            continue;
        }
        /* Retire the whole space, and drop all its records, including the
         * ones already checked; resume after the survivors among those.
         */
        const uint32_t space = map->satp;
        stale(rv, space);
        n = code_maps_drop_space(rv, space, n, i, &i);
    }
    rv->n_code_maps = n;
    return true;
}

void mmu_forget_code_maps(riscv_t *rv)
{
    rv->n_code_maps = 0;
    rv->code_maps_overflow = false;
    if (rv->code_maps)
        memset(code_map_page_heads(rv), 0,
               CODE_MAP_PAGE_BUCKETS * sizeof(uint16_t));
    memset(rv->code_space_epochs, 0, sizeof(rv->code_space_epochs));
}
#endif

static void note_code_page(riscv_t *rv, uint32_t vpn, uint32_t satp UNUSED)
{
#if !RV32_HAS(JIT)
    bit_set(rv->code_pages, code_page_slot(vpn));
#endif
    if (!mmu_paging(rv, rv->priv_mode))
        return;
#if RV32_HAS(JIT)
    code_map_record(rv, vpn, satp);
#endif
    /* The fetch just filled the iTLB. Without an exact 4 KiB entry for this
     * page, assume the code came through a superpage.
     */
    const tlb_entry_t *entry = &rv->itlb[vpn & TLB_MASK];
    if (entry->valid && tlb_entry_vpn(entry) == vpn &&
        entry->level == TLB_PAGE_LEVEL_4K)
        return;
    bit_set(rv->code_superpages, vpn >> 10);
}

uint32_t mmu_note_code(riscv_t *rv, uint32_t start, uint32_t end)
{
    /* Visit each page of [start, end), which may wrap past the top of memory:
     * stop at the page of its last byte rather than compare page numbers.
     */
    const uint32_t first = start >> RV_PG_SHIFT;
    const uint32_t last = (end - 1) >> RV_PG_SHIFT;
    uint32_t satp = rv->csr_satp;
#if RV32_HAS(JIT)
    /* Kernel code reached only through global mappings is shared by every
     * address space, so that each process does not decode and compile the
     * kernel's code paths again. T2C builds keep per-space code for now:
     * shared kernel code reaches tier 2 sooner, and the extra LLVM work
     * measured slower Linux boots there.
     */
    if (!RV32_HAS(T2C) && rv->priv_mode == RV_PRIV_S_MODE &&
        mmu_paging(rv, rv->priv_mode)) {
        bool global = true;
        for (uint32_t vpn = first;; vpn = (vpn + 1) & MASK(32 - RV_PG_SHIFT)) {
            global = global && code_page_global(rv, vpn);
            if (vpn == last)
                break;
        }
        if (global)
            satp = SATP_SHARED;
    }
#endif
    for (uint32_t vpn = first;; vpn = (vpn + 1) & MASK(32 - RV_PG_SHIFT)) {
        note_code_page(rv, vpn, satp);
        if (vpn == last)
            break;
    }
    return satp;
}

bool mmu_code_superpage(const riscv_t *rv, uint32_t vaddr)
{
    return bit_test(rv->code_superpages, vaddr >> (RV_PG_SHIFT + 10));
}

#if !RV32_HAS(JIT)
bool mmu_may_hold_code(const riscv_t *rv, uint32_t vaddr)
{
    return bit_test(rv->code_pages, code_page_slot(vaddr >> RV_PG_SHIFT)) ||
           mmu_code_superpage(rv, vaddr);
}

void mmu_forget_code(riscv_t *rv)
{
    memset(rv->code_pages, 0, sizeof(rv->code_pages));
    memset(rv->code_superpages, 0, sizeof(rv->code_superpages));
}
#endif

#if RV32_HAS(JIT)
static inline bool mmu_io_is_default(const riscv_t *rv)
{
    return rv->io.mem_translate == mmu_translate &&
           rv->io.mem_read_w == mmu_read_w && rv->io.mem_read_s == mmu_read_s &&
           rv->io.mem_read_b == mmu_read_b &&
           rv->io.mem_write_w == mmu_write_w &&
           rv->io.mem_write_s == mmu_write_s &&
           rv->io.mem_write_b == mmu_write_b;
}

static inline void jit_dtlb_fill(riscv_t *rv,
                                 uint32_t vaddr,
                                 uint32_t paddr,
                                 uint8_t perm,
                                 bool dirty)
{
    const uint32_t idx = (vaddr >> RV_PG_SHIFT) & TLB_MASK;
    const uint32_t va_page = vaddr & ~(RV_PG_SIZE - 1);
    const uint32_t pa_page = paddr & ~(RV_PG_SIZE - 1);
    memory_t *mem = PRIV(rv)->mem;
    /* A tag hit bypasses the I/O callbacks, so replaced ones get no tags. */
    const bool ram =
        GUEST_RAM_CONTAINS(mem, pa_page, RV_PG_SIZE) && mmu_io_is_default(rv);
    const uint32_t context = mmu_ram_tag_context(rv);
    /* MXR-dependent X-only reads must never match the class-zero U probe. */
    const uint32_t read_context =
        perm & PTE_R ? context : rv->mmu_access_context;
    rv->jit_dtlb[idx].read_tag = ram && (!mmu_paging(rv, mmu_data_priv(rv)) ||
                                         mmu_perm_ok(rv, perm, PTE_R))
                                     ? va_page | read_context
                                     : UINT32_MAX;
    rv->jit_dtlb[idx].write_tag = ram &&
                                          (!mmu_paging(rv, mmu_data_priv(rv)) ||
                                           mmu_perm_ok(rv, perm, PTE_W)) &&
                                          dirty
                                      ? va_page | context
                                      : UINT32_MAX;
    rv->jit_dtlb[idx].addend = (uintptr_t) mem->mem_base + pa_page - va_page;
}
#endif

/* TLB lookup for data accesses (read/write).
 * Returns physical address if TLB hit, 0 if miss.
 * Updates A/D bits in PTE on successful lookup.
 */
static inline uint32_t dtlb_lookup(riscv_t *rv,
                                   uint32_t vaddr,
                                   bool write,
                                   bool *hit)
{
    uint32_t vpn = vaddr >> RV_PG_SHIFT;
    uint32_t idx = vpn & TLB_MASK;
    tlb_entry_t *entry = &rv->dtlb[idx];

    if (entry->valid && entry->vpn == vpn) {
        /* Check permissions */
        if (!mmu_perm_ok(rv, entry->perm, write ? PTE_W : PTE_R)) {
            *hit = false;
            return 0;
        }

        /* Handle dirty bit for writes */
        if (write && !entry->dirty) {
            /* Update PTE dirty bit in memory */
            vm_attr_t *attr = PRIV(rv);
            pte_t *pte = (pte_t *) (attr->mem->mem_base + entry->pte_addr);
            *pte |= PTE_D;
            entry->dirty = 1;
        }
        entry->valid = rv->mmu_access_context;
        mmu_note_stamp(rv, idx, entry->perm);
#if RV32_HAS(JIT)
        jit_dtlb_fill(rv, vaddr, entry->ppn, entry->perm, entry->dirty);
#endif
        *hit = true;
        /* Tags distinguish individual 4 KiB VPNs even for superpages. */
        return entry->ppn | (vaddr & MASK(RV_PG_SHIFT));
    }

    *hit = false;
    return 0;
}

/* Fill a TLB entry after a successful page walk. */
static inline void tlb_fill(riscv_t *rv,
                            tlb_entry_t *entry,
                            uint32_t vaddr,
                            pte_t *pte,
                            uint32_t level)
{
    if (entry->valid)
        tlb_invalidate(rv, entry);
    entry->vpn = vaddr >> RV_PG_SHIFT;
    /* Store page-aligned physical address base (PPN extracted from PTE bits
     * [31:10], shifted left by 12) */
    entry->ppn = *pte >> (RV_PG_SHIFT - 2) << RV_PG_SHIFT;
    /* Cache the translated 4 KiB slice of a superpage. A hit still requires
     * its exact VPN, so the remaining address offset is always 12 bits.
     */
    if (level == TLB_PAGE_LEVEL_SUPER) {
        entry->ppn |= vaddr & (MASK(RV_PG_SHIFT + 10) & ~MASK(RV_PG_SHIFT));
        rv->tlb_super_slices[vaddr >> (RV_PG_SHIFT + 10)]++;
    }
    entry->pte_addr = (uint8_t *) pte - PRIV(rv)->mem->mem_base;
    entry->perm = *pte & (PTE_R | PTE_W | PTE_X | PTE_U);
    entry->dirty = (*pte & PTE_D) ? 1 : 0;
    entry->level = level;
    entry->valid = rv->mmu_access_context;
}

static inline void dtlb_populate(riscv_t *rv,
                                 uint32_t vaddr,
                                 pte_t *pte,
                                 uint32_t level)
{
    const uint32_t idx = (vaddr >> RV_PG_SHIFT) & TLB_MASK;
    tlb_entry_t *entry = &rv->dtlb[idx];
    tlb_fill(rv, entry, vaddr, pte, level);
    mmu_note_stamp(rv, idx, entry->perm);
#if RV32_HAS(JIT)
    jit_dtlb_fill(rv, vaddr, entry->ppn, entry->perm, entry->dirty);
#endif
}

static inline void itlb_populate(riscv_t *rv,
                                 uint32_t vaddr,
                                 pte_t *pte,
                                 uint32_t level)
{
    tlb_entry_t *entry = &rv->itlb[(vaddr >> RV_PG_SHIFT) & TLB_MASK];
    tlb_fill(rv, entry, vaddr, pte, level);
    if (entry->perm & PTE_X)
        entry->vpn |= ITLB_EXEC_TAG | (rv->priv_mode << ITLB_PRIV_SHIFT);
}

#define PAGE_TABLE(ppn)                                               \
    ppn_is_valid(rv, ppn)                                             \
        ? (uint32_t *) (attr->mem->mem_base + (ppn << (RV_PG_SHIFT))) \
        : NULL

/* Walk through page tables and get the corresponding PTE by virtual address if
 * exists
 * @rv: RISC-V emulator
 * @vaddr: virtual address
 * @level: the level of which the PTE is located
 * @return: NULL if a not found or fault else the corresponding PTE
 */
pte_t *mmu_walk(riscv_t *rv, const uint32_t vaddr, uint32_t *level)
{
    vm_attr_t *attr = PRIV(rv);
    uint32_t ppn = rv->csr_satp & MASK(22);

    /* root page table */
    uint32_t *page_table = PAGE_TABLE(ppn);
    if (!page_table)
        return NULL;

    for (int i = 1; i >= 0; i--) {
        *level = 2 - i;
        uint32_t vpn =
            (vaddr >> RV_PG_SHIFT >> (i * (RV_PG_SHIFT - 2))) & MASK(10);
        pte_t *pte = page_table + vpn;

        uint8_t XWRV_bit = (*pte & MASK(4));
        switch (XWRV_bit) {
        case NEXT_PG_TBL: /* next level of the page table */
            ppn = (*pte >> (RV_PG_SHIFT - 2));
            page_table = PAGE_TABLE(ppn);
            if (!page_table)
                return NULL;
            break;
        case RO_PAGE:
        case RW_PAGE:
        case EO_PAGE:
        case RX_PAGE:
        case RWX_PAGE:
            ppn = (*pte >> (RV_PG_SHIFT - 2));
            if (*level == 1 &&
                unlikely(ppn & MASK(10))) /* misaligned superpage */
                return NULL;
            return pte; /* leaf PTE */
        case RESRV_PAGE1:
        case RESRV_PAGE2:
        default:
            return NULL;
        }
    }

    return NULL;
}

/* Verify the PTE and generate corresponding faults if needed
 * @op: the operation
 * @rv: RISC-V emulator
 * @pte: to be verified pte
 * @vaddr: the corresponding virtual address to cause fault
 * @return: false if a any fault is generated which caused by violating the
 * access permission else true
 */
/* FIXME: handle access fault, addr out of range check */
#define MMU_FAULT_CHECK(op, rv, pte, vaddr, access_bits) \
    mmu_##op##_fault_check(rv, pte, vaddr, access_bits)
#define MMU_FAULT_CHECK_IMPL(op, pgfault)                                     \
    bool mmu_##op##_fault_check(riscv_t *rv, pte_t *pte, uint32_t vaddr,      \
                                uint32_t access_bits)                         \
    {                                                                         \
        uint32_t scause;                                                      \
        uint32_t stval = vaddr;                                               \
        switch (access_bits) {                                                \
        case PTE_R:                                                           \
            scause = PAGEFAULT_LOAD;                                          \
            break;                                                            \
        case PTE_W:                                                           \
            scause = PAGEFAULT_STORE;                                         \
            break;                                                            \
        case PTE_X:                                                           \
            scause = PAGEFAULT_INSN;                                          \
            break;                                                            \
        default:                                                              \
            __UNREACHABLE;                                                    \
            break;                                                            \
        }                                                                     \
        if (!pte || !(*pte & PTE_V) || !mmu_perm_ok(rv, *pte, access_bits)) { \
            SET_CAUSE_AND_TVAL_THEN_TRAP(rv, scause, stval);                  \
            return false;                                                     \
        }                                                                     \
        /* valid PTE */                                                       \
        return true;                                                          \
    }

MMU_FAULT_CHECK_IMPL(ifetch, pagefault_insn)
MMU_FAULT_CHECK_IMPL(read, pagefault_load)
MMU_FAULT_CHECK_IMPL(write, pagefault_store)

/* The IO handler that operates when the Memory Management Unit (MMU)
 * is enabled during system emulation is responsible for managing
 * input/output operations. These callbacks are designed to implement
 * the riscv_io_t interface, ensuring compatibility and consistency to
 * the structure required by the interface. As a result, the riscv_io_t
 * interface can be reused.
 *
 * The IO handlers include:
 * - mmu_ifetch
 * - mmu_read_w
 * - mmu_read_s
 * - mmu_read_b
 * - mmu_write_w
 * - mmu_write_s
 * - mmu_write_b
 */
uint32_t __attribute__((noinline)) mmu_ifetch_miss(riscv_t *rv, uint32_t vaddr)
{
    const uint32_t access_priv = rv->priv_mode;
    const uint32_t access_satp = rv->csr_satp;
    /* TLB miss - do full page walk */
    uint32_t level;
    pte_t *pte = mmu_walk(rv, vaddr, &level);
    bool ok = MMU_FAULT_CHECK(ifetch, rv, pte, vaddr, PTE_X);
    if (unlikely(!ok)) {
        CHECK_PENDING_SIGNAL(rv, need_handle_signal);

        /* Retry only if the handler returned to the access and let the hart run
         * on; a halted hart would otherwise retry forever.
         */
        if (need_handle_signal || rv_has_halted(rv) ||
            rv->priv_mode != access_priv || rv->csr_satp != access_satp) {
            need_handle_signal = true;
            return 0;
        }
        /* Retry walk after trap handler has set up the page */
        pte = mmu_walk(rv, vaddr, &level);
        /* Re-validate permissions after retry */
        if (!pte || !MMU_FAULT_CHECK(ifetch, rv, pte, vaddr, PTE_X)) {
            need_retranslate = true;
            /* Also set need_handle_signal so RVOP macro returns for retry */
            need_handle_signal = true;
            return 0;
        }
    }

    /* Update Accessed bit per RISC-V Sv32 spec */
    if (!(*pte & PTE_A))
        *pte |= PTE_A;

    /* Populate iTLB for future accesses */
    itlb_populate(rv, vaddr, pte, level);

    get_ppn_and_offset();
    return (vaddr & MASK(RV_PG_SHIFT)) == RV_PG_SIZE - 2
               ? memory_ifetch_half(ppn | offset)
               : memory_ifetch(ppn | offset);
}

uint32_t rv_mmu_ifetch(riscv_t *rv, uint32_t vaddr)
{
    return mmu_ifetch(rv, vaddr);
}

uint32_t mmu_read_w(riscv_t *rv, const uint32_t vaddr)
{
    uint32_t addr = rv->io.mem_translate(rv, vaddr, R);

    if (need_handle_signal)
        return 0;

    if (GUEST_RAM_CONTAINS(PRIV(rv)->mem, addr, 4))
        return memory_read_w(addr);

#if RV32_HAS(SYSTEM_MMIO)
    MMIO_READ();
#endif

    __UNREACHABLE;
}

uint16_t mmu_read_s(riscv_t *rv, const uint32_t vaddr)
{
    uint32_t addr = rv->io.mem_translate(rv, vaddr, R);

    if (need_handle_signal)
        return 0;

    if (GUEST_RAM_CONTAINS(PRIV(rv)->mem, addr, 2))
        return memory_read_s(addr);

#if RV32_HAS(SYSTEM_MMIO)
    MMIO_READ();
#endif

    __UNREACHABLE;
}

uint8_t mmu_read_b(riscv_t *rv, const uint32_t vaddr)
{
    uint32_t addr = rv->io.mem_translate(rv, vaddr, R);

    if (need_handle_signal)
        return 0;

    if (GUEST_RAM_CONTAINS(PRIV(rv)->mem, addr, 1))
        return memory_read_b(addr);

#if RV32_HAS(SYSTEM_MMIO)
    MMIO_READ();
#endif

    __UNREACHABLE;
}

void mmu_write_w(riscv_t *rv, const uint32_t vaddr, const uint32_t val)
{
    uint32_t addr = rv->io.mem_translate(rv, vaddr, W);

    if (need_handle_signal)
        return;

    if (GUEST_RAM_CONTAINS(PRIV(rv)->mem, addr, 4)) {
        memory_write_w(addr, (uint8_t *) &val);
        return;
    }

#if RV32_HAS(SYSTEM_MMIO)
    MMIO_WRITE();
#endif

    __UNREACHABLE;
}

void mmu_write_s(riscv_t *rv, const uint32_t vaddr, const uint16_t val)
{
    uint32_t addr = rv->io.mem_translate(rv, vaddr, W);

    if (need_handle_signal)
        return;

    if (GUEST_RAM_CONTAINS(PRIV(rv)->mem, addr, 2)) {
        memory_write_s(addr, (uint8_t *) &val);
        return;
    }

#if RV32_HAS(SYSTEM_MMIO)
    MMIO_WRITE();
#endif

    __UNREACHABLE;
}

void mmu_write_b(riscv_t *rv, const uint32_t vaddr, const uint8_t val)
{
    uint32_t addr = rv->io.mem_translate(rv, vaddr, W);

    if (need_handle_signal)
        return;

    if (GUEST_RAM_CONTAINS(PRIV(rv)->mem, addr, 1)) {
        memory_write_b(addr, (uint8_t *) &val);
        return;
    }

#if RV32_HAS(SYSTEM_MMIO)
    MMIO_WRITE();
#endif

    __UNREACHABLE;
}

/* Translate @vaddr for a debugger. It walks the page tables as a data access
 * does, but never raises a fault, updates the A/D bits or fills a TLB, so the
 * guest cannot tell it happened.
 *
 * Returns false when the address is unmapped.
 */
bool mmu_debug_translate(riscv_t *rv, uint32_t vaddr, uint32_t *paddr)
{
    if (!mmu_paging(rv, mmu_data_priv(rv))) {
        *paddr = vaddr;
        return true;
    }

    uint32_t level;
    pte_t *pte = mmu_walk(rv, vaddr, &level);
    if (!pte)
        return false;

    /* A Sv32 PTE can map a physical address above 4 GiB, so compute it in 64
     * bits rather than let it wrap onto unrelated low memory.
     */
    const uint64_t ppn = (uint64_t) (*pte >> (RV_PG_SHIFT - 2)) << RV_PG_SHIFT;
    const uint32_t offset =
        level == 1 ? vaddr & MASK(RV_PG_SHIFT + 10) : vaddr & MASK(RV_PG_SHIFT);
    const uint64_t translated = ppn | offset;
    if (translated > UINT32_MAX)
        return false;
    *paddr = (uint32_t) translated;
    return true;
}

uint32_t mmu_translate(riscv_t *rv, uint32_t vaddr, bool rw)
{
    assert(rv->mmu_access_context == mmu_access_context_value(rv));
    const uint32_t access_priv = rv->priv_mode;
    const uint32_t access_satp = rv->csr_satp;
    const uint32_t access_context = rv->mmu_access_context;
    if (!mmu_paging(rv, mmu_data_priv(rv))) {
#if RV32_HAS(JIT)
        jit_dtlb_fill(rv, vaddr, vaddr, PTE_R | PTE_W, true);
#endif
        return vaddr;
    }

    /* Try dTLB first for fast path */
    bool hit;
    uint32_t paddr = dtlb_lookup(rv, vaddr, !rw, &hit);
    if (hit)
        return paddr;

    /* TLB miss - do full page walk */
    uint32_t level;
    pte_t *pte = mmu_walk(rv, vaddr, &level);
    bool ok = rw ? MMU_FAULT_CHECK(read, rv, pte, vaddr, PTE_R)
                 : MMU_FAULT_CHECK(write, rv, pte, vaddr, PTE_W);
    if (unlikely(!ok)) {
        CHECK_PENDING_SIGNAL(rv, need_handle_signal);

        /* Retry only if the handler returned to the access and let the hart run
         * on; a halted hart would otherwise retry forever.
         */
        if (need_handle_signal || rv_has_halted(rv) ||
            rv->priv_mode != access_priv ||
            rv->mmu_access_context != access_context ||
            rv->csr_satp != access_satp) {
            need_handle_signal = true;
            return 0;
        }
        /* Retry walk after trap handler has set up the page */
        pte = mmu_walk(rv, vaddr, &level);
        /* Re-validate permissions after retry */
        ok = rw ? MMU_FAULT_CHECK(read, rv, pte, vaddr, PTE_R)
                : MMU_FAULT_CHECK(write, rv, pte, vaddr, PTE_W);
        if (!pte || !ok) {
            need_handle_signal = true;
            return 0;
        }
    }

    /* Update A/D bits per RISC-V Sv32 spec */
    if (!(*pte & PTE_A))
        *pte |= PTE_A;
    if (!rw && !(*pte & PTE_D)) /* Write access needs Dirty bit */
        *pte |= PTE_D;

    /* Populate dTLB for future accesses */
    dtlb_populate(rv, vaddr, pte, level);

    get_ppn_and_offset();
    return ppn | offset;
}

riscv_io_t mmu_io = {
    /* memory read interface */
    .mem_ifetch = rv_mmu_ifetch,
    .mem_read_w = mmu_read_w,
    .mem_read_s = mmu_read_s,
    .mem_read_b = mmu_read_b,

    /* memory write interface */
    .mem_write_w = mmu_write_w,
    .mem_write_s = mmu_write_s,
    .mem_write_b = mmu_write_b,

    /* VA2PA handler */
    .mem_translate = mmu_translate,

    /* MMU memory access functions for T2C runtime binding */
    .mmu_read_w = mmu_read_w,
    .mmu_read_s = mmu_read_s,
    .mmu_read_b = mmu_read_b,
    .mmu_write_w = mmu_write_w,
    .mmu_write_s = mmu_write_s,
    .mmu_write_b = mmu_write_b,

    /* system services or essential routines */
    .on_ecall = ecall_handler,
    .on_ebreak = ebreak_handler,
    .on_memcpy = memcpy_handler,
    .on_memset = memset_handler,
    .on_trap = trap_handler,
};
