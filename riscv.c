#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "riscv.h"
#include "riscv_private.h"

#ifdef DEBUG
#define print_register(rv)                           \
    do {                                             \
        for (int i = 0; i < 32; ++i) {               \
            printf("X[%02d] = %08X\t", i, rv->X[i]); \
            if (!(i % 5)) {                          \
                putchar('\n');                       \
            }                                        \
        }                                            \
        putchar('\n');                               \
    } while (0)

#define debug_print(str) printf("DEBUG: " #str "\n")
#define debug_print_hexval(var) printf("DEBUG: " #var " = %08X(hex)\n", var)
#else
#define print_register(rv) (void) 0
#define debug_print(str) (void) 0
#define debug_print_hexval(var) (void) 0
#endif

static void rv_except_inst_misaligned(struct riscv_t *rv, uint32_t old_pc)
{
    const uint32_t base = rv->csr_mtvec & ~0x3;
    const uint32_t mode = rv->csr_mtvec & 0x3;

    const uint32_t code = 0;  // instruction address misaligned

    rv->csr_mepc = old_pc;
    rv->csr_mtval = rv->PC;

    switch (mode) {
    case 0:  // DIRECT
        rv->PC = base;
        break;
    case 1:  // VECTORED
        rv->PC = base + rv->inst_len * code;
        break;
    }

    rv->csr_mcause = code;
}

static void rv_except_load_misaligned(struct riscv_t *rv, uint32_t addr)
{
    const uint32_t base = rv->csr_mtvec & ~0x3;
    const uint32_t mode = rv->csr_mtvec & 0x3;

    const uint32_t code = 4;  // load address misaligned

    rv->csr_mepc = rv->PC;
    rv->csr_mtval = addr;

    switch (mode) {
    case 0:  // DIRECT
        rv->PC = base;
        break;
    case 1:  // VECTORED
        rv->PC = base + rv->inst_len * code;
        break;
    }

    rv->csr_mcause = code;
}

static void rv_except_store_misaligned(struct riscv_t *rv, uint32_t addr)
{
    const uint32_t base = rv->csr_mtvec & ~0x3;
    const uint32_t mode = rv->csr_mtvec & 0x3;

    const uint32_t code = 6;  // store address misaligned

    rv->csr_mepc = rv->PC;
    rv->csr_mtval = addr;

    switch (mode) {
    case 0:  // DIRECT
        rv->PC = base;
        break;
    case 1:  // VECTORED
        rv->PC = base + rv->inst_len * code;
        break;
    }

    rv->csr_mcause = code;
}

static void rv_except_illegal_inst(struct riscv_t *rv UNUSED)
{
    /* TODO: dump more information */
    assert(!"illegal instruction");
}

static bool op_load(struct riscv_t *rv, uint32_t inst UNUSED)
{
    // itype format
    const int32_t imm = dec_itype_imm(inst);
    const uint32_t rs1 = dec_rs1(inst);
    const uint32_t funct3 = dec_funct3(inst);
    const uint32_t rd = dec_rd(inst);

    // load address
    const uint32_t addr = rv->X[rs1] + imm;

    // dispatch by read size
    switch (funct3) {
    case 0:  // LB
        rv->X[rd] = sign_extend_b(rv->io.mem_read_b(rv, addr));
        break;
    case 1:  // LH
        if (addr & 1) {
            rv_except_load_misaligned(rv, addr);
            return false;
        }
        rv->X[rd] = sign_extend_h(rv->io.mem_read_s(rv, addr));
        break;
    case 2:  // LW
        if (addr & 3) {
            rv_except_load_misaligned(rv, addr);
            return false;
        }
        rv->X[rd] = rv->io.mem_read_w(rv, addr);
        break;
    case 4:  // LBU
        rv->X[rd] = rv->io.mem_read_b(rv, addr);
        break;
    case 5:  // LHU
        if (addr & 1) {
            rv_except_load_misaligned(rv, addr);
            return false;
        }
        rv->X[rd] = rv->io.mem_read_s(rv, addr);
        break;
    default:
        rv_except_illegal_inst(rv);
        return false;
    }
    // step over instruction
    rv->PC += rv->inst_len;
    // enforce zero register
    if (rd == rv_reg_zero)
        rv->X[rv_reg_zero] = 0;
    return true;
}

#ifdef ENABLE_Zifencei
static bool op_misc_mem(struct riscv_t *rv, uint32_t inst UNUSED)
{
    // FIXME: fill real implementations
    rv->PC += 4;
    return true;
}
#else
#define op_misc_mem NULL
#endif  // ENABLE_Zifencei

static bool op_op_imm(struct riscv_t *rv, uint32_t inst)
{
    // i-type decode
    const int32_t imm = dec_itype_imm(inst);
    const uint32_t rd = dec_rd(inst);
    const uint32_t rs1 = dec_rs1(inst);
    const uint32_t funct3 = dec_funct3(inst);

    // dispatch operation type
    switch (funct3) {
    case 0:  // ADDI
        rv->X[rd] = (int32_t)(rv->X[rs1]) + imm;
        break;
    case 1:  // SLLI
        rv->X[rd] = rv->X[rs1] << (imm & 0x1f);
        break;
    case 2:  // SLTI
        rv->X[rd] = ((int32_t)(rv->X[rs1]) < imm) ? 1 : 0;
        break;
    case 3:  // SLTIU
        rv->X[rd] = (rv->X[rs1] < (uint32_t) imm) ? 1 : 0;
        break;
    case 4:  // XORI
        rv->X[rd] = rv->X[rs1] ^ imm;
        break;
    case 5:
        if (imm & ~0x1f) {
            // SRAI
            rv->X[rd] = ((int32_t) rv->X[rs1]) >> (imm & 0x1f);
        } else {
            // SRLI
            rv->X[rd] = rv->X[rs1] >> (imm & 0x1f);
        }
        break;
    case 6:  // ORI
        rv->X[rd] = rv->X[rs1] | imm;
        break;
    case 7:  // ANDI
        rv->X[rd] = rv->X[rs1] & imm;
        break;
    default:
        rv_except_illegal_inst(rv);
        return false;
    }

    // step over instruction
    rv->PC += rv->inst_len;

    // enforce zero register
    if (rd == rv_reg_zero)
        rv->X[rv_reg_zero] = 0;
    return true;
}

// add upper immediate to pc
static bool op_auipc(struct riscv_t *rv, uint32_t inst)
{
    // u-type decode
    const uint32_t rd = dec_rd(inst);
    const uint32_t val = dec_utype_imm(inst) + rv->PC;
    rv->X[rd] = val;

    // step over instruction
    rv->PC += rv->inst_len;

    // enforce zero register
    if (rd == rv_reg_zero)
        rv->X[rv_reg_zero] = 0;
    return true;
}

static bool op_store(struct riscv_t *rv, uint32_t inst)
{
    // s-type format
    const int32_t imm = dec_stype_imm(inst);
    const uint32_t rs1 = dec_rs1(inst);
    const uint32_t rs2 = dec_rs2(inst);
    const uint32_t funct3 = dec_funct3(inst);

    // store address
    const uint32_t addr = rv->X[rs1] + imm;
    const uint32_t data = rv->X[rs2];

    // dispatch by write size
    switch (funct3) {
    case 0:  // SB
        rv->io.mem_write_b(rv, addr, data);
        break;
    case 1:  // SH
        if (addr & 1) {
            rv_except_store_misaligned(rv, addr);
            return false;
        }
        rv->io.mem_write_s(rv, addr, data);
        break;
    case 2:  // SW
        if (addr & 3) {
            rv_except_store_misaligned(rv, addr);
            return false;
        }
        rv->io.mem_write_w(rv, addr, data);
        break;
    default:
        rv_except_illegal_inst(rv);
        return false;
    }

    // step over instruction
    rv->PC += rv->inst_len;
    return true;
}

static bool op_op(struct riscv_t *rv, uint32_t inst)
{
    // r-type decode
    const uint32_t rd = dec_rd(inst);
    const uint32_t funct3 = dec_funct3(inst);
    const uint32_t rs1 = dec_rs1(inst);
    const uint32_t rs2 = dec_rs2(inst);
    const uint32_t funct7 = dec_funct7(inst);

    // XXX: skip zero register here

    switch (funct7) {
    case 0b0000000:
        switch (funct3) {
        case 0b000:  // ADD
            rv->X[rd] = (int32_t)(rv->X[rs1]) + (int32_t)(rv->X[rs2]);
            break;
        case 0b001:  // SLL
            rv->X[rd] = rv->X[rs1] << (rv->X[rs2] & 0x1f);
            break;
        case 0b010:  // SLT
            rv->X[rd] = ((int32_t)(rv->X[rs1]) < (int32_t)(rv->X[rs2])) ? 1 : 0;
            break;
        case 0b011:  // SLTU
            rv->X[rd] = (rv->X[rs1] < rv->X[rs2]) ? 1 : 0;
            break;
        case 0b100:  // XOR
            rv->X[rd] = rv->X[rs1] ^ rv->X[rs2];
            break;
        case 0b101:  // SRL
            rv->X[rd] = rv->X[rs1] >> (rv->X[rs2] & 0x1f);
            break;
        case 0b110:  // OR
            rv->X[rd] = rv->X[rs1] | rv->X[rs2];
            break;
        case 0b111:  // AND
            rv->X[rd] = rv->X[rs1] & rv->X[rs2];
            break;
        default:
            rv_except_illegal_inst(rv);
            return false;
        }
        break;
#ifdef ENABLE_RV32M
    case 0b0000001:
        // RV32M instructions
        switch (funct3) {
        case 0b000:  // MUL
            rv->X[rd] = (int32_t) rv->X[rs1] * (int32_t) rv->X[rs2];
            break;
        case 0b001: {  // MULH
            const int64_t a = (int32_t) rv->X[rs1];
            const int64_t b = (int32_t) rv->X[rs2];
            rv->X[rd] = ((uint64_t)(a * b)) >> 32;
        } break;
        case 0b010: {  // MULHSU
            const int64_t a = (int32_t) rv->X[rs1];
            const uint64_t b = rv->X[rs2];
            rv->X[rd] = ((uint64_t)(a * b)) >> 32;
        } break;
        case 0b011:  // MULHU
            rv->X[rd] = ((uint64_t) rv->X[rs1] * (uint64_t) rv->X[rs2]) >> 32;
            break;
        case 0b100: {  // DIV
            const int32_t dividend = (int32_t) rv->X[rs1];
            const int32_t divisor = (int32_t) rv->X[rs2];
            if (divisor == 0) {
                rv->X[rd] = ~0u;
            } else if (divisor == -1 && rv->X[rs1] == 0x80000000u) {
                rv->X[rd] = rv->X[rs1];
            } else {
                rv->X[rd] = dividend / divisor;
            }
        } break;
        case 0b101: {  // DIVU
            const uint32_t dividend = rv->X[rs1];
            const uint32_t divisor = rv->X[rs2];
            if (divisor == 0) {
                rv->X[rd] = ~0u;
            } else {
                rv->X[rd] = dividend / divisor;
            }
        } break;
        case 0b110: {  // REM
            const int32_t dividend = rv->X[rs1];
            const int32_t divisor = rv->X[rs2];
            if (divisor == 0) {
                rv->X[rd] = dividend;
            } else if (divisor == -1 && rv->X[rs1] == 0x80000000u) {
                rv->X[rd] = 0;
            } else {
                rv->X[rd] = dividend % divisor;
            }
        } break;
        case 0b111: {  // REMU
            const uint32_t dividend = rv->X[rs1];
            const uint32_t divisor = rv->X[rs2];
            if (divisor == 0) {
                rv->X[rd] = dividend;
            } else {
                rv->X[rd] = dividend % divisor;
            }
        } break;
        default:
            rv_except_illegal_inst(rv);
            return false;
        }
        break;
#endif  // ENABLE_RV32M
    case 0b0100000:
        switch (funct3) {
        case 0b000:  // SUB
            rv->X[rd] = (int32_t)(rv->X[rs1]) - (int32_t)(rv->X[rs2]);
            break;
        case 0b101:  // SRA
            rv->X[rd] = ((int32_t) rv->X[rs1]) >> (rv->X[rs2] & 0x1f);
            break;
        default:
            rv_except_illegal_inst(rv);
            return false;
        }
        break;
    default:
        rv_except_illegal_inst(rv);
        return false;
    }
    // step over instruction
    rv->PC += rv->inst_len;
    // enforce zero register
    if (rd == rv_reg_zero)
        rv->X[rv_reg_zero] = 0;
    return true;
}

static bool op_lui(struct riscv_t *rv, uint32_t inst)
{
    // u-type decode
    const uint32_t rd = dec_rd(inst);
    const uint32_t val = dec_utype_imm(inst);
    rv->X[rd] = val;

    // step over instruction
    rv->PC += rv->inst_len;

    // enforce zero register
    if (rd == rv_reg_zero)
        rv->X[rv_reg_zero] = 0;
    return true;
}

static bool op_branch(struct riscv_t *rv, uint32_t inst)
{
    const uint32_t pc = rv->PC;
    // b-type decode
    const uint32_t func3 = dec_funct3(inst);
    const int32_t imm = dec_btype_imm(inst);
    const uint32_t rs1 = dec_rs1(inst);
    const uint32_t rs2 = dec_rs2(inst);

    // track if branch is taken or not
    bool taken = false;

    // dispatch by branch type
    switch (func3) {
    case 0:  // BEQ
        taken = (rv->X[rs1] == rv->X[rs2]);
        break;
    case 1:  // BNE
        taken = (rv->X[rs1] != rv->X[rs2]);
        break;
    case 4:  // BLT
        taken = ((int32_t) rv->X[rs1] < (int32_t) rv->X[rs2]);
        break;
    case 5:  // BGE
        taken = ((int32_t) rv->X[rs1] >= (int32_t) rv->X[rs2]);
        break;
    case 6:  // BLTU
        taken = (rv->X[rs1] < rv->X[rs2]);
        break;
    case 7:  // BGEU
        taken = (rv->X[rs1] >= rv->X[rs2]);
        break;
    default:
        rv_except_illegal_inst(rv);
        return false;
    }
    // perform branch action
    if (taken) {
        rv->PC += imm;
        if (rv->PC & 0x3)
            rv_except_inst_misaligned(rv, pc);
    } else {
        // step over instruction
        rv->PC += rv->inst_len;
    }
    // can branch
    return false;
}

static bool op_jalr(struct riscv_t *rv, uint32_t inst)
{
    const uint32_t pc = rv->PC;

    // i-type decode
    const uint32_t rd = dec_rd(inst);
    const uint32_t rs1 = dec_rs1(inst);
    const int32_t imm = dec_itype_imm(inst);

    // compute return address
    const uint32_t ra = rv->PC + rv->inst_len;

    // jump
    rv->PC = (rv->X[rs1] + imm) & ~1u;

    // link
    if (rd != rv_reg_zero)
        rv->X[rd] = ra;

    // check for exception
    if (rv->PC & 0x3) {
        rv_except_inst_misaligned(rv, pc);
        return false;
    }
    // can branch
    return false;
}

static bool op_jal(struct riscv_t *rv, uint32_t inst)
{
    const uint32_t pc = rv->PC;
    // j-type decode
    const uint32_t rd = dec_rd(inst);
    const int32_t rel = dec_jtype_imm(inst);

    // compute return address
    const uint32_t ra = rv->PC + rv->inst_len;
    rv->PC += rel;

    // link
    if (rd != rv_reg_zero)
        rv->X[rd] = ra;

    // check alignment of PC
    if (rv->PC & 0x3) {
        rv_except_inst_misaligned(rv, pc);
        return false;
    }
    // can branch
    return false;
}

// get a pointer to a CSR
static uint32_t *csr_get_ptr(struct riscv_t *rv, uint32_t csr)
{
    switch (csr) {
    case CSR_CYCLE:
        return (uint32_t *) (&rv->csr_cycle) + 0;
    case CSR_CYCLEH:
        return (uint32_t *) (&rv->csr_cycle) + 1;
    case CSR_MSTATUS:
        return (uint32_t *) (&rv->csr_mstatus);
    case CSR_MTVEC:
        return (uint32_t *) (&rv->csr_mtvec);
    case CSR_MISA:
        return (uint32_t *) (&rv->csr_misa);
    case CSR_MSCRATCH:
        return (uint32_t *) (&rv->csr_mscratch);
    case CSR_MEPC:
        return (uint32_t *) (&rv->csr_mepc);
    case CSR_MCAUSE:
        return (uint32_t *) (&rv->csr_mcause);
    case CSR_MTVAL:
        return (uint32_t *) (&rv->csr_mtval);
    case CSR_MIP:
        return (uint32_t *) (&rv->csr_mip);
    default:
        return NULL;
    }
}

static bool csr_is_writable(uint32_t csr)
{
    return csr < 0xc00;
}

// perform csrrw
static uint32_t csr_csrrw(struct riscv_t *rv, uint32_t csr, uint32_t val)
{
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    const uint32_t out = *c;
    if (csr_is_writable(csr))
        *c = val;

    return out;
}

// perform csrrs (atomic read and set)
static uint32_t csr_csrrs(struct riscv_t *rv, uint32_t csr, uint32_t val)
{
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    const uint32_t out = *c;
    if (csr_is_writable(csr))
        *c |= val;

    return out;
}

// perform csrrc (atomic read and clear)
static uint32_t csr_csrrc(struct riscv_t *rv, uint32_t csr, uint32_t val)
{
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    const uint32_t out = *c;
    if (csr_is_writable(csr))
        *c &= ~val;
    return out;
}

static bool op_system(struct riscv_t *rv, uint32_t inst)
{
    // i-type decode
    const int32_t imm = dec_itype_imm(inst);
    const int32_t csr = dec_csr(inst);
    const uint32_t funct3 = dec_funct3(inst);
    const uint32_t rs1 = dec_rs1(inst);
    const uint32_t rd = dec_rd(inst);

    // dispatch by func3 field
    switch (funct3) {
    case 0:
        // dispatch from imm field
        switch (imm) {
        case 0:  // ECALL
            rv->io.on_ecall(rv);
            break;
        case 1:  // EBREAK
            rv->io.on_ebreak(rv);
            break;
        case 0x002:  // URET
        case 0x102:  // SRET
        case 0x202:  // HRET
        case 0x105:  // WFI
            rv_except_illegal_inst(rv);
            return false;
        case 0x302:  // MRET
            rv->PC = rv->csr_mepc;
            // this is a branch
            return false;
        default:
            rv_except_illegal_inst(rv);
            return false;
        }
        break;
#ifdef ENABLE_Zicsr
    case 1: {  // CSRRW    (Atomic Read/Write CSR)
        uint32_t tmp = csr_csrrw(rv, csr, rv->X[rs1]);
        rv->X[rd] = rd ? tmp : rv->X[rd];
        break;
    }
    case 2: {  // CSRRS    (Atomic Read and Set Bits in CSR)
        uint32_t tmp =
            csr_csrrs(rv, csr, (rs1 == rv_reg_zero) ? 0u : rv->X[rs1]);
        rv->X[rd] = rd ? tmp : rv->X[rd];
        break;
    }
    case 3: {  // CSRRC    (Atomic Read and Clear Bits in CSR)
        uint32_t tmp =
            csr_csrrc(rv, csr, (rs1 == rv_reg_zero) ? ~0u : rv->X[rs1]);
        rv->X[rd] = rd ? tmp : rv->X[rd];
        break;
    }
    case 5: {  // CSRRWI
        uint32_t tmp = csr_csrrc(rv, csr, rv->X[rs1]);
        rv->X[rd] = rd ? tmp : rv->X[rd];
        break;
    }
    case 6: {  // CSRRSI
        uint32_t tmp = csr_csrrs(rv, csr, rs1);
        rv->X[rd] = rd ? tmp : rv->X[rd];
        break;
    }
    case 7: {  // CSRRCI
        uint32_t tmp = csr_csrrc(rv, csr, rs1);
        rv->X[rd] = rd ? tmp : rv->X[rd];
        break;
    }
#endif  // ENABLE_Zicsr
    default:
        rv_except_illegal_inst(rv);
        return false;
    }

    // step over instruction
    rv->PC += rv->inst_len;

    // enforce zero register
    if (rd == rv_reg_zero)
        rv->X[rv_reg_zero] = 0;
    return true;
}

#ifdef ENABLE_RV32A
static bool op_amo(struct riscv_t *rv, uint32_t inst)
{
    const uint32_t rd = dec_rd(inst);
    const uint32_t rs1 = dec_rs1(inst);
    const uint32_t rs2 = dec_rs2(inst);
    const uint32_t f7 = dec_funct7(inst);
    const uint32_t funct5 = (f7 >> 2) & 0x1f;

    switch (funct5) {
    case 0b00010:  // LR.W
        rv->X[rd] = rv->io.mem_read_w(rv, rv->X[rs1]);
        // skip registration of the 'reservation set'
        // FIXME: uimplemented
        break;
    case 0b00011:  // SC.W
        // we assume the 'reservation set' is valid
        // FIXME: unimplemented
        rv->io.mem_write_w(rv, rv->X[rs1], rv->X[rs2]);
        rv->X[rd] = 0;
        break;
    case 0b00001: {  // AMOSWAP.W
        rv->X[rd] = rv->io.mem_read_w(rv, rs1);
        rv->io.mem_write_s(rv, rs1, rv->X[rs2]);
        break;
    }
    case 0b00000: {  // AMOADD.W
        rv->X[rd] = rv->io.mem_read_w(rv, rs1);
        const int32_t res = (int32_t) rv->X[rd] + (int32_t) rv->X[rs2];
        rv->io.mem_write_s(rv, rs1, res);
        break;
    }
    case 0b00100: {  // AMOXOR.W
        rv->X[rd] = rv->io.mem_read_w(rv, rs1);
        const int32_t res = rv->X[rd] ^ rv->X[rs2];
        rv->io.mem_write_s(rv, rs1, res);
        break;
    }
    case 0b01100: {  // AMOAND.W
        rv->X[rd] = rv->io.mem_read_w(rv, rs1);
        const int32_t res = rv->X[rd] & rv->X[rs2];
        rv->io.mem_write_s(rv, rs1, res);
        break;
    }
    case 0b01000: {  // AMOOR.W
        rv->X[rd] = rv->io.mem_read_w(rv, rs1);
        const int32_t res = rv->X[rd] | rv->X[rs2];
        rv->io.mem_write_s(rv, rs1, res);
        break;
    }
    case 0b10000: {  // AMOMIN.W
        rv->X[rd] = rv->io.mem_read_w(rv, rs1);
        const int32_t a = rv->X[rd];
        const int32_t b = rv->X[rs2];
        const int32_t res = a < b ? a : b;
        rv->io.mem_write_s(rv, rs1, res);
        break;
    }
    case 0b10100: {  // AMOMAX.W
        rv->X[rd] = rv->io.mem_read_w(rv, rs1);
        const int32_t a = rv->X[rd];
        const int32_t b = rv->X[rs2];
        const int32_t res = a > b ? a : b;
        rv->io.mem_write_s(rv, rs1, res);
        break;
    }
    case 0b11000: {  // AMOMINU.W
        rv->X[rd] = rv->io.mem_read_w(rv, rs1);
        const uint32_t a = rv->X[rd];
        const uint32_t b = rv->X[rs2];
        const uint32_t res = a < b ? a : b;
        rv->io.mem_write_s(rv, rs1, res);
        break;
    }
    case 0b11100: {  // AMOMAXU.W
        rv->X[rd] = rv->io.mem_read_w(rv, rs1);
        const uint32_t a = rv->X[rd];
        const uint32_t b = rv->X[rs2];
        const uint32_t res = a > b ? a : b;
        rv->io.mem_write_s(rv, rs1, res);
        break;
    }
    default:
        rv_except_illegal_inst(rv);
        return false;
    }

    // step over instruction
    rv->PC += 4;

    // enforce zero register
    if (rd == rv_reg_zero)
        rv->X[rv_reg_zero] = 0;
    return true;
}
#else
#define op_amo NULL
#endif  // ENABLE_RV32A

/* No RV32F support */
#define op_load_fp NULL
#define op_store_fp NULL
#define op_fp NULL
#define op_madd NULL
#define op_msub NULL
#define op_nmsub NULL
#define op_nmadd NULL

#ifdef ENABLE_RV32C
static bool c_op_addi(struct riscv_t *rv, uint16_t inst)
{
    debug_print("Entered c.addi/nop");

    uint16_t tmp =
        (uint16_t)(((inst & FCI_IMM_12) >> 5) | (inst & FCI_IMM_6_2)) >> 2;
    const int32_t imm = (0x20 & tmp) ? 0xffffffc0 | tmp : tmp;
    const uint16_t rd = c_dec_rd(inst);

    // dispatch operation type
    if (rd != 0) {
        // C.ADDI
        rv->X[rd] += imm;
    } else {
        // C.NOP
    }

    // step over instruction
    rv->PC += rv->inst_len;
    // enforce zero register
    if (rd == rv_reg_zero)
        rv->X[rv_reg_zero] = 0;
    return true;
}

static bool c_op_sw(struct riscv_t *rv, uint16_t inst)
{
    // const uint16_t imm =
    //     (inst & FC_IMM_12_10) >> 7 | (inst & 0x20) >> 4 | (inst & 0x10) << 1;
    // const uint16_t rs1 = c_dec_rs1c(inst);
    // const uint16_t rs2 = c_dec_rs2c(inst);
    // printf("%x, %x, %x\n", imm, rs1, rs2);


    rv->PC += rv->inst_len;
    return true;
}

static bool c_op_addi4spn(struct riscv_t *rv, uint16_t inst)
{
    debug_print("Entered c.addi4spn");
    uint16_t temp = 0;
    temp |= (inst & 0x1800) >> 7;
    temp |= (inst & 0x780) >> 1;
    temp |= (inst & 0x40) >> 4;
    temp |= (inst & 0x20) >> 2;

    const uint16_t imm = temp;
    const uint16_t rd = c_dec_rdc(inst) | 0x08;
    rv->X[rd] = rv->X[2] + imm;

    rv->PC += rv->inst_len;
    return true;
}

static bool c_op_fsd(struct riscv_t *rv, uint16_t inst)
{
    rv->PC += rv->inst_len;
    return true;
}

static bool c_op_fsw(struct riscv_t *rv, uint16_t inst)
{
    rv->PC += rv->inst_len;
    return true;
}

static bool c_op_li(struct riscv_t *rv, uint16_t inst)
{
    debug_print("Entered c.li");

    uint16_t tmp = (uint16_t)((inst & 0x1000) >> 7 | (inst & 0x7c) >> 2);
    const int32_t imm = (tmp & 0x20) ? 0xffffffc0 | tmp : tmp;
    const uint16_t rd = c_dec_rd(inst);
    rv->X[rd] = imm;

    rv->PC += rv->inst_len;
    return true;
}

static bool c_op_lui(struct riscv_t *rv, uint16_t inst)
{
    debug_print("Entered c.lui/addi16sp");
    const uint16_t rd = c_dec_rd(inst);
    /* TODO */
    if (rd == 2) {
        // C.ADDI16SP
        uint32_t tmp = (inst & 0x1000) >> 3;
        tmp |= (inst & 0x40);
        tmp |= (inst & 0x20) << 1;
        tmp |= (inst & 0x18) << 4;
        tmp |= (inst & 0x4) << 3;
        const int32_t imm = (tmp & 0x200) ? 0xfffffc | tmp : tmp;
        if (imm == 0)
            assert(!"Should not be zero.");
        rv->X[rd] += imm;
    } else if (rd != 0) {
        // C.LUI
        uint32_t tmp = (inst & 0x1000) << 5 | (inst & 0x7c) << 12;
        const int32_t imm = (tmp & 0x20000) ? 0xfffc0000 | tmp : tmp;
        if (imm == 0)
            assert(!"Should not be zero.");
        rv->X[rd] = imm;
    } else {
        assert(!"Should be unreachbale.");
    }

    rv->PC += rv->inst_len;
    return true;
}

static bool c_op_misc_alu(struct riscv_t *rv, uint16_t inst)
{
    rv->PC += rv->inst_len;
    return true;
}

static bool c_op_slli(struct riscv_t *rv, uint16_t inst)
{
    rv->PC += rv->inst_len;
    return true;
}

// CI-type
static bool c_op_lwsp(struct riscv_t *rv, uint16_t inst)
{
    debug_print("Entered c.lwsp");

    uint16_t temp = 0;
    temp |= ((inst & FCI_IMM_6_2) | 0b1110000) >> 2;
    temp |= (inst & FCI_IMM_12) >> 7;
    temp |= ((inst & FCI_IMM_6_2) | 0b0001100) << 4;

    const uint16_t imm = temp;
    const uint16_t rd = c_dec_rd(inst);
    const uint16_t addr = rv->X[2] + imm;

    if (addr & 3) {
        rv_except_load_misaligned(rv, addr);
        return false;
    }
    rv->X[rd] = rv->io.mem_read_w(rv, addr);

    rv->PC += rv->inst_len;
    return true;
}

// CSS-type
static bool c_op_swsp(struct riscv_t *rv, uint16_t inst)
{
    debug_print("Entered c.swsp");

    const uint16_t imm = (inst & 0x1e00) >> 7 | (inst & 0x180) >> 1;
    const uint16_t rs2 = c_dec_rs2(inst);
    const uint32_t addr = rv->X[2] + imm;
    const uint32_t data = rv->X[rs2];

    if (addr & 3) {
        rv_except_store_misaligned(rv, addr);
        return false;
    }
    rv->io.mem_write_w(rv, addr, data);

    rv->PC += rv->inst_len;
    return true;
}

// CL-type
static bool c_op_lw(struct riscv_t *rv, uint16_t inst)
{
    debug_print("Entered c.lw");

    uint16_t temp = 0;
    temp |= (inst & 0b0000000001000000) >> 4;
    temp |= (inst & FC_IMM_12_10) >> 7;
    temp |= (inst & 0b0000000000100000) << 1;

    const uint16_t imm = temp;
    const uint16_t rd = c_dec_rdc(inst) | 0x08;
    const uint16_t rs1 = c_dec_rs1c(inst) | 0x08;
    const uint32_t addr = rv->X[rs1] + imm;

    if (addr & 3) {
        rv_except_load_misaligned(rv, addr);
        return false;
    }
    rv->X[rd] = rv->io.mem_read_w(rv, addr);

    rv->PC += rv->inst_len;
    return true;
}

// CJ-type
static bool c_op_j(struct riscv_t *rv, uint16_t inst)
{
    debug_print("Entered c.j");

    const uint32_t imm = sign_extend_h(c_dec_cjtype_imm(inst));
    rv->PC += imm;
    return true;
}

static bool c_op_jal(struct riscv_t *rv, uint16_t inst)
{
    debug_print("Entered c.jal");

    const uint32_t imm = sign_extend_h(c_dec_cjtype_imm(inst));
    rv->X[1] = rv->PC + 2;
    rv->PC += imm;
    return true;
}

// CR-type
static bool c_op_cr(struct riscv_t *rv, uint16_t inst)
{
    const rs1 = c_dec_rs1(inst);

    switch ((inst & 0x1000) >> 12) {
    case 0:  // c.jr
        debug_print("Entered c.jr");
        rv->PC = rv->X[rs1];
        break;
    case 1:  // c.jalr
        debug_print("Entered c.jalr");
        rv->X[1] = rv->PC + 2;
        rv->PC = rv->X[rs1];
        break;
    default:
        assert(!"Should be unreachbale.");
        break;
    }

    if (rv->PC & 1) {
        rv_except_inst_misaligned(rv, rv->PC);
        return false;
    }

    return true;
}

// CB-type
static bool c_op_beqz(struct riscv_t *rv, uint16_t inst)
{
    debug_print("Entered c.beqz");

    const uint32_t imm = sign_extend_h(c_dec_cbtype_imm(inst));
    const uint32_t rs1 = c_dec_rs1c(inst) | 0x08;

    if(!rv->X[rs1]){
        rv->PC += imm;
    }
    else{
        rv->PC += rv->inst_len;
    }

    return true;
}

static bool c_op_bnez(struct riscv_t *rv, uint16_t inst)
{
    debug_print("Entered c.bnez");

    const uint32_t imm = sign_extend_h(c_dec_cbtype_imm(inst));
    const uint32_t rs1 = c_dec_rs1c(inst) | 0x08;

    if(rv->X[rs1]){
        rv->PC += imm;
    }
    else{
        rv->PC += rv->inst_len;
    }

    return true;
}
#else
#define c_op_addi4spn NULL
#define c_op_addi NULL
#define c_op_swsp NULL
#define c_op_li NULL
#define c_op_slli NULL
#define c_op_jal NULL
#define c_op_lw NULL
#define c_op_lwsp NULL
#define c_op_lui NULL
#define c_op_misc_alu NULL
#define c_op_jalr NULL
#define c_op_fsd NULL
#define c_op_j NULL
#define c_op_beqz NULL
#define c_op_fsw NULL
#define c_op_bnez NULL
#define c_op_sw NULL
#endif  // ENABLE_RV32C

/* No RV32C.F support */
#define c_op_fldsp NULL
#define c_op_flwsp NULL
#define c_op_fswsp NULL
#define c_op_fsdsp NULL
#define c_op_fld NULL
#define c_op_flw NULL

/* TODO: function implemetation and Test Correctness*/
// c_op_addi4spn    - done
// c_op_addi        - done
// c_op_swsp        - done
// c_op_li          - done
// c_op_slli NULL
// c_op_jal         - done
// c_op_lw          - done
// c_op_lwsp        - done
// c_op_lui NULL
// c_op_misc_alu NULL
// c_op_jalr NULL
// c_op_fsd NULL
// c_op_j           - done
// c_op_beqz NULL
// c_op_fsw NULL
// c_op_bnez NULL
// c_op_sw

// opcode handler type
typedef bool (*opcode_t)(struct riscv_t *rv, uint32_t inst);
typedef bool (*c_opcode_t)(struct riscv_t *rv, uint16_t inst);

// clang-format off
// opcode dispatch table
static const opcode_t opcodes[] = {
//  000        001          010       011          100        101       110   111
    op_load,   op_load_fp,  NULL,     op_misc_mem, op_op_imm, op_auipc, NULL, NULL, // 00
    op_store,  op_store_fp, NULL,     op_amo,      op_op,     op_lui,   NULL, NULL, // 01
    op_madd,   op_msub,     op_nmsub, op_nmadd,    op_fp,     NULL,     NULL, NULL, // 10
    op_branch, op_jalr,     NULL,     op_jal,      op_system, NULL,     NULL, NULL, // 11
};

// compressed opcode dispatch table
static const c_opcode_t c_opcodes[] = {
//  00              01              10          11         
    c_op_addi4spn,  c_op_addi,      c_op_slli,  NULL, // 000
    c_op_fld,       c_op_jal,       c_op_fldsp, NULL, // 001
    c_op_lw,        c_op_li,        c_op_lwsp,  NULL, // 010
    c_op_flw,       c_op_lui,       c_op_flwsp, NULL, // 011
    NULL,           c_op_misc_alu,  c_op_cr,    NULL, // 100
    c_op_fsd,       c_op_j,         c_op_fsdsp, NULL, // 101
    c_op_sw,        c_op_beqz,      c_op_swsp,  NULL, // 110
    c_op_fsw,       c_op_bnez,      c_op_fswsp, NULL, // 111
};
// clang-format on

void rv_step(struct riscv_t *rv, int32_t cycles)
{
    assert(rv);
    const uint64_t cycles_target = rv->csr_cycle + cycles;

    while (rv->csr_cycle < cycles_target && !rv->halt) {
        // fetch the next instruction
        const uint32_t inst = rv->io.mem_ifetch(rv, rv->PC);

        // Illegal instruction if inst[15:0] == 0
        assert(inst & 0xFFFF && "inst[15:0] must not be all 0.\n");

        // standard uncompressed instruction
        if ((inst & 3) == 3) {
            const uint32_t index = (inst & INST_6_2) >> 2;

            // dispatch this opcode
            const opcode_t op = opcodes[index];
            assert(op);
            rv->inst_len = INST_32;
            if (!op(rv, inst))
                break;

            // increment the cycles csr
            rv->csr_cycle++;
        } else {
            const uint16_t c_index =
                (inst & FC_FUNC3) >> 11 | (inst & FC_OPCODE);
            const c_opcode_t op = c_opcodes[c_index];

            // DEBUG: Print accepted c-instruction
            debug_print_hexval(rv->PC);
            debug_print_hexval(c_index);
            debug_print_hexval(inst);
            assert(op);
            rv->inst_len = INST_16;
            if (!op(rv, inst))
                break;

            // increment the cycles csr
            rv->csr_cycle++;

            // DEBUG: print register state
            print_register(rv);
        }
    }
}

riscv_user_t rv_userdata(struct riscv_t *rv)
{
    assert(rv);
    return rv->userdata;
}

bool rv_set_pc(struct riscv_t *rv, riscv_word_t pc)
{
    assert(rv);
    if (pc & 3)
        return false;

    rv->PC = pc;
    return true;
}

riscv_word_t rv_get_pc(struct riscv_t *rv)
{
    assert(rv);
    return rv->PC;
}

void rv_set_reg(struct riscv_t *rv, uint32_t reg, riscv_word_t in)
{
    assert(rv);
    if (reg < RV_NUM_REGS && reg != rv_reg_zero)
        rv->X[reg] = in;
}

riscv_word_t rv_get_reg(struct riscv_t *rv, uint32_t reg)
{
    assert(rv);
    if (reg < RV_NUM_REGS)
        return rv->X[reg];

    return ~0u;
}

struct riscv_t *rv_create(const struct riscv_io_t *io, riscv_user_t userdata)
{
    assert(io);
    struct riscv_t *rv = (struct riscv_t *) malloc(sizeof(struct riscv_t));
    memset(rv, 0, sizeof(struct riscv_t));

    // copy over the IO interface
    memcpy(&rv->io, io, sizeof(struct riscv_io_t));

    // copy over the userdata
    rv->userdata = userdata;

    // reset
    rv_reset(rv, 0u);

    return rv;
}

void rv_halt(struct riscv_t *rv)
{
    rv->halt = true;
}

bool rv_has_halted(struct riscv_t *rv)
{
    return rv->halt;
}

void rv_delete(struct riscv_t *rv)
{
    assert(rv);
    free(rv);
}

void rv_reset(struct riscv_t *rv, riscv_word_t pc)
{
    assert(rv);
    memset(rv->X, 0, sizeof(uint32_t) * RV_NUM_REGS);

    // set the reset address
    rv->PC = pc;
    rv->inst_len = INST_UNKNOWN;

    // set the default stack pointer
    rv->X[rv_reg_sp] = DEFAULT_STACK_ADDR;

    // reset the csrs
    rv->csr_cycle = 0;
    rv->csr_mstatus = 0;
    rv->halt = false;
}
