/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define IRQ_UART_SHIFT 1
#define IRQ_UART_BIT (1 << IRQ_UART_SHIFT)

enum UART_REG {
    U8250_THR_RBR_DLL = 0,
    U8250_IER_DLH,
    U8250_IIR_FCR,
    U8250_LCR,
    U8250_MCR,
    U8250_LSR,
    U8250_MSR,
    U8250_SR,
};

typedef struct {
    uint8_t dll, dlh;                    /* divisor (ignored) */
    uint8_t lcr;                         /* UART config */
    uint8_t ier;                         /* interrupt config */
    uint8_t current_intr, pending_intrs; /* interrupt status */
    uint8_t mcr;       /* other output signals, loopback mode (ignored) */
    int in_fd, out_fd; /* I/O handling */
    bool in_ready;
} u8250_state_t;

/* update UART status */
void u8250_update_interrupts(u8250_state_t *uart);

/* poll UART status */
void u8250_check_ready(u8250_state_t *uart);

/* read a word from UART */
uint32_t u8250_read(u8250_state_t *uart, uint32_t addr);

/* write a word to UART */
void u8250_write(u8250_state_t *uart, uint32_t addr, uint32_t value);

/* create a UART instance */
u8250_state_t *u8250_new();

/* delete a UART instance */
void u8250_delete(u8250_state_t *uart);
