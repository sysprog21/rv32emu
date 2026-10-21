/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdio.h>
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

void mmu_tlb_flush_all(riscv_t *rv)
{
    memset(rv->dtlb, 0, sizeof(rv->dtlb));
#if RV32_HAS(JIT)
    memset(rv->jit_dtlb, 0xff, sizeof(rv->jit_dtlb));
#endif
    memset(rv->itlb, 0, sizeof(rv->itlb));
    memset(rv->tlb_super_slices, 0, sizeof(rv->tlb_super_slices));
}

/* The VPN of a TLB entry; iTLB tags carry ITLB_EXEC_TAG. */
static inline uint32_t tlb_entry_vpn(const tlb_entry_t *entry)
{
    return entry->vpn & ~ITLB_EXEC_TAG;
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
    entry->vpn &= ~ITLB_EXEC_TAG;
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

static void note_code_page(riscv_t *rv, uint32_t vpn)
{
#if !RV32_HAS(JIT)
    bit_set(rv->code_pages, code_page_slot(vpn));
#endif
    if (!rv->csr_satp)
        return;
    /* The fetch just filled the iTLB. Without an exact 4 KiB entry for this
     * page, assume the code came through a superpage.
     */
    const tlb_entry_t *entry = &rv->itlb[vpn & TLB_MASK];
    if (entry->valid && tlb_entry_vpn(entry) == vpn &&
        entry->level == TLB_PAGE_LEVEL_4K)
        return;
    bit_set(rv->code_superpages, vpn >> 10);
}

void mmu_note_code(riscv_t *rv, uint32_t start, uint32_t end)
{
    /* Visit each page of [start, end), which may wrap past the top of memory:
     * stop at the page of its last byte rather than compare page numbers.
     */
    const uint32_t last = (end - 1) >> RV_PG_SHIFT;
    for (uint32_t vpn = start >> RV_PG_SHIFT;;
         vpn = (vpn + 1) & MASK(32 - RV_PG_SHIFT)) {
        note_code_page(rv, vpn);
        if (vpn == last)
            break;
    }
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
    rv->jit_dtlb[idx].read_tag = ram && (perm & PTE_R) ? va_page : UINT32_MAX;
    rv->jit_dtlb[idx].write_tag =
        ram && (perm & PTE_W) && dirty ? va_page : UINT32_MAX;
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
        uint8_t needed = write ? PTE_W : PTE_R;
        if (!(entry->perm & needed)) {
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
#if RV32_HAS(JIT)
            jit_dtlb_fill(rv, vaddr, entry->ppn, entry->perm, true);
#endif
        }

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
    entry->valid = 1;
}

static inline void dtlb_populate(riscv_t *rv,
                                 uint32_t vaddr,
                                 pte_t *pte,
                                 uint32_t level)
{
    tlb_entry_t *entry = &rv->dtlb[(vaddr >> RV_PG_SHIFT) & TLB_MASK];
    tlb_fill(rv, entry, vaddr, pte, level);
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
        entry->vpn |= ITLB_EXEC_TAG;
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
        if (pte && (!(*pte & PTE_V))) {                                       \
            SET_CAUSE_AND_TVAL_THEN_TRAP(rv, scause, stval);                  \
            return false;                                                     \
        }                                                                     \
        if (!(pte && (*pte & access_bits))) {                                 \
            SET_CAUSE_AND_TVAL_THEN_TRAP(rv, scause, stval);                  \
            return false;                                                     \
        }                                                                     \
        /*                                                                    \
         * (1) When MXR=0, only loads from pages marked readable (R=1) will   \
         * succeed.                                                           \
         *                                                                    \
         * (2) When MXR=1, loads from pages marked either readable or         \
         * executable (R=1 or X=1) will succeed.                              \
         */                                                                   \
        if (pte && ((!(SSTATUS_MXR & rv->csr_sstatus) && !(*pte & PTE_R) &&   \
                     (access_bits == PTE_R)) ||                               \
                    ((SSTATUS_MXR & rv->csr_sstatus) &&                       \
                     !((*pte & PTE_R) | (*pte & PTE_X)) &&                    \
                     (access_bits == PTE_R)))) {                              \
            SET_CAUSE_AND_TVAL_THEN_TRAP(rv, scause, stval);                  \
            return false;                                                     \
        }                                                                     \
        /*                                                                    \
         * When SUM=0, S-mode memory accesses to pages that are accessible by \
         * U-mode will fault.                                                 \
         */                                                                   \
        if (pte && rv->priv_mode == RV_PRIV_S_MODE &&                         \
            !(SSTATUS_SUM & rv->csr_sstatus) && (*pte & PTE_U)) {             \
            SET_CAUSE_AND_TVAL_THEN_TRAP(rv, scause, stval);                  \
            return false;                                                     \
        }                                                                     \
        /* PTE not found, map it in handler */                                \
        if (!pte) {                                                           \
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
    /* TLB miss - do full page walk */
    uint32_t level;
    pte_t *pte = mmu_walk(rv, vaddr, &level);
    bool ok = MMU_FAULT_CHECK(ifetch, rv, pte, vaddr, PTE_X);
    if (unlikely(!ok)) {
        CHECK_PENDING_SIGNAL(rv, need_handle_signal);

        /* Retry only if the handler returned to the access and let the hart run
         * on; a halted hart would otherwise retry forever.
         */
        if (need_handle_signal || rv_has_halted(rv))
            return 0;
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
    return memory_ifetch(ppn | offset);
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
    if (!rv->csr_satp) {
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
    if (!rv->csr_satp) {
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
        if (need_handle_signal || rv_has_halted(rv))
            return 0;
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
