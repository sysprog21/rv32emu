/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "uart.h"

/* Emulate 8250 (plain, without loopback mode support) */

#define U8250_INTR_THRE 1

void u8250_update_interrupts(u8250_state_t *uart)
{
    /* Some interrupts are level-generated. */
    /* TODO: does it also generate an LSR change interrupt? */
    if (uart->in_ready)
        uart->pending_intrs |= 1;
    else
        uart->pending_intrs &= ~1;

    /* Prevent generating any disabled interrupts in the first place */
    uart->pending_intrs &= uart->ier;

    /* Update current interrupt (higher bits -> more priority) */
    if (uart->pending_intrs)
        uart->current_intr = ilog2(uart->pending_intrs);
}

void u8250_check_ready(u8250_state_t *uart)
{
    if (uart->in_ready)
        return;

    struct pollfd pfd = {uart->in_fd, POLLIN, 0};
    poll(&pfd, 1, 0);
    if (pfd.revents & POLLIN)
        uart->in_ready = true;
}

static void u8250_handle_out(u8250_state_t *uart, uint8_t value)
{
    if (write(uart->out_fd, &value, 1) < 1)
        fprintf(stderr, "failed to write UART output: %s\n", strerror(errno));
}

static uint8_t u8250_handle_in(u8250_state_t *uart)
{
    uint8_t value = 0;
    u8250_check_ready(uart);
    if (!uart->in_ready)
        return value;

    if (read(uart->in_fd, &value, 1) < 0)
        fprintf(stderr, "failed to read UART input: %s\n", strerror(errno));
    uart->in_ready = false;
    u8250_check_ready(uart);

    if (value == 1) {           /* start of heading (Ctrl-a) */
        if (getchar() == 120) { /* keyboard x */
            printf("\n");       /* end emulator with newline */
            exit(0);
        }
    }

    return value;
}

uint32_t u8250_read(u8250_state_t *uart, uint32_t addr)
{
    uint8_t ret = 0;

    switch (addr) {
    case U8250_THR_RBR_DLL:
        if (uart->lcr & (1 << 7)) /* DLAB */
            return uart->dll;
        return u8250_handle_in(uart);
    case U8250_IER_DLH:
        if (uart->lcr & (1 << 7)) /* DLAB */
            return uart->dlh;
        return uart->ier;
    case U8250_IIR_FCR:
        ret = (uart->current_intr << 1) | (uart->pending_intrs ? 0 : 1);
        if (uart->current_intr == U8250_INTR_THRE)
            uart->pending_intrs &= ~(1 << uart->current_intr);
        return ret;
    case U8250_LCR:
        return uart->lcr;
    case U8250_MCR:
        return uart->mcr;
        break;
    case U8250_LSR:
        /* LSR = no error, TX done & ready */
        return (0x60 | (uint8_t) uart->in_ready);
    case U8250_MSR:
        /* MSR = carrier detect, no ring, data ready, clear to send. */
        return 0xb0;
        /* no scratch register, so we should be detected as a plain 8250. */
    default:
        break;
    }

    return (uint32_t) (int8_t) ret;
}

void u8250_write(u8250_state_t *uart, uint32_t addr, uint32_t value)
{
    switch (addr) {
    case U8250_THR_RBR_DLL:
        if (uart->lcr & (1 << 7)) { /* DLAB */
            uart->dll = value;
            break;
        }
        u8250_handle_out(uart, value);
        uart->pending_intrs |= 1 << U8250_INTR_THRE;
        break;
    case U8250_IER_DLH:
        if (uart->lcr & (1 << 7)) { /* DLAB */
            uart->dlh = value;
            break;
        }
        uart->ier = value;
        break;
    case U8250_LCR:
        uart->lcr = value;
        break;
    case U8250_MCR:
        uart->mcr = value;
        break;
    }
}

u8250_state_t *u8250_new()
{
    u8250_state_t *uart = calloc(1, sizeof(u8250_state_t));
    assert(uart);

    return uart;
}

void u8250_delete(u8250_state_t *uart)
{
    free(uart);
}
