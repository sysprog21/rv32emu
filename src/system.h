/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include "riscv_private.h"

/* PTE XWRV bit in order */
enum SV32_PTE_PERM {
    NEXT_PG_TBL = 0b0001,
    RO_PAGE = 0b0011,
    RW_PAGE = 0b0111,
    EO_PAGE = 0b1001,
    RX_PAGE = 0b1011,
    RWX_PAGE = 0b1111,
    RESRV_PAGE1 = 0b0101,
    RESRV_PAGE2 = 0b1101,
};

/*
 * The IO handler that operates when the Memory Management Unit (MMU)
 * is enabled during system emulation is responsible for managing
 * input/output operations. These callbacks are designed to implement
 * the riscv_io_t interface, ensuring compatibility and consistency to
 * the structure required by the interface. As a result, the riscv_io_t
 * interface can be reused.
 */
uint32_t mmu_ifetch(riscv_t *rv, const uint32_t addr);
uint32_t mmu_read_w(riscv_t *rv, const uint32_t addr);
uint16_t mmu_read_s(riscv_t *rv, const uint32_t addr);
uint8_t mmu_read_b(riscv_t *rv, const uint32_t addr);
void mmu_write_w(riscv_t *rv, const uint32_t addr, const uint32_t val);
void mmu_write_s(riscv_t *rv, const uint32_t addr, const uint16_t val);
void mmu_write_b(riscv_t *rv, const uint32_t addr, const uint8_t val);

/*
 * Trap might occurs during block emulation. For instance, page fault.
 * In order to handle trap, we have to escape from block and execute
 * registered trap handler. This trap_handler function helps to execute
 * the registered trap handler, PC by PC. Once the trap is handled,
 * resume the previous execution flow where cause the trap.
 *
 * Now, rv32emu supports misaligned access and page fault handling.
 */
void trap_handler(riscv_t *rv);
