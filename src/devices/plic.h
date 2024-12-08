/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdint.h>

enum PLIC_REG {
    INTR_PENDING = 0x400,
    INTR_ENABLE = 0x800,
    INTR_PRIORITY = 0x80000,
    INTR_CLAIM_OR_COMPLETE = 0x80001,
};

/* PLIC */
typedef struct {
    uint32_t masked;
    uint32_t ip;
    uint32_t ie;
    /* state of input interrupt lines (level-triggered), set by environment */
    uint32_t active;
    /* RISC-V instance to receive PLIC interrupt */
    void *rv;
} plic_t;

/* update PLIC status */
void plic_update_interrupts(plic_t *plic);

/* read a word from PLIC */
uint32_t plic_read(plic_t *plic, const uint32_t addr);

/* write a word to PLIC */
void plic_write(plic_t *plic, const uint32_t addr, uint32_t value);

/* create a PLIC instance */
plic_t *plic_new();
