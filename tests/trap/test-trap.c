/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/*
 * Exercise the real trap callback and observe the selected handler address.
 * SYSTEM executes handlers synchronously; stop at their first instruction
 * fetch so handler code cannot hide an incorrect entry address.
 */
#include <stdio.h>

#include "mpool.h"
#include "riscv_private.h"

#define FAULT_PC 0x1000U
#define VECTOR_BASE 0x8000U

#if RV32_HAS(SYSTEM_MMIO)
/* Provide the CLI-owned rv symbol required by SYSTEM_MMIO cleanup. */
riscv_t *rv;
#endif

static unsigned cases, failures, fetches;
static uint32_t handler_pc;

static uint32_t stop_at_handler(riscv_t *rv, uint32_t pc)
{
    fetches++;
    handler_pc = pc;
    rv_halt(rv);
    return 0;
}

static void check_route(unsigned privilege,
                        unsigned mode,
                        uint32_t cause,
                        uint32_t vector_offset)
{
    riscv_t cpu = {0};
    riscv_t *rv = &cpu;
    rv->priv_mode = privilege;
    rv->PC = FAULT_PC;
    rv->csr_stvec = rv->csr_mtvec = VECTOR_BASE | mode;
    rv->io.mem_ifetch = stop_at_handler;
    rv->io.on_trap = trap_handler;
#if RV32_HAS(SYSTEM)
    rv->block_ir_mp = mpool_create(4 * sizeof(rv_insn_t), sizeof(rv_insn_t));
#endif
    fetches = 0;
    handler_pc = 0;
    SET_CAUSE_AND_TVAL_THEN_TRAP(rv, cause, 0);

    /* Vectored offsets apply only to interrupts, never to exceptions. */
    const uint32_t expected = VECTOR_BASE + ((mode != 0) ? vector_offset : 0);
    const uint32_t epc =
        privilege == RV_PRIV_M_MODE ? rv->csr_mepc : rv->csr_sepc;
    bool ok = (rv->PC == expected) && (epc == FAULT_PC);
#if RV32_HAS(SYSTEM)
    ok = ok && (fetches == 1) && (handler_pc == expected);
    mpool_destroy(rv->block_ir_mp);
#else
    ok = ok && (fetches == 0);
#endif
    cases++;
    if (!ok) {
        failures++;
        fprintf(stderr,
                "FAIL privilege=%u mode=%u cause=%08x pc=%08x expected=%08x\n",
                privilege, mode, cause, rv->PC, expected);
    }
}

#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
static void check_unconfigured(unsigned privilege, uint32_t cause, bool halt)
{
    riscv_t cpu = {0};
    riscv_t *rv = &cpu;
    rv->priv_mode = privilege;
    rv->PC = FAULT_PC;
    rv->io.mem_ifetch = stop_at_handler;
    rv->io.on_trap = trap_handler;
    fetches = 0;
    SET_CAUSE_AND_TVAL_THEN_TRAP(rv, cause, 0);

    const uint32_t epc =
        privilege == RV_PRIV_M_MODE ? rv->csr_mepc : rv->csr_sepc;
    const bool ok = (rv_has_halted(rv) == halt) && (rv->PC == FAULT_PC) &&
                    (epc == FAULT_PC) && (rv->trap_cnt == 0) && (fetches == 0);
    cases++;
    if (!ok) {
        failures++;
        fprintf(stderr, "FAIL unset vector privilege=%u cause=%08x halt=%u\n",
                privilege, cause, rv_has_halted(rv));
    }
}
#endif

int main(void)
{
    const unsigned privileges[] = {RV_PRIV_U_MODE, RV_PRIV_S_MODE,
                                   RV_PRIV_M_MODE};
    /* Literal expected offsets from the mtvec/stvec vectored-mode contract. */
    const struct {
        uint32_t cause, offset;
        bool halt_without_vector;
    } inputs[] = {{ILLEGAL_INSN, 0, true},
                  {BREAKPOINT, 0, true},
                  {0x80000000U, 0, false},
                  {0x80000005U, 20, false},
                  {0x80000009U, 36, false}};
    for (unsigned p = 0; p < 3; p++)
        for (unsigned mode = 0; mode < 2; mode++)
            for (unsigned c = 0; c < 5; c++)
                check_route(privileges[p], mode, inputs[c].cause,
                            inputs[c].offset);
#if RV32_HAS(SYSTEM) && !RV32_HAS(ELF_LOADER)
    for (unsigned p = 0; p < 3; p++)
        for (unsigned c = 0; c < 5; c++)
            check_unconfigured(privileges[p], inputs[c].cause,
                               inputs[c].halt_without_vector);
#endif
    printf("Trap routing: %u cases, %u failures\n", cases, failures);
    return (failures != 0) ? 1 : 0;
}
