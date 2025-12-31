/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
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
    memset(rv->itlb, 0, sizeof(rv->itlb));
}

void mmu_tlb_flush(riscv_t *rv, uint32_t vaddr)
{
    uint32_t vpn = vaddr >> RV_PG_SHIFT;
    uint32_t idx = vpn & TLB_MASK;

    if (rv->dtlb[idx].valid && rv->dtlb[idx].vpn == vpn)
        rv->dtlb[idx].valid = 0;

    if (rv->itlb[idx].valid && rv->itlb[idx].vpn == vpn)
        rv->itlb[idx].valid = 0;
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
extern bool need_retranslate;
static uint32_t mmu_ifetch(riscv_t *rv, const uint32_t vaddr)
{
    /*
     * Do not call rv->io.mem_translate() because the basic block might be
     * retranslated and the corresponding PTE is NULL, get_ppn_and_offset()
     * cannot work on a NULL PTE.
     */

    if (!rv->csr_satp)
        return memory_ifetch(vaddr);

    uint32_t vpn = vaddr >> RV_PG_SHIFT;
    uint32_t idx = vpn & TLB_MASK;
    tlb_entry_t *entry = &rv->itlb[idx];

    /* TLB Hit */
    if (likely(entry->valid && entry->vpn == vpn)) {
        /* Check 1: User mode can only access U-page */
        if (rv->priv_mode == RV_PRIV_U_MODE && !(entry->perm & PTE_U))
            goto miss;

        /* Check 2: S-mode execution from U-page (fetch fault) */
        if (rv->priv_mode == RV_PRIV_S_MODE && (entry->perm & PTE_U))
            goto miss;

        /* Check 3: Executable permission */
        if (!(entry->perm & PTE_X))
            goto miss;

        return memory_ifetch((entry->ppn << RV_PG_SHIFT) |
                             (vaddr & MASK(RV_PG_SHIFT)));
    }

miss:;
    uint32_t level;
    pte_t *pte = mmu_walk(rv, vaddr, &level);
    bool ok = MMU_FAULT_CHECK(ifetch, rv, pte, vaddr, PTE_X);
    if (unlikely(!ok)) {
        need_retranslate = true;
        return 0;
    }

    if (need_retranslate)
        return 0;

    /* Populate TLB and update A bit per RISC-V Sv32 spec */
    if (likely(pte)) {
        /* Update A bit on instruction fetch */
        if (!(*pte & PTE_A))
            *pte |= PTE_A;

        entry->vpn = vpn;
        entry->perm = (uint8_t) (*pte & 0xFF);
        entry->valid = 1;
        entry->level = (uint8_t) level;

        if (level == 1) { /* Superpage */
            uint32_t pte_ppn = *pte >> 10;
            uint32_t page_offset = vpn & MASK(10);
            entry->ppn = pte_ppn | page_offset;
        } else {
            entry->ppn = *pte >> 10;
        }
    }

    get_ppn_and_offset();
    return memory_ifetch(ppn | offset);
}

uint32_t mmu_read_w(riscv_t *rv, const uint32_t vaddr)
{
    uint32_t addr = rv->io.mem_translate(rv, vaddr, R);

#if RV32_HAS(SYSTEM) && RV32_HAS(ELF_LOADER)
    if (need_retranslate)
        return 0;
#elif RV32_HAS(SYSTEM)
    if (need_handle_signal)
        return 0;
#endif

    if (GUEST_RAM_CONTAINS(PRIV(rv)->mem, addr, 4))
        return memory_read_w(addr);

#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
    MMIO_READ();
#endif

    __UNREACHABLE;
}

uint16_t mmu_read_s(riscv_t *rv, const uint32_t vaddr)
{
    uint32_t addr = rv->io.mem_translate(rv, vaddr, R);

#if RV32_HAS(SYSTEM) && RV32_HAS(ELF_LOADER)
    if (need_retranslate)
        return 0;
#elif RV32_HAS(SYSTEM)
    if (need_handle_signal)
        return 0;
#endif

    if (GUEST_RAM_CONTAINS(PRIV(rv)->mem, addr, 2))
        return memory_read_s(addr);

#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
    MMIO_READ();
#endif

    __UNREACHABLE;
}

uint8_t mmu_read_b(riscv_t *rv, const uint32_t vaddr)
{
    uint32_t addr = rv->io.mem_translate(rv, vaddr, R);

#if RV32_HAS(SYSTEM) && RV32_HAS(ELF_LOADER)
    if (need_retranslate)
        return 0;
#elif RV32_HAS(SYSTEM)
    if (need_handle_signal)
        return 0;
#endif

    if (GUEST_RAM_CONTAINS(PRIV(rv)->mem, addr, 1))
        return memory_read_b(addr);

#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
    MMIO_READ();
#endif

    __UNREACHABLE;
}

void mmu_write_w(riscv_t *rv, const uint32_t vaddr, const uint32_t val)
{
    uint32_t addr = rv->io.mem_translate(rv, vaddr, W);

#if RV32_HAS(SYSTEM) && RV32_HAS(ELF_LOADER)
    if (need_retranslate)
        return;
#elif RV32_HAS(SYSTEM)
    if (need_handle_signal)
        return;
#endif

    if (GUEST_RAM_CONTAINS(PRIV(rv)->mem, addr, 4)) {
        memory_write_w(addr, (uint8_t *) &val);
        return;
    }

#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
    MMIO_WRITE();
#endif

    __UNREACHABLE;
}

void mmu_write_s(riscv_t *rv, const uint32_t vaddr, const uint16_t val)
{
    uint32_t addr = rv->io.mem_translate(rv, vaddr, W);

#if RV32_HAS(SYSTEM) && RV32_HAS(ELF_LOADER)
    if (need_retranslate)
        return;
#elif RV32_HAS(SYSTEM)
    if (need_handle_signal)
        return;
#endif

    if (GUEST_RAM_CONTAINS(PRIV(rv)->mem, addr, 2)) {
        memory_write_s(addr, (uint8_t *) &val);
        return;
    }

#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
    MMIO_WRITE();
#endif

    __UNREACHABLE;
}

void mmu_write_b(riscv_t *rv, const uint32_t vaddr, const uint8_t val)
{
    uint32_t addr = rv->io.mem_translate(rv, vaddr, W);

#if RV32_HAS(SYSTEM) && RV32_HAS(ELF_LOADER)
    if (need_retranslate)
        return;
#elif RV32_HAS(SYSTEM)
    if (need_handle_signal)
        return;
#endif

    if (GUEST_RAM_CONTAINS(PRIV(rv)->mem, addr, 1)) {
        memory_write_b(addr, (uint8_t *) &val);
        return;
    }

#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
    MMIO_WRITE();
#endif

    __UNREACHABLE;
}

/*
 * TODO: dTLB can be introduced here to
 * cache the gVA to gPA tranlation.
 */
uint32_t mmu_translate(riscv_t *rv, uint32_t vaddr, bool rw)
{
    if (!rv->csr_satp)
        return vaddr;

    uint32_t vpn = vaddr >> RV_PG_SHIFT;
    uint32_t idx = vpn & TLB_MASK;
    tlb_entry_t *entry = &rv->dtlb[idx];

    if (likely(entry->valid && entry->vpn == vpn)) {
        /* Check 1: User mode access to non-U page */
        if (rv->priv_mode == RV_PRIV_U_MODE && !(entry->perm & PTE_U))
            goto miss;

        /* Check 2: Supervisor mode access to U page (SUM check) */
        if (rv->priv_mode == RV_PRIV_S_MODE && (entry->perm & PTE_U) &&
            !(rv->csr_sstatus & SSTATUS_SUM))
            goto miss;

        if (rw) { /* Read */
            /* Check 3: Read permission.
             * If MXR=0: Must be R.
             * If MXR=1: Must be R or X.
             */
            bool readable =
                (entry->perm & PTE_R) ||
                ((rv->csr_sstatus & SSTATUS_MXR) && (entry->perm & PTE_X));
            if (!readable)
                goto miss;
        } else { /* Write */
            /* Check 4: Write permission */
            if (!(entry->perm & PTE_W))
                goto miss;

            /* Update D bit on write hits if not already set */
            if (!entry->dirty) {
                vm_attr_t *attr = PRIV(rv);
                pte_t *pte_ptr =
                    (pte_t *) (attr->mem->mem_base + entry->pte_addr);
                if (!(*pte_ptr & PTE_D))
                    *pte_ptr |= PTE_D;
                entry->dirty = 1;
            }
        }

        return (entry->ppn << RV_PG_SHIFT) | (vaddr & MASK(RV_PG_SHIFT));
    }

miss:;
    uint32_t level;
    pte_t *pte = mmu_walk(rv, vaddr, &level);
    bool ok = rw ? MMU_FAULT_CHECK(read, rv, pte, vaddr, PTE_R)
                 : MMU_FAULT_CHECK(write, rv, pte, vaddr, PTE_W);
    if (unlikely(!ok)) {
#if RV32_HAS(ELF_LOADER)
        need_retranslate = true;
#endif
        return 0;
    }

    /* Populate TLB and update A/D bits per RISC-V Sv32 spec */
    if (likely(pte)) {
        vm_attr_t *attr = PRIV(rv);

        /* Update A bit on any access, D bit on writes */
        if (!(*pte & PTE_A))
            *pte |= PTE_A;
        if (!rw && !(*pte & PTE_D)) /* Write access */
            *pte |= PTE_D;

        entry->vpn = vpn;
        entry->perm = (uint8_t) (*pte & 0xFF);
        entry->valid = 1;
        entry->level = (uint8_t) level;
        entry->pte_addr =
            (uint32_t) ((uintptr_t) pte - (uintptr_t) attr->mem->mem_base);
        entry->dirty = (*pte & PTE_D) ? 1 : 0;

        if (level == 1) {
            uint32_t pte_ppn = *pte >> 10;
            uint32_t page_offset = vpn & MASK(10);
            entry->ppn = pte_ppn | page_offset;
        } else {
            entry->ppn = *pte >> 10;
        }
    }

    get_ppn_and_offset();
    return ppn | offset;
}

riscv_io_t mmu_io = {
    /* memory read interface */
    .mem_ifetch = mmu_ifetch,
    .mem_read_w = mmu_read_w,
    .mem_read_s = mmu_read_s,
    .mem_read_b = mmu_read_b,

    /* memory write interface */
    .mem_write_w = mmu_write_w,
    .mem_write_s = mmu_write_s,
    .mem_write_b = mmu_write_b,

    /* VA2PA handler */
    .mem_translate = mmu_translate,

    /* system services or essential routines */
    .on_ecall = ecall_handler,
    .on_ebreak = ebreak_handler,
    .on_memcpy = memcpy_handler,
    .on_memset = memset_handler,
    .on_trap = trap_handler,
};
