/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/*
 * Generate CSR instruction words in guest RAM and execute them through the
 * production decoder, rv_step(), and trap_handler(). The 88-byte bootstrap
 * ELF only initializes rv_create(); no assembler or cross compiler is needed.
 * Native observation is necessary: a guest-only test cannot tell whether
 * rv_step() returned immediately after the trap, before guest resumption.
 *
 * Run with make run-test-csr-unsupported (also in make check and make tests).
 * See README.md for configurations, test layout, and integrated RVV cases.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "riscv_private.h"

#if (RV32_HAS(Zicsr)) && (!RV32_HAS(SYSTEM_MMIO)) && (!RV32_HAS(RV32E))

#if RV32_HAS(SYSTEM)
#define CSR_STATE(cpu, field) (cpu)->csr_s##field
#define CSR_NUMBER(field) CSR_S##field
#define CSR_MODE RV_PRIV_S_MODE
/* sret */
#define CSR_RETURN 0x10200073
#else
#define CSR_STATE(cpu, field) (cpu)->csr_m##field
#define CSR_NUMBER(field) CSR_M##field
#define CSR_MODE RV_PRIV_M_MODE
/* mret */
#define CSR_RETURN 0x30200073
#endif

#define SENTINEL 0x13579bdfU
#define HANDLER_PC 0x80000U

static unsigned trap_count;
static uint32_t trap_pc, trap_cause, trap_rd;
static unsigned destination;
static unsigned case_count, failure_count;
static uint32_t next_pc = 0x1000;

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++)
        p[i] = value >> (8 * i);
}

/*
 * Encode csr[31:20], rs1/zimm[19:15], funct3[14:12], rd[11:7], SYSTEM.
 * op 1/2/3 selects CSRRW/CSRRS/CSRRC; 5/6/7 selects immediate forms.
 * Keep source (the encoded field) distinct from the register's value:
 * rs1=x0 and rs1=x5 with x5=0 exercise different decoder/write rules.
 */
static uint32_t csr_insn(unsigned csr,
                         unsigned op,
                         unsigned source,
                         unsigned rd)
{
    return (csr << 20) | (source << 15) | (op << 12) | (rd << 7) | 0x73;
}

/*
 * rv_create loads an ELF. This minimal little-endian RV32 executable has
 * one PT_LOAD segment containing EBREAK; cases replace guest RAM afterward.
 */
static bool write_boot_image(int fd)
{
    uint8_t image[88] = {0};
    memcpy(image, "\177ELF\1\1\1", 7);
    /* ET_EXEC */
    image[16] = 2;
    /* EM_RISCV */
    image[18] = 243;
    /* e_version */
    put32(image + 20, 1);
    /* e_entry */
    put32(image + 24, 0x1000);
    /* e_phoff */
    put32(image + 28, 52);
    /* e_ehsize */
    image[40] = 52;
    /* e_phentsize */
    image[42] = 32;
    /* e_phnum */
    image[44] = 1;
    /* e_shentsize */
    image[46] = 40;
    /* PT_LOAD */
    put32(image + 52, 1);
    /* p_offset */
    put32(image + 56, 84);
    /* p_vaddr */
    put32(image + 60, 0x1000);
    /* p_paddr */
    put32(image + 64, 0x1000);
    /* p_filesz */
    put32(image + 68, 4);
    /* p_memsz */
    put32(image + 72, 4);
    /* PF_R | PF_X */
    put32(image + 76, 5);
    /* p_align */
    put32(image + 80, 4);
    /* ebreak */
    put32(image + 84, 0x00100073);
    return write(fd, image, sizeof(image)) == sizeof(image);
}

static void record_trap(riscv_t *cpu)
{
    trap_count++;
    trap_pc = rv_get_pc(cpu);
    trap_rd = rv_get_reg(cpu, destination);
    trap_cause = CSR_STATE(cpu, cause);
    /* Observe the exception, then delegate to the production trap handler. */
    trap_handler(cpu);
}

static void stop_guest(riscv_t *cpu)
{
    rv_halt(cpu);
}

static void install_handler(riscv_t *cpu)
{
    const uint32_t code[] = {
        /* addi x28, x28, 1: handler count */
        0x001e0e13,
        /* csrrs x27, epc, x0: retain fault EPC */
        csr_insn(CSR_NUMBER(EPC), 2, 0, 27),
        /* addi x29, x27, 4 */
        0x004d8e93,
        /* csrrw x0, epc, x29 */
        csr_insn(CSR_NUMBER(EPC), 1, 29, 0),
        CSR_RETURN,
    };
    for (unsigned i = 0; i < sizeof(code) / sizeof(code[0]); i++)
        put32(PRIV(cpu)->mem->mem_base + HANDLER_PC + 4 * i, code[i]);
}

/* Load one case at a fresh PC and reset the hart state it observes. */
static uint32_t load_case(riscv_t *cpu,
                          uint32_t insn,
                          unsigned op,
                          unsigned source,
                          uint32_t source_value,
                          unsigned rd,
                          bool default_handler,
                          uint32_t initial_value)
{
    const uint32_t pc = next_pc;
    /* Never reuse translated code after changing guest RAM. */
    next_pc += 32;
    const uint32_t code[] = {
        /* addi x31, x0, 85: fault is not at the block start */
        0x05500f93,
        insn,
        /* addi x30, x30, 1: exactly one continuation */
        0x001f0f13,
        /* ebreak: test completion */
        0x00100073,
    };
    for (unsigned i = 0; i < sizeof(code) / sizeof(code[0]); i++)
        put32(PRIV(cpu)->mem->mem_base + pc + 4 * i, code[i]);

    memset(cpu->X, 0, sizeof(cpu->X));
    if ((source != 0) && (op < 5))
        rv_set_reg(cpu, source, source_value);
    if (rd != 0)
        rv_set_reg(cpu, rd, SENTINEL);
    cpu->priv_mode = CSR_MODE;
    CSR_STATE(cpu, scratch) = initial_value;
    CSR_STATE(cpu, tvec) = default_handler ? 0 : HANDLER_PC;
    CSR_STATE(cpu, cause) = SENTINEL;
    cpu->halt = false;
    destination = rd;
    trap_count = 0;
    trap_pc = trap_cause = trap_rd = SENTINEL;
    if (!rv_set_pc(cpu, pc)) {
        fprintf(stderr, "cannot set test PC\n");
        exit(1);
    }
    return pc;
}

static void run_case(riscv_t *cpu,
                     unsigned csr,
                     unsigned op,
                     unsigned source,
                     uint32_t source_value,
                     unsigned rd,
                     bool supported,
                     bool default_handler,
                     uint32_t initial_value)
{
    const uint32_t insn = csr_insn(csr, op, source, rd);
    rv_insn_t decoded = {0};
    const bool accepted = rv_decode(&decoded, insn);
    const uint32_t pc = load_case(cpu, insn, op, source, source_value, rd,
                                  default_handler, initial_value);
    /* A trap must return control to the dispatcher before fallthrough. */
    rv_step(cpu);
    const bool stopped_on_trap =
        supported || ((trap_count == 1) && (!rv_has_halted(cpu)) &&
                      (rv_get_reg(cpu, 30) == 0));
    /* Bound the driver as well as each rv_step cycle budget. */
    for (unsigned i = 1; (i < 16) && (!rv_has_halted(cpu)); i++)
        rv_step(cpu);

    const uint32_t actual_rd = rv_get_reg(cpu, rd);
    const uint32_t value =
        (op >= 5) ? source : ((source != 0) ? source_value : 0);
    const unsigned operation = op & 3;
    const uint32_t expected_csr = operation == 1   ? value
                                  : operation == 2 ? (initial_value | value)
                                                   : (initial_value & ~value);
    const uint32_t actual_csr = CSR_STATE(cpu, scratch);
    const uint32_t epc = CSR_STATE(cpu, epc);
    bool ok = stopped_on_trap && accepted && (rv_has_halted(cpu)) &&
              (rv_get_reg(cpu, 0) == 0) && (rv_get_reg(cpu, 31) == 85) &&
              (rv_get_reg(cpu, 30) == 1);
    if (supported) {
        ok = ok && (trap_count == 0) &&
             (actual_rd == ((rd != 0) ? initial_value : 0)) &&
             (actual_csr == expected_csr);
    } else {
        ok = ok && (trap_count == 1) && (trap_cause == ILLEGAL_INSN) &&
             (trap_pc == pc + 4) && (trap_rd == ((rd != 0) ? SENTINEL : 0)) &&
             (actual_rd == ((rd != 0) ? SENTINEL : 0)) && (epc == pc + 8);
        if (!default_handler)
            ok = ok && (rv_get_reg(cpu, 28) == 1) &&
                 (rv_get_reg(cpu, 27) == pc + 4);
    }
    case_count++;
    if (!ok) {
        failure_count++;
        fprintf(stderr,
                "FAIL csr=%03x op=%u source=%u value=%08x rd=%u default=%u\n",
                csr, op, source, source_value, rd, default_handler);
    }
}

/*
 * Run a supported access to a CSR other than scratch, which the caller sets
 * up and inspects. Require no trap, exactly one continuation, and rd.
 */
static void run_mapped_case(riscv_t *cpu,
                            unsigned csr,
                            unsigned op,
                            unsigned source,
                            uint32_t source_value,
                            uint32_t expected_rd)
{
    const unsigned rd = 10;
    const uint32_t insn = csr_insn(csr, op, source, rd);
    rv_insn_t decoded = {0};
    const bool accepted = rv_decode(&decoded, insn);
    load_case(cpu, insn, op, source, source_value, rd, false, 0);
    cpu->priv_mode = RV_PRIV_M_MODE;
    for (unsigned i = 0; (i < 16) && (!rv_has_halted(cpu)); i++)
        rv_step(cpu);
    const bool ok = accepted && rv_has_halted(cpu) && (trap_count == 0) &&
                    (rv_get_reg(cpu, 30) == 1) &&
                    (rv_get_reg(cpu, rd) == expected_rd);
    case_count++;
    if (!ok) {
        failure_count++;
        fprintf(stderr, "FAIL csr=%03x op=%u value=%08x mapped\n", csr, op,
                source_value);
    }
}

static void expect_state(unsigned csr, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        failure_count++;
        fprintf(stderr, "FAIL csr=%03x state=%08x expected=%08x\n", csr, actual,
                expected);
    }
}

/* Writes replace the implicit increment; reads observe the previous count. */
static void run_minstret_cases(riscv_t *cpu)
{
    const uint32_t code[] = {
        csr_insn(0xb82, 5, 0, 0),
        csr_insn(0xb02, 1, 5, 0),
        csr_insn(0xc02, 2, 0, 10),
        csr_insn(0xc82, 2, 0, 11),
        csr_insn(0xb02, 2, 0, 12),
        csr_insn(0xb82, 2, 0, 13),
        csr_insn(0xb82, 5, 3, 14),
        csr_insn(0xc02, 2, 0, 15),
        csr_insn(0xc82, 2, 0, 16),
        csr_insn(0xb02, 5, 8, 0),
        csr_insn(0xb02, 2, 0, 17),
        csr_insn(0xb02, 6, 2, 18),
        csr_insn(0xb02, 7, 8, 19),
        csr_insn(0xb02, 2, 6, 20),
        csr_insn(0xb02, 3, 6, 21),
        csr_insn(0xb02, 2, 0, 22),
        csr_insn(0xb02, 2, 7, 23),
        csr_insn(0xb02, 3, 7, 24),
        csr_insn(0xb02, 6, 0, 25),
        csr_insn(0xb02, 7, 0, 26),
        csr_insn(0xb02, 5, 0, 0),
        csr_insn(0xb02, 1, 0, 27),
        csr_insn(0xb02, 2, 0, 28),
        csr_insn(0xb82, 1, 8, 0),
        csr_insn(0xb02, 1, 5, 0),
        csr_insn(0xb02, 2, 0, 8),
        csr_insn(0xb02, 2, 0, 9),
        csr_insn(0xb82, 2, 0, 29),
        0x00100073, /* ebreak */
    };
    const uint32_t expected[] = {
        0xfffffffe, 0, 0, 1, 1, 2, 3, 8, 9, 11, 3, 7, 3, 4, 4, 4, 5, 0, 0,
    };
    const uint32_t pc = load_case(cpu, code[0], 1, 5, 0xfffffffe, 0, false, 0);
    next_pc += sizeof(code);
    for (unsigned i = 0; i < sizeof(code) / sizeof(code[0]); i++)
        put32(PRIV(cpu)->mem->mem_base + pc + 4 * i, code[i]);
    cpu->priv_mode = RV_PRIV_M_MODE;
    cpu->X[6] = 4;
    cpu->X[8] = UINT32_MAX;
    const uint64_t cycle = cpu->csr_cycle;
    const uint64_t mcycle_offset = cpu->csr_mcycle_offset;
    for (unsigned i = 0; i < 16 && !rv_has_halted(cpu); i++)
        rv_step(cpu);
    case_count++;
    expect_state(0xb02, trap_count, 0);
    expect_state(0xb02, rv_has_halted(cpu), true);
    expect_state(0xb02, cpu->X[0], 0);
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
        expect_state(0xb02, cpu->X[10 + i], expected[i]);
    expect_state(0xb02, cpu->X[8], 0xfffffffe);
    expect_state(0xb02, cpu->X[9], UINT32_MAX);
    expect_state(0xb82, cpu->X[29], 0);
    expect_state(0xb02, cpu->csr_cycle - cycle, 29);
    expect_state(0xb02, cpu->csr_mcycle_offset != mcycle_offset, false);
    /* High-half writes persist across blocks and leave mcycle alone. */
    run_mapped_case(cpu, 0xb82, 1, 5, UINT32_MAX, 0);
    run_mapped_case(cpu, 0xb82, 2, 5, 0, UINT32_MAX);
    run_mapped_case(cpu, 0xb82, 3, 5, 0xfffffffc, UINT32_MAX);
    run_mapped_case(cpu, 0xb82, 6, 4, 0, 3);
    run_mapped_case(cpu, 0xb82, 7, 2, 0, 7);
    run_mapped_case(cpu, 0xc82, 2, 0, 0, 5);
    expect_state(0xb02, cpu->csr_mcycle_offset != mcycle_offset, false);
    for (unsigned csr = 0xc02; csr <= 0xc82; csr += 0x80)
        for (unsigned op = 1; op <= 5; op += 4)
            for (unsigned def = 0; def < 2; def++)
                run_case(cpu, csr, op, 0, 0, 10, false, def, 0xa5);
    const unsigned operations[] = {1, 2, 3, 5, 6, 7};
    for (unsigned csr = 0xb02; csr <= 0xb82; csr += 0x80) {
        expect_state(csr, insn_reads_counter(csr_insn(csr, 2, 0, 10)), true);
        for (unsigned mode = RV_PRIV_U_MODE; mode <= RV_PRIV_S_MODE; mode++)
            for (unsigned i = 0; i < 6; i++) {
                const uint32_t fault_pc =
                    load_case(cpu, csr_insn(csr, operations[i], 0, 10),
                              operations[i], 0, 0, 10, true, 0);
                cpu->priv_mode = mode;
                rv_step(cpu);
                case_count++;
                expect_state(csr, trap_count, 1);
                expect_state(csr, cpu->csr_scause, ILLEGAL_INSN);
                expect_state(csr, trap_pc, fault_pc + 4);
                expect_state(csr, cpu->X[10], SENTINEL);
                expect_state(csr, cpu->X[30], 0);
            }
    }
}

#if RV32_HAS(SYSTEM)
static void stop_counter_trap(riscv_t *cpu)
{
    trap_count++;
    trap_pc = cpu->PC;
    trap_rd = cpu->X[10];
    trap_cause = cpu->csr_scause;
    rv_halt(cpu);
}

/* The guest handler dispatches even when rv_decode rejects the encoding. */
static void run_handler_readonly_cases(riscv_t *cpu)
{
    const unsigned readonly_csrs[] = {
        0xc00,     0xc80,         0xc02,       0xc82,      CSR_TIME,
        CSR_TIMEH, CSR_MVENDORID, CSR_MARCHID, CSR_MIMPID, CSR_MHARTID,
#if RV32_HAS(EXT_V)
        CSR_VL,    CSR_VTYPE,     CSR_VLENB,
#endif
    };
    const unsigned operations[] = {1, 2, 3, 5, 6, 7};
    cpu->io.on_trap = stop_counter_trap;
    for (unsigned c = 0; c < sizeof(readonly_csrs) / sizeof(readonly_csrs[0]);
         c++)
        for (unsigned op = 0; op < 6; op++)
            for (unsigned value = 0; value < 2; value++)
                for (unsigned source = 0; source <= 5; source += 5) {
                    const uint32_t pc =
                        load_case(cpu, 0, operations[op], source,
                                  value ? UINT32_MAX : 0, 10, false, 0);
                    const bool write =
                        source || operations[op] == 1 || operations[op] == 5;
                    const uint32_t ids[] = {cpu->csr_mvendorid,
                                            cpu->csr_marchid, cpu->csr_mimpid,
                                            cpu->csr_mhartid};
                    const uint32_t timer[] = {cpu->csr_time[0],
                                              cpu->csr_time[1]};
                    uint8_t *handler = PRIV(cpu)->mem->mem_base + pc + 16;
                    put32(handler, csr_insn(readonly_csrs[c], operations[op],
                                            source, 10));
                    put32(handler + 4, 0x001f0f13); /* addi x30, x30, 1 */
                    put32(handler + 8, 0x00100073); /* ebreak */
                    cpu->csr_stvec = pc + 16;
                    cpu->csr_scause = ILLEGAL_INSN;
                    cpu->trap_cnt = 1;
                    const uint64_t cycle_offset = cpu->csr_mcycle_offset;
                    const uint64_t instret_offset = cpu->csr_minstret_offset;
                    trap_handler(cpu);
                    case_count++;
                    expect_state(readonly_csrs[c], rv_has_halted(cpu), true);
                    expect_state(readonly_csrs[c], trap_count, write);
                    expect_state(readonly_csrs[c], cpu->X[30], !write);
                    expect_state(readonly_csrs[c],
                                 cpu->csr_mcycle_offset != cycle_offset, false);
                    expect_state(readonly_csrs[c],
                                 cpu->csr_minstret_offset != instret_offset,
                                 false);
                    expect_state(readonly_csrs[c], cpu->csr_mvendorid, ids[0]);
                    expect_state(readonly_csrs[c], cpu->csr_marchid, ids[1]);
                    expect_state(readonly_csrs[c], cpu->csr_mimpid, ids[2]);
                    expect_state(readonly_csrs[c], cpu->csr_mhartid, ids[3]);
                    if (!write && c >= 6 && c < 10)
                        expect_state(readonly_csrs[c], cpu->X[10], ids[c - 6]);
                    if (write) {
                        expect_state(readonly_csrs[c], cpu->csr_time[0],
                                     timer[0]);
                        expect_state(readonly_csrs[c], cpu->csr_time[1],
                                     timer[1]);
                        expect_state(readonly_csrs[c], trap_cause,
                                     ILLEGAL_INSN);
                        expect_state(readonly_csrs[c], trap_pc, pc + 16);
                        expect_state(readonly_csrs[c], trap_rd, SENTINEL);
                        expect_state(readonly_csrs[c], cpu->X[10], SENTINEL);
                    }
                    cpu->trap_cnt = 0;
                }
    cpu->io.on_trap = record_trap;
    install_handler(cpu);
}
#endif

/* Mandatory CSRs must reach their state, not trap or read a stale zero. */
static void run_mapped_cases(riscv_t *cpu)
{
    const struct {
        unsigned csr;
        uint32_t value;
    } info[] = {
        {CSR_MVENDORID, RV_MVENDORID},
        {CSR_MARCHID, RV_MARCHID},
        {CSR_MIMPID, RV_MIMPID},
        {CSR_MHARTID, 0},
    };
    for (unsigned i = 0; i < sizeof(info) / sizeof(info[0]); i++) {
        /* csrrs a0, id, x0 reads; csrrw/csrrwi always write, so trap. */
        run_mapped_case(cpu, info[i].csr, 2, 0, 0, info[i].value);
        for (unsigned op = 1; op <= 5; op += 4)
            for (unsigned def = 0; def < 2; def++)
                run_case(cpu, info[i].csr, op, 0, 0, 10, false, def, 0xa5);
    }

    cpu->csr_mie = 0x888;
    run_mapped_case(cpu, CSR_MIE, 3, 5, 0x8, 0x888);
    expect_state(CSR_MIE, cpu->csr_mie, 0x880);
    /* Nothing else observes mcounteren, so a write must read back. */
    run_mapped_case(cpu, CSR_MCOUNTEREN, 1, 5, 0x7, 0);
    run_mapped_case(cpu, CSR_MCOUNTEREN, 2, 0, 0, 0x7);

    /* Machine writes persist across blocks without changing the clock. */
    const uint64_t cycle = cpu->csr_cycle;
    const unsigned first_case = case_count;
    run_mapped_case(cpu, 0xb80, 5, 1, 0, 0);
    run_mapped_case(cpu, 0xb00, 1, 5, 0xfffffffe,
                    (uint32_t) (cpu->csr_cycle + 2));
    run_mapped_case(cpu, 0xc80, 2, 0, 0, 2);
    run_mapped_case(cpu, 0xb80, 2, 0, 0, 2);
    run_mapped_case(cpu, 0xc00, 2, 0, 0, 10);
    run_mapped_case(cpu, 0xb00, 2, 0, 0, 14);
    run_mapped_case(cpu, 0xb80, 2, 5, 4, 2);
    run_mapped_case(cpu, 0xb80, 3, 5, 2, 6);
    run_mapped_case(cpu, 0xb80, 6, 2, 0, 4);
    run_mapped_case(cpu, 0xb80, 7, 4, 0, 6);
    run_mapped_case(cpu, 0xc80, 2, 0, 0, 2);
    expect_state(0xb00, cpu->csr_cycle, cycle + 4 * (case_count - first_case));
    expect_state(0xb80, cpu->csr_cycle >> 32, cycle >> 32);
    if (!insn_reads_counter(csr_insn(0xb00, 2, 0, 10)) ||
        !insn_reads_counter(csr_insn(0xb80, 2, 0, 10))) {
        failure_count++;
        fprintf(stderr, "FAIL mcycle missing from counter entry check\n");
    }
    for (unsigned csr = 0xc00; csr <= 0xc80; csr += 0x80)
        for (unsigned op = 1; op <= 5; op += 4)
            run_case(cpu, csr, op, 0, 0, 10, false, false, 0xa5);

#if RV32_HAS(EXT_F)
    /* frm is fcsr[7:5]; each access must keep fflags in fcsr[4:0]. */
    cpu->csr_fcsr = 0x5f;
    run_mapped_case(cpu, CSR_FRM, 1, 5, 0xfffffffb, 0x2);
    expect_state(CSR_FRM, cpu->csr_fcsr, 0x7f);
    run_mapped_case(cpu, CSR_FRM, 6, 4, 0, 0x3);
    expect_state(CSR_FRM, cpu->csr_fcsr, 0xff);
    run_mapped_case(cpu, CSR_FRM, 3, 5, 0x5, 0x7);
    expect_state(CSR_FRM, cpu->csr_fcsr, 0x5f);
    run_mapped_case(cpu, CSR_FRM, 2, 0, 0, 0x2);
    expect_state(CSR_FRM, cpu->csr_fcsr, 0x5f);

    /* fflags updates must preserve frm, including masks above bit 4. */
    run_mapped_case(cpu, CSR_FRM, 5, 3, 0, 0x2);
    run_mapped_case(cpu, CSR_FFLAGS, 5, 0, 0, 0x1f);
    run_mapped_case(cpu, CSR_FRM, 2, 0, 0, 0x3);
    expect_state(CSR_FCSR, cpu->csr_fcsr, 0x60);
    run_mapped_case(cpu, CSR_FFLAGS, 1, 5, 0xffffffe1, 0);
    expect_state(CSR_FCSR, cpu->csr_fcsr, 0x61);
    run_mapped_case(cpu, CSR_FFLAGS, 2, 5, 0xffffffe2, 0x1);
    expect_state(CSR_FCSR, cpu->csr_fcsr, 0x63);
    run_mapped_case(cpu, CSR_FFLAGS, 3, 5, 0xffffffe1, 0x3);
    expect_state(CSR_FCSR, cpu->csr_fcsr, 0x62);
    run_mapped_case(cpu, CSR_FFLAGS, 6, 4, 0, 0x2);
    run_mapped_case(cpu, CSR_FFLAGS, 7, 2, 0, 0x6);
    expect_state(CSR_FCSR, cpu->csr_fcsr, 0x64);
#endif
}

int main(void)
{
    char path[MAX_PATH_LEN] = "/tmp/rv32emu-csr-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0)
        return 1;
    bool written = write_boot_image(fd);
    close(fd);
    if (!written) {
        unlink(path);
        return 1;
    }
    vm_attr_t attr = {
        .mem_size = 4 * 1024 * 1024,
        .stack_size = 4096,
        .args_offset_size = 4096,
        .log_level = LOG_FATAL,
        .cycle_per_step = 32,
    };
    attr.data.user.elf_program = path;
    riscv_t *cpu = rv_create(&attr);
    unlink(path);
    if (!cpu)
        return 1;
    cpu->io.on_trap = record_trap;
    cpu->io.on_ebreak = stop_guest;
    install_handler(cpu);
    const unsigned scratch = CSR_NUMBER(SCRATCH);
    /*
     * Generate the matrix rather than maintaining hundreds of guest files:
     * six forms x two source fields x two destinations = 24 scratch cases.
     * Three register-zero-value and six zero-result controls bring it to 33.
     * Eight missing CSRs x 24, minus 12 forbidden FFF writes, times two
     * handlers = 360; three register-zero cases and one encoding add four.
     * Thus 397 execution cases are separate from the decoder checks below.
     * To extend coverage, add an address to missing[] only while lookup is
     * absent. When an address gains support, replace its negative cases with
     * positive controls, keeping coverage for missing entries.
     */
    const unsigned operations[] = {1, 2, 3, 5, 6, 7};
    const unsigned missing[] = {0x7ff, 0x800, 0xb01, 0xb03,
                                0xb81, 0xb83, 0xbff, 0xfff};
    for (unsigned i = 0; i < 6; i++) {
        for (unsigned source = 0; source <= 5; source += 5) {
            for (unsigned rd = 0; rd <= 10; rd += 10) {
                run_case(cpu, scratch, operations[i], source, 8, rd, true,
                         false, 0xa5);
                for (unsigned j = 0; j < sizeof(missing) / sizeof(missing[0]);
                     j++) {
                    /*
                     * Nonzero-source FFF is forbidden at decode, not a
                     * missing-CSR execution path; check its rejection
                     * separately below, without claiming trap delivery.
                     */
                    if ((missing[j] == 0xfff) && (source != 0))
                        continue;
                    run_case(cpu, missing[j], operations[i], source, 8, rd,
                             false, false, 0xa5);
                    run_case(cpu, missing[j], operations[i], source, 8, rd,
                             false, true, 0xa5);
                }
            }
        }
        /* A nonzero register field holding zero still performs an access. */
        if (operations[i] < 5) {
            run_case(cpu, scratch, operations[i], 5, 0, 10, true, false, 0xa5);
            run_case(cpu, 0xb03, operations[i], 5, 0, 10, false, false, 0xa5);
        }
    }
    /* csrrw x1, unsupported mhpmcounter3, x2 */
    run_case(cpu, 0xb03, 1, 2, 8, 1, false, false, 0xa5);
    /* A valid zero-valued read must not be mistaken for a missing CSR. */
    for (unsigned i = 0; i < 6; i++)
        run_case(cpu, scratch, operations[i], 0, 0, 10, true, false, 0);
    run_mapped_cases(cpu);
    for (unsigned csr = CSR_TIME; csr <= CSR_TIMEH; csr += 0x80)
        for (unsigned op = 1; op <= 5; op += 4)
            for (unsigned def = 0; def < 2; def++)
                run_case(cpu, csr, op, 0, 0, 10, false, def, 0xa5);
    run_minstret_cases(cpu);
#if RV32_HAS(SYSTEM)
    for (unsigned csr = 0xb00; csr <= 0xb80; csr += 0x80)
        for (unsigned i = 0; i < 6; i++)
            run_case(cpu, csr, operations[i], 0, 0, 10, false, false, 0xa5);
#endif
#if RV32_HAS(EXT_V)
    /* Zero-source writes reach the helpers despite the decoder's checks. */
    for (unsigned csr = CSR_VL; csr <= CSR_VLENB; csr++)
        for (unsigned op = 1; op <= 5; op += 4)
            for (unsigned def = 0; def < 2; def++)
                run_case(cpu, csr, op, 0, 0, 10, false, def, 0xa5);
#endif
#if RV32_HAS(SYSTEM)
    run_handler_readonly_cases(cpu);
#endif
    unsigned decoder_failures = 0;
    const unsigned read_only[] = {0xfff, 0xc00, 0xc80, 0xc02, 0xc82};
    for (unsigned csr = 0; csr < sizeof(read_only) / sizeof(read_only[0]);
         csr++)
        for (unsigned i = 0; i < 6; i++) {
            rv_insn_t decoded = {0};
            decoder_failures += rv_decode(
                &decoded, csr_insn(read_only[csr], operations[i], 5, 10));
        }
    printf("CSR decoder: 30 forbidden cases, %u failures\n", decoder_failures);
    rv_delete(cpu);
    printf("CSR execution: %u cases, %u failures\n", case_count, failure_count);
    return ((failure_count != 0) || (decoder_failures != 0)) ? 1 : 0;
}
#else
int main(void)
{
    puts("SKIP CSR execution: requires Zicsr, RV32I, and user ELF loading");
    return 0;
}
#endif
