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

#if !RV32_HAS(ELF_LOADER)

#define MMIO_R 1
#define MMIO_W 0

enum SUPPORTED_MMIO {
    MMIO_PLIC,
    MMIO_UART,
};

/* clang-format off */
#define MMIO_OP(io, rw)                                                      \
    switch(io){                                                              \
        case MMIO_PLIC:                                                      \
            IIF(rw)( /* read */                                              \
                mmio_read_val = plic_read(PRIV(rv)->plic, addr & 0x3FFFFFF); \
                plic_update_interrupts(PRIV(rv)->plic);                      \
                return mmio_read_val;                                        \
                ,    /* write */                                             \
                plic_write(PRIV(rv)->plic, addr & 0x3FFFFFF, val);           \
                plic_update_interrupts(PRIV(rv)->plic);                      \
                return;                                                      \
            )                                                                \
            break;                                                           \
        case MMIO_UART:                                                      \
            IIF(rw)( /* read */                                              \
                mmio_read_val = u8250_read(PRIV(rv)->uart, addr & 0xFFFFF);  \
                emu_update_uart_interrupts(rv);                              \
                return mmio_read_val;                                        \
                ,    /* write */                                             \
                u8250_write(PRIV(rv)->uart, addr & 0xFFFFF, val);            \
                emu_update_uart_interrupts(rv);                              \
                return;                                                      \
            )                                                                \
            break;                                                           \
        default:                                                             \
            fprintf(stderr, "unknown MMIO type %d\n", io);                   \
            break;                                                           \
    }
/* clang-format on */

#define MMIO_READ()                                         \
    do {                                                    \
        uint32_t mmio_read_val;                             \
        if ((addr >> 28) == 0xF) { /* MMIO at 0xF_______ */ \
            /* 256 regions of 1MiB */                       \
            switch ((addr >> 20) & MASK(8)) {               \
            case 0x0:                                       \
            case 0x2: /* PLIC (0 - 0x3F) */                 \
                MMIO_OP(MMIO_PLIC, MMIO_R);                 \
                break;                                      \
            case 0x40: /* UART */                           \
                MMIO_OP(MMIO_UART, MMIO_R);                 \
                break;                                      \
            default:                                        \
                __UNREACHABLE;                              \
                break;                                      \
            }                                               \
        }                                                   \
    } while (0)

#define MMIO_WRITE()                                        \
    do {                                                    \
        if ((addr >> 28) == 0xF) { /* MMIO at 0xF_______ */ \
            /* 256 regions of 1MiB */                       \
            switch ((addr >> 20) & MASK(8)) {               \
            case 0x0:                                       \
            case 0x2: /* PLIC (0 - 0x3F) */                 \
                MMIO_OP(MMIO_PLIC, MMIO_W);                 \
                break;                                      \
            case 0x40: /* UART */                           \
                MMIO_OP(MMIO_UART, MMIO_W);                 \
                break;                                      \
            default:                                        \
                __UNREACHABLE;                              \
                break;                                      \
            }                                               \
        }                                                   \
    } while (0)

void emu_update_uart_interrupts(riscv_t *rv);
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
#define MMU_FAULT_CHECK_DECL(op)                                           \
    bool mmu_##op##_fault_check(riscv_t *rv, uint32_t *pte, uint32_t addr, \
                                uint32_t access_bits);

MMU_FAULT_CHECK_DECL(ifetch);
MMU_FAULT_CHECK_DECL(read);
MMU_FAULT_CHECK_DECL(write);

uint32_t *mmu_walk(riscv_t *rv, const uint32_t addr, uint32_t *level);

#define get_ppn_and_offset()                                  \
    uint32_t ppn;                                             \
    uint32_t offset;                                          \
    do {                                                      \
        ppn = *pte >> (RV_PG_SHIFT - 2) << RV_PG_SHIFT;       \
        offset = level == 1 ? addr & MASK((RV_PG_SHIFT + 10)) \
                            : addr & MASK(RV_PG_SHIFT);       \
    } while (0)
