/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#if !RV32_HAS(SYSTEM)
#error "Do not manage to build this file unless you enable system support."
#endif

#include "devices/plic.h"
#include "devices/uart.h"
#include "riscv_private.h"

#define R 1
#define W 0

/* MMIO definitions for Linux kernel emulation.
 * Only defined when ELF_LOADER is disabled, as kernel mode needs MMIO but ELF
 * test mode does not.
 */
#if !RV32_HAS(ELF_LOADER)

#define MMIO_R 1
#define MMIO_W 0

enum SUPPORTED_MMIO {
    MMIO_PLIC,
    MMIO_UART,
    MMIO_VIRTIOBLK,
    MMIO_VIRTIORNG,
#if RV32_HAS(VIRTIO_NET)
    MMIO_VIRTIONET,
#endif
#if RV32_HAS(VIRTIO_SND)
    MMIO_VIRTIOSND,
#endif
#if RV32_HAS(GOLDFISH_RTC)
    MMIO_RTC,
#endif /* RV32_HAS(GOLDFISH_RTC) */
};

/* clang-format off */
#define MMIO_OP(io, rw)                                                               \
    switch(io){                                                                       \
        case MMIO_PLIC:                                                               \
            IIF(rw)( /* read */                                                       \
                mmio_read_val = plic_read(PRIV(rv)->plic, addr & 0x3FFFFFF);          \
                plic_update_interrupts(PRIV(rv)->plic);                               \
                return mmio_read_val;                                                 \
                ,    /* write */                                                      \
                plic_write(PRIV(rv)->plic, addr & 0x3FFFFFF, val);                    \
                plic_update_interrupts(PRIV(rv)->plic);                               \
                return;                                                               \
            )                                                                         \
            break;                                                                    \
        case MMIO_UART:                                                               \
            IIF(rw)( /* read */                                                       \
                mmio_read_val = u8250_read(PRIV(rv)->uart, addr & 0xFFFFF);           \
                emu_update_uart_interrupts(rv);                                       \
                return mmio_read_val;                                                 \
                ,    /* write */                                                      \
                u8250_write(PRIV(rv)->uart, addr & 0xFFFFF, val);                     \
                emu_update_uart_interrupts(rv);                                       \
                return;                                                               \
            )                                                                         \
            break;                                                                    \
        case MMIO_VIRTIOBLK:                                                          \
            IIF(rw)( /* read */                                                       \
                mmio_read_val = virtio_blk_read(PRIV(rv)->vblk_curr, addr & 0xFFFFF); \
                emu_update_vblk_interrupts(rv);                                       \
                return mmio_read_val;                                                 \
                ,    /* write */                                                      \
                virtio_blk_write(PRIV(rv)->vblk_curr, addr & 0xFFFFF, val);           \
                emu_update_vblk_interrupts(rv);                                       \
                return;                                                               \
            )                                                                         \
            break;                                                                    \
        case MMIO_VIRTIORNG:                                                          \
            IIF(rw)(  /* read */                                                      \
                mmio_read_val = virtio_rng_read(PRIV(rv)->vrng, addr & 0xFFFFF);      \
                emu_update_vrng_interrupts(rv);                                       \
                return mmio_read_val;                                                 \
                ,     /* write */                                                     \
                virtio_rng_write(PRIV(rv)->vrng, addr & 0xFFFFF, val);                \
                emu_update_vrng_interrupts(rv);                                       \
                return;                                                               \
            )                                                                         \
            break;                                                                    \
        IIF(RV32_FEATURE_VIRTIO_NET)(                                                 \
        case MMIO_VIRTIONET:                                                          \
            IIF(rw)( /* read */                                                       \
                mmio_read_val = virtio_net_read(PRIV(rv)->vnet, addr & 0xFFFFF);      \
                emu_update_vnet_interrupts(rv);                                       \
                return mmio_read_val;                                                 \
                ,    /* write */                                                      \
                virtio_net_write(PRIV(rv)->vnet, addr & 0xFFFFF, val);                \
                emu_update_vnet_interrupts(rv);                                       \
                return;                                                               \
            )                                                                         \
            break;                                                                    \
        )                                                                             \
        IIF(RV32_FEATURE_VIRTIO_SND)(                                                 \
        case MMIO_VIRTIOSND:                                                          \
            IIF(rw)( /* read */                                                       \
                mmio_read_val = virtio_snd_read(PRIV(rv)->vsnd, addr & 0xFFFFF);      \
                emu_update_vsnd_interrupts(rv);                                       \
                return mmio_read_val;                                                 \
                ,    /* write */                                                      \
                virtio_snd_write(PRIV(rv)->vsnd, addr & 0xFFFFF, val);                \
                emu_update_vsnd_interrupts(rv);                                       \
                return;                                                               \
            )                                                                         \
            break;                                                                    \
        )                                                                             \
        IIF(RV32_FEATURE_GOLDFISH_RTC)(                                               \
        case MMIO_RTC:                                                                \
            IIF(rw)( /* read */                                                       \
                mmio_read_val = rtc_read(PRIV(rv)->rtc, addr & 0xFFFFF);              \
                emu_update_rtc_interrupts(rv);                                        \
                return mmio_read_val;                                                 \
                ,    /* write */                                                      \
                rtc_write(PRIV(rv)->rtc, addr & 0xFFFFF, val);                        \
                emu_update_rtc_interrupts(rv);                                        \
                return;                                                               \
            )                                                                         \
            break;                                                                    \
        ,)                                                                            \
        default:                                                                      \
            rv_log_error("unknown MMIO type %d\n", io);                               \
            break;                                                                    \
    }

#define MMIO_READ()                                                           \
    do {                                                                      \
        uint32_t mmio_read_val;                                               \
        if ((addr >> 28) == 0xF) { /* MMIO at 0xF_______ */                   \
            /* 256 regions of 1MiB */                                         \
            uint32_t hi = (addr >> 20) & MASK(8);                             \
            if (PRIV(rv)->vblk_cnt && hi >= PRIV(rv)->vblk_mmio_base_hi &&    \
                hi <= PRIV(rv)->vblk_mmio_max_hi) {                           \
                PRIV(rv)->vblk_curr =                                         \
                    PRIV(rv)->vblk[hi - PRIV(rv)->vblk_mmio_base_hi];         \
                MMIO_OP(MMIO_VIRTIOBLK, MMIO_R);                              \
            } else if (PRIV(rv)->vrng && hi == PRIV(rv)->vrng_mmio_base_hi) { \
                MMIO_OP(MMIO_VIRTIORNG, MMIO_R);                              \
            }                                                                         \
            IIF(RV32_FEATURE_VIRTIO_NET)(                                              \
            else if (PRIV(rv)->vnet && hi == PRIV(rv)->vnet_mmio_base_hi) {           \
                MMIO_OP(MMIO_VIRTIONET, MMIO_R);                                      \
            }                                                                         \
            )                                                                         \
            IIF(RV32_FEATURE_VIRTIO_SND)(                                     \
            else if (PRIV(rv)->vsnd &&                                       \
                     hi == PRIV(rv)->vsnd_mmio_base_hi) {                     \
                MMIO_OP(MMIO_VIRTIOSND, MMIO_R);                             \
            }                                                                 \
            )                                                                 \
            else {                                                                    \
                switch (hi) {                                                 \
                case 0x0:                                                     \
                case 0x2: /* PLIC (0 - 0x3F) */                               \
                    MMIO_OP(MMIO_PLIC, MMIO_R);                               \
                    break;                                                    \
                case 0x40: /* UART */                                         \
                    MMIO_OP(MMIO_UART, MMIO_R);                               \
                    break;                                                    \
                IIF(RV32_FEATURE_GOLDFISH_RTC)(                               \
                case 0x41 : /* RTC */                                         \
                    MMIO_OP(MMIO_RTC, MMIO_R);                                \
                    break;                                                    \
                ,)                                                            \
                default:                                                      \
                    __UNREACHABLE;                                            \
                    break;                                                    \
                }                                                             \
            }                                                                 \
        }                                                                     \
    } while (0)

#define MMIO_WRITE()                                                          \
    do {                                                                      \
        if ((addr >> 28) == 0xF) { /* MMIO at 0xF_______ */                   \
            /* 256 regions of 1MiB */                                         \
            uint32_t hi = (addr >> 20) & MASK(8);                             \
            if (PRIV(rv)->vblk_cnt && hi >= PRIV(rv)->vblk_mmio_base_hi &&    \
                hi <= PRIV(rv)->vblk_mmio_max_hi) {                           \
                PRIV(rv)->vblk_curr =                                         \
                    PRIV(rv)->vblk[hi - PRIV(rv)->vblk_mmio_base_hi];         \
                MMIO_OP(MMIO_VIRTIOBLK, MMIO_W);                              \
            } else if (PRIV(rv)->vrng && hi == PRIV(rv)->vrng_mmio_base_hi) { \
                MMIO_OP(MMIO_VIRTIORNG, MMIO_W);                              \
            }                                                                         \
            IIF(RV32_FEATURE_VIRTIO_NET)(                                              \
            else if (PRIV(rv)->vnet && hi == PRIV(rv)->vnet_mmio_base_hi) {           \
                MMIO_OP(MMIO_VIRTIONET, MMIO_W);                                      \
            }                                                                         \
            )                                                                         \
            IIF(RV32_FEATURE_VIRTIO_SND)(                                     \
            else if (PRIV(rv)->vsnd && hi == PRIV(rv)->vsnd_mmio_base_hi) {   \
                MMIO_OP(MMIO_VIRTIOSND, MMIO_W);                              \
            }                                                                 \
            )                                                                 \
            else {                                                                    \
                switch (hi) {                                                 \
                case 0x0:                                                     \
                case 0x2: /* PLIC (0 - 0x3F) */                               \
                    MMIO_OP(MMIO_PLIC, MMIO_W);                               \
                    break;                                                    \
                case 0x40: /* UART */                                         \
                    MMIO_OP(MMIO_UART, MMIO_W);                               \
                    break;                                                    \
                IIF(RV32_FEATURE_GOLDFISH_RTC)(                               \
                case 0x41 : /* RTC */                                         \
                    MMIO_OP(MMIO_RTC, MMIO_W);                                \
                    break;                                                    \
                ,)                                                            \
                default:                                                      \
                    __UNREACHABLE;                                            \
                    break;                                                    \
                }                                                             \
            }                                                                 \
        }                                                                     \
    } while (0)
/* clang-format on */

void emu_update_uart_interrupts(riscv_t *rv);
void emu_update_vblk_interrupts(riscv_t *rv);
void emu_update_vrng_interrupts(riscv_t *rv);
#if RV32_HAS(VIRTIO_NET)
void emu_update_vnet_interrupts(riscv_t *rv);
#endif
#if RV32_HAS(VIRTIO_SND)
void emu_update_vsnd_interrupts(riscv_t *rv);
#endif
#if RV32_HAS(GOLDFISH_RTC)
void emu_update_rtc_interrupts(riscv_t *rv);
#endif /* RV32_HAS(GOLDFISH_RTC) */

#endif /* !RV32_HAS(ELF_LOADER) */

/* Whether the trap handler that just ran inline resumed somewhere other than
 * the faulting instruction, as a kernel does to deliver a signal or to take
 * an exception-table fixup. The access must then be abandoned, not retried.
 */
#define CHECK_PENDING_SIGNAL(rv, signal_flag)              \
    do {                                                   \
        signal_flag = (rv->csr_sepc != rv->last_csr_sepc); \
    } while (0)

/*
 * Signal to RVOP macro that inline trap handling occurred.
 * When set, the instruction should return without advancing PC to allow retry.
 * Used both for Linux kernel signal handling (modifies SEPC) and ELF loader
 * mode inline trap handling (page fault resolved, instruction needs retry).
 */
extern bool need_handle_signal;

/* Walk through page tables and get the corresponding PTE by virtual address if
 * exists
 * @rv: RISC-V emulator
 * @addr: virtual address
 * @level: the level of which the PTE is located
 * @return: NULL if a not found or fault else the corresponding PTE
 */
uint32_t *mmu_walk(riscv_t *rv, const uint32_t addr, uint32_t *level);

/* Verify the PTE and generate corresponding faults if needed
 * @op: the operation
 * @rv: RISC-V emulator
 * @pte: to be verified pte
 * @addr: the corresponding virtual address to cause fault
 * @return: false if a any fault is generated which caused by violating the
 * access permission else true
 */
/* FIXME: handle access fault, addr out of range check */
#define MMU_FAULT_CHECK_DECL(op)                                            \
    bool mmu_##op##_fault_check(riscv_t *rv, uint32_t *pte, uint32_t vaddr, \
                                uint32_t access_bits);

MMU_FAULT_CHECK_DECL(ifetch);
MMU_FAULT_CHECK_DECL(read);
MMU_FAULT_CHECK_DECL(write);

/*
 * Translate virtual address to physical address with TLB caching.
 */
uint32_t mmu_translate(riscv_t *rv, uint32_t vaddr, bool rw);
uint32_t rv_mmu_ifetch(riscv_t *rv, uint32_t vaddr);
uint32_t mmu_ifetch_miss(riscv_t *rv, uint32_t vaddr);
extern bool need_retranslate;
extern bool need_handle_signal;

/* Instruction fetch through the iTLB, the body of rv_mmu_ifetch(). It is
 * inline here so the interpreter's fetches skip the call on an iTLB hit in
 * every build, not only with LTO; a miss walks the page table out of line.
 *
 * Do not call rv->io.mem_translate(): the basic block might be retranslated
 * while the corresponding PTE is NULL.
 */
/* Instruction fetch ignores MPRV. Data accesses may borrow MPP in M-mode. */
static inline uint32_t mmu_data_priv(const riscv_t *rv)
{
    return rv->priv_mode == RV_PRIV_M_MODE && (rv->csr_mstatus & MSTATUS_MPRV)
               ? (rv->csr_mstatus & MSTATUS_MPP) >> MSTATUS_MPP_SHIFT
               : rv->priv_mode;
}

static inline bool mmu_paging(const riscv_t *rv, uint32_t priv)
{
    return priv != RV_PRIV_M_MODE && (rv->csr_satp & (UINT32_C(1) << 31));
}

/* Shared walk/hit permission rules; a valid PTE may never execute in both U
 * and S mode, regardless of SUM. MXR broadens only loads, never stores.
 */
static inline bool mmu_perm_ok(const riscv_t *rv,
                               uint32_t perm,
                               uint32_t access)
{
    const uint32_t priv = access == PTE_X ? rv->priv_mode : mmu_data_priv(rv);
    if (priv == RV_PRIV_U_MODE && !(perm & PTE_U))
        return false;
    if (priv == RV_PRIV_S_MODE && (perm & PTE_U) &&
        (access == PTE_X || !(rv->csr_sstatus & SSTATUS_SUM)))
        return false;
    if (access == PTE_R && (rv->csr_sstatus & SSTATUS_MXR))
        access |= PTE_X;
    return (perm & access) != 0;
}

/* SUM matters only to U pages, so the S context ignores it: kernel pages keep
 * their stamps and RAM tags across the SUM toggles around each user copy. U
 * pages stamped in S mode are tracked instead, and parked when SUM clears.
 */
static inline uint32_t mmu_access_context_value(const riscv_t *rv)
{
    const uint32_t priv = mmu_data_priv(rv);
    const uint32_t mxr = !!(rv->csr_sstatus & SSTATUS_MXR);
    return (priv == RV_PRIV_M_MODE   ? 1
            : priv == RV_PRIV_U_MODE ? 2 + mxr
                                     : 4 + mxr)
           << 4;
}

/* A nonzero dTLB stamp no context produces: the entry stays cached for the
 * checked C lookup, but every inline fast path misses it.
 */
#define MMU_CONTEXT_PARKED (UINT32_C(0xf) << 4)

/* Raw R/W user pages have the same permission under both MXR values, and
 * SUM/MPRV cannot change U-mode access. Class zero lets privilege-keyed
 * native U blocks probe them without loading context on every access.
 * Execute-only reads retain a nonzero class and use the checked helper.
 */
static inline uint32_t mmu_ram_tag_context(const riscv_t *rv)
{
    return rv->priv_mode == RV_PRIV_U_MODE ? 0 : rv->mmu_access_context;
}

/* Context updates never flush translations. Nonzero dTLB stamps encode the
 * five effective U/S/M + MXR combinations in bits 4..7. Exact RAM tags
 * additionally use class zero for raw-R/W U-mode pages.
 */
void mmu_access_context_changed(riscv_t *rv);

static inline uint32_t mmu_ifetch_part(riscv_t *rv, uint32_t vaddr)
{
    if (!mmu_paging(rv, rv->priv_mode))
        return memory_ifetch(vaddr);
    if (need_retranslate)
        return 0;
    const uint32_t vpn = (vaddr >> RV_PG_SHIFT) | ITLB_EXEC_TAG |
                         (rv->priv_mode << ITLB_PRIV_SHIFT);
    const tlb_entry_t *entry = &rv->itlb[vpn & TLB_MASK];
    /* Tags distinguish individual 4 KiB VPNs even for superpages. */
    if (entry->vpn == vpn)
        return (vaddr & MASK(RV_PG_SHIFT)) == RV_PG_SIZE - 2
                   ? memory_ifetch_half(entry->ppn | (RV_PG_SIZE - 2))
                   : memory_ifetch(entry->ppn | (vaddr & MASK(RV_PG_SHIFT)));
    return mmu_ifetch_miss(rv, vaddr);
}
static inline uint32_t mmu_ifetch(riscv_t *rv, uint32_t vaddr)
{
    if (likely((vaddr & MASK(RV_PG_SHIFT)) != RV_PG_SIZE - 2) ||
        !mmu_paging(rv, rv->priv_mode))
        return mmu_ifetch_part(rv, vaddr);
    uint32_t low = mmu_ifetch_part(rv, vaddr);
    if ((low & 3) != 3)
        return low;
    uint32_t high = mmu_ifetch_part(rv, vaddr + 2);
    if (need_handle_signal || need_retranslate)
        return 0;
    return (low & UINT32_C(0xffff)) | (high << 16);
}
bool mmu_debug_translate(riscv_t *rv, uint32_t vaddr, uint32_t *paddr);
#if RV32_HAS(T2C)
/* Side-effect-free translation through the iTLB entry the last instruction
 * fetch from this page filled. Fails when there is none, e.g. after SFENCE.
 */
bool mmu_code_translate(riscv_t *rv, uint32_t vaddr, uint32_t *paddr);
#endif

/*
 * TLB management functions for SFENCE.VMA and SATP changes.
 */
void mmu_tlb_flush_all(riscv_t *rv);
void mmu_tlb_flush(riscv_t *rv, uint32_t vaddr);

/* Record that a block was decoded from [start, end), and return the address
 * space key it belongs to: the current SATP or, in JIT builds, SATP_SHARED for
 * S-mode code fetched only through global mappings.
 */
uint32_t mmu_note_code(riscv_t *rv, uint32_t start, uint32_t end);
#if RV32_HAS(JIT)
/* Compare the page-table entries recorded for decoded code with the current
 * ones; shared code is compared with the current address space's entries.
 * Call stale(rv, satp) once for each address space in which any of them
 * changed, and forget that space's records. With switched, check after an
 * address-space switch rather than a fence. A fence for one page passes its
 * vpn, since it orders only that page's leaf entry; others pass
 * CODE_MAP_ALL_PAGES. Return false, having called nothing, when the records
 * overflowed and every space must be retired.
 */
#define CODE_MAP_ALL_PAGES UINT32_MAX
bool mmu_check_code_maps(riscv_t *rv,
                         void (*stale)(riscv_t *, uint32_t),
                         bool switched,
                         uint32_t vpn);
/* Forget every record, once all decoded code is retired. */
void mmu_forget_code_maps(riscv_t *rv);
#endif
/* Whether vaddr lies in a 4 MiB region holding code fetched through a
 * superpage, where flushing one address may remap code on another page.
 */
bool mmu_code_superpage(const riscv_t *rv, uint32_t vaddr);
#if !RV32_HAS(JIT)
/* Whether flushing vaddr may remap code of an interpreter block. */
bool mmu_may_hold_code(const riscv_t *rv, uint32_t vaddr);
/* Forget all recorded code, once the interpreter dropped its blocks. */
void mmu_forget_code(riscv_t *rv);
#endif

#define get_ppn_and_offset()                                   \
    uint32_t ppn;                                              \
    uint32_t offset;                                           \
    do {                                                       \
        ppn = *pte >> (RV_PG_SHIFT - 2) << RV_PG_SHIFT;        \
        offset = level == 1 ? vaddr & MASK((RV_PG_SHIFT + 10)) \
                            : vaddr & MASK(RV_PG_SHIFT);       \
    } while (0)

uint8_t mmu_read_b(riscv_t *rv, const uint32_t vaddr);
uint16_t mmu_read_s(riscv_t *rv, const uint32_t vaddr);
uint32_t mmu_read_w(riscv_t *rv, const uint32_t vaddr);
void mmu_write_b(riscv_t *rv, const uint32_t vaddr, const uint8_t val);
void mmu_write_s(riscv_t *rv, const uint32_t vaddr, const uint16_t val);
void mmu_write_w(riscv_t *rv, const uint32_t vaddr, const uint32_t val);
