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
    MMIO_RTC,
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
        default:                                                                      \
            rv_log_error("unknown MMIO type %d\n", io);                               \
            break;                                                                    \
    }
/* clang-format on */

#define MMIO_READ()                                                        \
    do {                                                                   \
        uint32_t mmio_read_val;                                            \
        if ((addr >> 28) == 0xF) { /* MMIO at 0xF_______ */                \
            /* 256 regions of 1MiB */                                      \
            uint32_t hi = (addr >> 20) & MASK(8);                          \
            if (PRIV(rv)->vblk_cnt && hi >= PRIV(rv)->vblk_mmio_base_hi && \
                hi <= PRIV(rv)->vblk_mmio_max_hi) {                        \
                PRIV(rv)->vblk_curr =                                      \
                    PRIV(rv)->vblk[hi - PRIV(rv)->vblk_mmio_base_hi];      \
                MMIO_OP(MMIO_VIRTIOBLK, MMIO_R);                           \
            } else {                                                       \
                switch (hi) {                                              \
                case 0x0:                                                  \
                case 0x2: /* PLIC (0 - 0x3F) */                            \
                    MMIO_OP(MMIO_PLIC, MMIO_R);                            \
                    break;                                                 \
                case 0x40: /* UART */                                      \
                    MMIO_OP(MMIO_UART, MMIO_R);                            \
                    break;                                                 \
                case 0x41: /* RTC */                                       \
                    MMIO_OP(MMIO_RTC, MMIO_R);                             \
                    break;                                                 \
                default:                                                   \
                    __UNREACHABLE;                                         \
                    break;                                                 \
                }                                                          \
            }                                                              \
        }                                                                  \
    } while (0)

#define MMIO_WRITE()                                                       \
    do {                                                                   \
        if ((addr >> 28) == 0xF) { /* MMIO at 0xF_______ */                \
            /* 256 regions of 1MiB */                                      \
            uint32_t hi = (addr >> 20) & MASK(8);                          \
            if (PRIV(rv)->vblk_cnt && hi >= PRIV(rv)->vblk_mmio_base_hi && \
                hi <= PRIV(rv)->vblk_mmio_max_hi) {                        \
                PRIV(rv)->vblk_curr =                                      \
                    PRIV(rv)->vblk[hi - PRIV(rv)->vblk_mmio_base_hi];      \
                MMIO_OP(MMIO_VIRTIOBLK, MMIO_W);                           \
            } else {                                                       \
                switch (hi) {                                              \
                case 0x0:                                                  \
                case 0x2: /* PLIC (0 - 0x3F) */                            \
                    MMIO_OP(MMIO_PLIC, MMIO_W);                            \
                    break;                                                 \
                case 0x40: /* UART */                                      \
                    MMIO_OP(MMIO_UART, MMIO_W);                            \
                    break;                                                 \
                case 0x41: /* RTC */                                       \
                    MMIO_OP(MMIO_RTC, MMIO_W);                             \
                    break;                                                 \
                default:                                                   \
                    __UNREACHABLE;                                         \
                    break;                                                 \
                }                                                          \
            }                                                              \
        }                                                                  \
    } while (0)

void emu_update_uart_interrupts(riscv_t *rv);
void emu_update_vblk_interrupts(riscv_t *rv);
void emu_update_rtc_interrupts(riscv_t *rv);

/*
 * Linux kernel might create signal frame when returning from trap
 * handling, which modifies the SEPC CSR. Thus, the fault instruction
 * cannot always redo. For example, invalid memory access causes SIGSEGV.
 */
extern bool need_handle_signal;

#define CHECK_PENDING_SIGNAL(rv, signal_flag)              \
    do {                                                   \
        signal_flag = (rv->csr_sepc != rv->last_csr_sepc); \
    } while (0)

#endif

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
 * TODO: dTLB can be introduced here to
 * cache the gVA to gPA tranlation.
 */
uint32_t mmu_translate(riscv_t *rv, uint32_t vaddr, bool rw);

uint32_t *mmu_walk(riscv_t *rv, const uint32_t addr, uint32_t *level);

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
