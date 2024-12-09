/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdlib.h>

#include "../riscv.h"
#include "../riscv_private.h"
#include "plic.h"

void plic_update_interrupts(plic_t *plic)
{
    riscv_t *rv = (riscv_t *) plic->rv;

    /* Update pending interrupts */
    plic->ip |= plic->active & ~plic->masked;
    plic->masked |= plic->active;
    /* Send interrupt to target */
    if (plic->ip & plic->ie)
        rv->csr_sip |= SIP_SEIP;
    else
        rv->csr_sip &= ~SIP_SEIP;
}

uint32_t plic_read(plic_t *plic, const uint32_t addr)
{
    uint32_t plic_read_val = 0;

    switch (addr) {
    case PLIC_INTR_PENDING:
        plic_read_val = plic->ip;
        break;
    case PLIC_INTR_ENABLE:
        plic_read_val = plic->ie;
        break;
    case PLIC_INTR_PRIORITY_THRESHOLD:
        /* no priority support: target priority threshold hardwired to 0 */
        plic_read_val = 0;
        break;
    case PLIC_INTR_CLAIM_OR_COMPLETE:
        /* claim */
        {
            uint32_t intr_candidate = plic->ip & plic->ie;
            if (intr_candidate) {
                plic_read_val = ilog2(intr_candidate);
                plic->ip &= ~(1U << (plic_read_val));
            }
            break;
        }
    default:
        return 0;
    }

    return plic_read_val;
}

void plic_write(plic_t *plic, const uint32_t addr, uint32_t value)
{
    switch (addr) {
    case PLIC_INTR_ENABLE:
        plic->ie = (value & ~1);
        break;
    case PLIC_INTR_PRIORITY_THRESHOLD:
        /* no priority support: target priority threshold hardwired to 0 */
        break;
    case PLIC_INTR_CLAIM_OR_COMPLETE:
        /* completion */
        if (plic->ie & (1U << value))
            plic->masked &= ~(1U << value);
        break;
    default:
        break;
    }

    return;
}

plic_t *plic_new()
{
    plic_t *plic = calloc(1, sizeof(plic_t));
    assert(plic);

    return plic;
}

void plic_delete(plic_t *plic)
{
    free(plic);
}
