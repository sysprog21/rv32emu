#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "riscv.h"
#include "riscv_private.h"

static void rv_except_inst_misaligned(struct riscv_t *rv, uint32_t old_pc)
{
    // mtvec (Machine Trap-Vector Base Address Register)
    // mtvec[MXLEN-1:2]: vector base address
    // mtvec[1:0] : vector mode
    const uint32_t base = rv->csr_mtvec & ~0x3;
    const uint32_t mode = rv->csr_mtvec & 0x3;

    // Exception Code: Instruction Address Misaligned
    const uint32_t code = 0;

    // mepc  (Machine Exception Program Counter)
    // mtval (Machine Trap Value Register) : Misaligned Instruction
    rv->csr_mepc = old_pc;
    rv->csr_mtval = rv->PC;

    switch (mode) {
    case 0:  // DIRECT: All exceptions set PC to base
        rv->PC = base;
        break;
    case 1:  // VECTORED: Asynchronous interrupts set PC to base + 4 * code
        rv->PC = base + 4 * code;
        break;
    }

    // mcause (Machine Cause Register): store exception code
    rv->csr_mcause = code;
}

static void rv_except_load_misaligned(struct riscv_t *rv, uint32_t addr)
{
    // mtvec (Machine Trap-Vector Base Address Register)
    // mtvec[MXLEN-1:2]: vector base address
    // mtvec[1:0] : vector mode
    const uint32_t base = rv->csr_mtvec & ~0x3;
    const uint32_t mode = rv->csr_mtvec & 0x3;

    // Exception Code: Load Address Misaligned
    const uint32_t code = 4;

    // mepc (Machine Exception Program Counter)
    // mtval(Machine Trap Value Register) : Misaligned Load Address
    rv->csr_mepc = rv->PC;
    rv->csr_mtval = addr;

    switch (mode) {
    case 0:  // DIRECT: All exceptions set PC to base
        rv->PC = base;
        break;
    case 1:  // VECTORED: Asynchronous interrupts set PC to base + 4 * code
        rv->PC = base + 4 * code;
        break;
    }

    // mcause (Machine Cause Register): store exception code
    rv->csr_mcause = code;
}

static void rv_except_store_misaligned(struct riscv_t *rv, uint32_t addr)
{
    // mtvec (Machine Trap-Vector Base Address Register)
    // mtvec[MXLEN-1:2]: vector base address
    // mtvec[1:0] : vector mode
    const uint32_t base = rv->csr_mtvec & ~0x3;
    const uint32_t mode = rv->csr_mtvec & 0x3;

    // Exception Code: Store Address Misaligned
    const uint32_t code = 6;

    // mepc (Machine Exception Program Counter)
    // mtval(Machine Trap Value Register) : Misaligned Store Address
    rv->csr_mepc = rv->PC;
    rv->csr_mtval = addr;

    switch (mode) {
    case 0:  // DIRECT: All exceptions set PC to base
        rv->PC = base;
        break;
    case 1:  // VECTORED: Asynchronous interrupts set PC to base + 4 * code
        rv->PC = base + 4 * code;
        break;
    }

    // mcause (Machine Cause Register): store exception code
    rv->csr_mcause = code;
}

static void rv_except_illegal_inst(struct riscv_t *rv, uint32_t inst)
{
    // mtvec (Machine Trap-Vector Base Address Register)
    // mtvec[MXLEN-1:2]: vector base address
    // mtvec[1:0] : vector mode
    const uint32_t base = rv->csr_mtvec & ~0x3;
    const uint32_t mode = rv->csr_mtvec & 0x3;

    // Exception Code: Illegal Instruction
    const uint32_t code = 2;

    // mepc (Machine Exception Program Counter)
    // mtval(Machine Trap Value Register) : Illegal Instruction
    rv->csr_mepc = rv->PC;
    rv->csr_mtval = inst;

    switch (mode) {
    case 0:  // DIRECT: All exceptions set PC to base
        rv->PC = base;
        break;
    case 1:  // VECTORED: Asynchronous interrupts set PC to base + 4 * code
        rv->PC = base + 4 * code;
        break;
    }

    // mcause (Machine Cause Register): store exception code
    rv->csr_mcause = code;
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
        rv_except_illegal_inst(rv, inst);
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
#define op_misc_mem OP_UNIMP
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
        rv->X[rd] = (int32_t) (rv->X[rs1]) + imm;
        break;
    case 1:  // SLLI
        rv->X[rd] = rv->X[rs1] << (imm & 0x1f);
        break;
    case 2:  // SLTI
        rv->X[rd] = ((int32_t) (rv->X[rs1]) < imm) ? 1 : 0;
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
        rv_except_illegal_inst(rv, inst);
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
        rv_except_illegal_inst(rv, inst);
        return false;
    }

    // step over instruction
    rv->PC += 4;
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
            rv->X[rd] = (int32_t) (rv->X[rs1]) + (int32_t) (rv->X[rs2]);
            break;
        case 0b001:  // SLL
            rv->X[rd] = rv->X[rs1] << (rv->X[rs2] & 0x1f);
            break;
        case 0b010:  // SLT
            rv->X[rd] =
                ((int32_t) (rv->X[rs1]) < (int32_t) (rv->X[rs2])) ? 1 : 0;
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
            rv_except_illegal_inst(rv, inst);
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
            rv->X[rd] = ((uint64_t) (a * b)) >> 32;
        } break;
        case 0b010: {  // MULHSU
            const int64_t a = (int32_t) rv->X[rs1];
            const uint64_t b = rv->X[rs2];
            rv->X[rd] = ((uint64_t) (a * b)) >> 32;
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
            rv_except_illegal_inst(rv, inst);
            return false;
        }
        break;
#endif  // ENABLE_RV32M
    case 0b0100000:
        switch (funct3) {
        case 0b000:  // SUB
            rv->X[rd] = (int32_t) (rv->X[rs1]) - (int32_t) (rv->X[rs2]);
            break;
        case 0b101:  // SRA
            rv->X[rd] = ((int32_t) rv->X[rs1]) >> (rv->X[rs2] & 0x1f);
            break;
        default:
            rv_except_illegal_inst(rv, inst);
            return false;
        }
        break;
    default:
        rv_except_illegal_inst(rv, inst);
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
        rv_except_illegal_inst(rv, inst);
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
#ifdef ENABLE_RV32C
    if (rv->PC & 0x1) {
#else
    if (rv->PC & 0x3) {
#endif
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

#ifdef ENABLE_RV32C
    if (rv->PC & 0x1) {
#else
    if (rv->PC & 0x3) {
#endif
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
            rv_except_illegal_inst(rv, inst);
            return false;
        case 0x302:  // MRET
            rv->PC = rv->csr_mepc;
            // this is a branch
            return false;
        default:
            rv_except_illegal_inst(rv, inst);
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
        uint32_t tmp = csr_csrrw(rv, csr, rs1);
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
        rv_except_illegal_inst(rv, inst);
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
        rv_except_illegal_inst(rv, inst);
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
#define op_amo OP_UNIMP
#endif  // ENABLE_RV32A

/* No RV32F support */
#define op_load_fp OP_UNIMP
#define op_store_fp OP_UNIMP
#define op_fp OP_UNIMP
#define op_madd OP_UNIMP
#define op_msub OP_UNIMP
#define op_nmsub OP_UNIMP
#define op_nmadd OP_UNIMP


#ifdef ENABLE_RV32C
static bool op_caddi(struct riscv_t *rv, uint16_t inst)
{
    uint16_t tmp =
        (uint16_t) (((inst & FCI_IMM_12) >> 5) | (inst & FCI_IMM_6_2)) >> 2;
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

static bool op_caddi4spn(struct riscv_t *rv, uint16_t inst)
{
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

static bool op_cli(struct riscv_t *rv, uint16_t inst)
{
    uint16_t tmp = (uint16_t) ((inst & 0x1000) >> 7 | (inst & 0x7c) >> 2);
    const int32_t imm = (tmp & 0x20) ? 0xffffffc0 | tmp : tmp;
    const uint16_t rd = c_dec_rd(inst);
    rv->X[rd] = imm;

    rv->PC += rv->inst_len;
    return true;
}

static bool op_clui(struct riscv_t *rv, uint16_t inst)
{
    const uint16_t rd = c_dec_rd(inst);
    if (rd == 2) {
        // C.ADDI16SP
        uint32_t tmp = (inst & 0x1000) >> 3;
        tmp |= (inst & 0x40) >> 2;
        tmp |= (inst & 0x20) << 1;
        tmp |= (inst & 0x18) << 4;
        tmp |= (inst & 0x4) << 3;
        const uint32_t imm = (tmp & 0x200) ? (0xfffffc00 | tmp) : tmp;

        if (imm != 0)
            rv->X[rd] += imm;
        else {  // Code point: nzimm == 0 is reserved
        }
    } else if (rd != 0) {
        // C.LUI
        uint32_t tmp = (inst & 0x1000) << 5 | (inst & 0x7c) << 10;
        const int32_t imm = (tmp & 0x20000) ? (0xfffc0000 | tmp) : tmp;
        if (imm != 0)
            rv->X[rd] = imm;
        else {  // Code point 1: nzimm == 0 is reserved
        }
    } else {
        // Code point 2: rd==x0 is HINTS
    }

    rv->PC += rv->inst_len;
    return true;
}

static bool op_csrli(struct riscv_t *rv, uint16_t inst)
{
    uint32_t temp = 0;
    temp |= (inst & 0x1000) >> 7;
    temp |= (inst & 0x007C) >> 2;

    const uint32_t shamt = temp;
    const uint32_t rs1 = c_dec_rs1c(inst) | 0x08;

    // Code point 1: shamt[t]==1 are reserved
    if (shamt & 0x20)
        return true;
    // Code point 2: rd == x0 is HINTS
    if (rs1 == 0)
        return true;
    // Code point 3: shamt == 0 is HINT
    if (shamt == 0)
        return true;

    rv->X[rs1] >>= shamt;

    return true;
}

static bool op_csrai(struct riscv_t *rv, uint16_t inst)
{
    uint32_t temp = 0;
    temp |= (inst & 0x1000) >> 7;
    temp |= (inst & 0x007C) >> 2;

    const uint32_t shamt = temp;
    const uint32_t rs1 = c_dec_rs1c(inst) | 0x08;

    // Code point 1: shamt[5] == 1 is reserved
    if (shamt & 0x20)
        return true;
    // Code point 2: shame == 0 is HINT
    if (shamt == 0)
        return true;
    // Code point 3: rs1 == x0 is HINT
    if (rs1 == rv_reg_zero)
        return true;

    const uint32_t mask = 0x80000000 & rv->X[rs1];
    rv->X[rs1] >>= shamt;

    for (unsigned int i = 0; i < shamt; ++i) {
        rv->X[rs1] |= mask >> i;
    }

    return true;
}

static bool op_candi(struct riscv_t *rv, uint16_t inst)
{
    const uint16_t mask = (0x1000 & inst) << 3;

    uint16_t temp = 0;
    for (int i = 0; i <= 10; ++i) {
        temp |= (mask >> i);
    }
    temp |= (inst & 0x007C) >> 2;

    const uint32_t imm = sign_extend_h(temp);
    const uint32_t rs1 = c_dec_rs1c(inst) | 0x08;

    rv->X[rs1] &= imm;

    return true;
}

static bool op_cmisc_alu(struct riscv_t *rv, uint16_t inst)
{
    // Find actual instruction
    switch ((inst & 0x0C00) >> 10) {
    case 0:  // C.SRLI
        op_csrli(rv, inst);
        break;
    case 1:  // C.SRAI
        op_csrai(rv, inst);
        break;
    case 2:  // C.ANDI
        op_candi(rv, inst);
        break;
    case 3:;  // Arithmistic
        uint32_t temp = 0;
        temp |= (inst & 0x1000) >> 10;
        temp |= (inst & 0x0060) >> 5;

        const uint32_t funct = temp;
        const uint32_t rs1 = c_dec_rs1c(inst) | 0x08;
        const uint32_t rs2 = c_dec_rs2c(inst) | 0x08;
        const uint32_t rd = rs1;

        switch (funct) {
        case 0:  // SUB
            rv->X[rd] = rv->X[rs1] - rv->X[rs2];
            break;
        case 1:  // XOR
            rv->X[rd] = rv->X[rs1] ^ rv->X[rs2];
            break;
        case 2:  // OR
            rv->X[rd] = rv->X[rs1] | rv->X[rs2];
            break;
        case 3:  // AND
            rv->X[rd] = rv->X[rs1] & rv->X[rs2];
            break;
        case 4:
        case 5:
            assert(!"RV64/128C instructions");
            break;
        case 6:
        case 7:
            assert(!"Instruction preserved");
            break;
        default:
            assert(!"Should not be reachable");
            break;
        }
        break;
    default:
        assert(!"Should not be reachable");
        break;
    }

    rv->PC += rv->inst_len;
    return true;
}

static bool op_cslli(struct riscv_t *rv, uint16_t inst)
{
    uint32_t temp = 0;
    temp |= (inst & FCI_IMM_12) >> 7;
    temp |= (inst & FCI_IMM_6_2) >> 2;

    const uint32_t shamt = temp;
    const uint32_t rd = c_dec_rd(inst);

    if (rd) {
        rv->X[rd] <<= shamt;
    }

    rv->PC += rv->inst_len;
    return true;
}

// CI-type
static bool op_clwsp(struct riscv_t *rv, uint16_t inst)
{
    uint16_t temp = 0;
    temp |= (inst & 0x70) >> 2;
    temp |= (inst & 0x0c) << 4;
    temp |= (inst & 0x1000) >> 7;

    const uint16_t imm = temp;
    const uint16_t rd = c_dec_rd(inst);
    const uint32_t addr = rv->X[rv_reg_sp] + imm;

    // reserved for rd == 0
    if (rd == 0)
        return true;

    if (addr & 3) {
        rv_except_load_misaligned(rv, addr);
        return false;
    }

    rv->X[rd] = rv->io.mem_read_w(rv, addr);
    rv->PC += rv->inst_len;
    return true;
}

// CSS-type
static bool op_cswsp(struct riscv_t *rv, uint16_t inst)
{
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
static bool op_clw(struct riscv_t *rv, uint16_t inst)
{
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

// CS-type
static bool op_csw(struct riscv_t *rv, uint16_t inst)
{
    uint32_t temp = 0;
    //                ....xxxx....xxxx
    temp |= (inst & 0b0000000001000000) >> 4;
    temp |= (inst & FC_IMM_12_10) >> 7;
    temp |= (inst & 0b0000000000100000) << 1;

    const uint32_t imm = temp;
    const uint32_t rs1 = c_dec_rs1c(inst) | 0x08;
    const uint32_t rs2 = c_dec_rs2c(inst) | 0x08;
    const uint32_t addr = rv->X[rs1] + imm;
    const uint32_t data = rv->X[rs2];

    if (addr & 3) {
        rv_except_store_misaligned(rv, addr);
        return false;
    }
    rv->io.mem_write_w(rv, addr, data);

    rv->PC += rv->inst_len;
    return true;
}

// CJ-type
static bool op_cj(struct riscv_t *rv, uint16_t inst)
{
    const int32_t imm = (c_dec_cjtype_imm(inst));
    rv->PC += imm;
    if (rv->PC & 0x1) {
        rv_except_inst_misaligned(rv, rv->PC);
        return false;
    }
    // can branch
    return false;
}

static bool op_cjal(struct riscv_t *rv, uint16_t inst)
{
    const int32_t imm = sign_extend_h(c_dec_cjtype_imm(inst));
    rv->X[1] = rv->PC + 2;
    rv->PC += imm;
    if (rv->PC & 0x1) {
        rv_except_inst_misaligned(rv, rv->PC);
        return false;
    }
    // can branch
    return false;
}

// CR-type
static bool op_ccr(struct riscv_t *rv, uint16_t inst)
{
    const uint32_t rs1 = c_dec_rs1(inst);
    const uint32_t rs2 = c_dec_rs2(inst);
    const uint32_t rd = rs1;

    switch ((inst & 0x1000) >> 12) {
    case 0:
        if (rs2) {
            // C.MV
            rv->X[rd] = rv->X[rs2];
            rv->PC += rv->inst_len;
            if (rd == rv_reg_zero)
                rv->X[rv_reg_zero] = 0;
        } else {
            // C.JR
            rv->PC = rv->X[rs1];
            return false;
        }
        break;
    case 1:
        if (rs1 == 0 && rs2 == 0)  // C.EBREAK
            rv->io.on_ebreak(rv);
        else if (rs1 && rs2) {  // C.ADD
            rv->X[rd] = rv->X[rs1] + rv->X[rs2];
            rv->PC += rv->inst_len;
            if (rd == rv_reg_zero)
                rv->X[rv_reg_zero] = 0;
        } else if (rs1 && rs2 == 0) {  // rs1 != zero AND rs2 == zero
            // C.JALR
            // Unconditional jump and store PC+2 to ra
            const int32_t jump_to = rv->X[rs1];
            rv->X[rv_reg_ra] = rv->PC + rv->inst_len;
            rv->PC = jump_to;
            if (rv->PC & 0x1) {
                rv_except_inst_misaligned(rv, rv->PC);
                return false;
            }
            // can branch
            return false;
        } else                       // rs2 !=zero AND rd == zero
            rv->PC += rv->inst_len;  // Hint
        break;
    default:
        assert(!"Should be unreachable.");
        break;
    }

    return true;
}

// CB-type
static bool op_cbeqz(struct riscv_t *rv, uint16_t inst)
{
    const uint32_t imm = sign_extend_h(c_dec_cbtype_imm(inst));
    const uint32_t rs1 = c_dec_rs1c(inst) | 0x08;
    rv->PC += (!rv->X[rs1]) ? imm : rv->inst_len;
    // can branch
    return false;
}

static bool op_cbnez(struct riscv_t *rv, uint16_t inst)
{
    const uint32_t imm = sign_extend_h(c_dec_cbtype_imm(inst));
    const uint32_t rs1 = c_dec_rs1c(inst) | 0x08;
    rv->PC += (rv->X[rs1]) ? imm : rv->inst_len;
    // can branch
    return false;
}
#else
#define op_caddi4spn OP_UNIMP
#define op_caddi OP_UNIMP
#define op_cswsp OP_UNIMP
#define op_cli OP_UNIMP
#define op_cslli OP_UNIMP
#define op_cjal OP_UNIMP
#define op_clw OP_UNIMP
#define op_clwsp OP_UNIMP
#define op_clui OP_UNIMP
#define op_cmisc_alu OP_UNIMP
#define op_cjalr OP_UNIMP
#define op_cj OP_UNIMP
#define op_cbeqz OP_UNIMP
#define op_cbnez OP_UNIMP
#define op_csw OP_UNIMP
#endif  // ENABLE_RV32C

/* No RV32C.F support */
#define op_cfldsp OP_UNIMP
#define op_cflwsp OP_UNIMP
#define op_cfswsp OP_UNIMP
#define op_cfsdsp OP_UNIMP
#define op_cfld OP_UNIMP
#define op_cflw OP_UNIMP
#define op_cfsw OP_UNIMP
#define op_cfsd OP_UNIMP

// RV32 opcode handler type
typedef bool (*opcode_t)(struct riscv_t *rv, uint32_t inst);
// RV32C opcode handler type
typedef bool (*c_opcode_t)(struct riscv_t *rv, uint16_t inst);

// handler for all unimplemented opcodes
static bool op_unimp(struct riscv_t *rv, uint32_t inst UNUSED)
{
    rv_except_illegal_inst(rv, inst);
    return false;
}

void rv_step(struct riscv_t *rv, int32_t cycles)
{
    assert(rv);
    const uint64_t cycles_target = rv->csr_cycle + cycles;
    uint32_t inst;

#define OP_UNIMP op_unimp
#ifdef ENABLE_COMPUTED_GOTO
#define OP(instr) &&op_##instr
#define TABLE_TYPE const void *
#define TABLE_TYPE_RVC const void *
#else  // ENABLE_COMPUTED_GOTO = false
#define OP(instr) op_##instr
#define TABLE_TYPE const opcode_t
#define TABLE_TYPE_RVC const c_opcode_t
#endif

    // clang-format off
    TABLE_TYPE jump_table[] = {
    //  000         001           010        011           100         101        110   111
        OP(load),   OP(load_fp),  OP(unimp), OP(misc_mem), OP(op_imm), OP(auipc), OP(unimp), OP(unimp), // 00
        OP(store),  OP(store_fp), OP(unimp), OP(amo),      OP(op),     OP(lui),   OP(unimp), OP(unimp), // 01
        OP(madd),   OP(msub),     OP(nmsub), OP(nmadd),    OP(fp),     OP(unimp), OP(unimp), OP(unimp), // 10
        OP(branch), OP(jalr),     OP(unimp), OP(jal),      OP(system), OP(unimp), OP(unimp), OP(unimp), // 11
    };
#ifdef ENABLE_RV32C
    TABLE_TYPE_RVC jump_table_rvc[] = {
    //  00             01             10          11
        OP(caddi4spn), OP(caddi),     OP(cslli),  OP(unimp),  // 000
        OP(cfld),      OP(cjal),      OP(cfldsp), OP(unimp),  // 001
        OP(clw),       OP(cli),       OP(clwsp),  OP(unimp),  // 010
        OP(cflw),      OP(clui),      OP(cflwsp), OP(unimp),  // 011
        OP(unimp),     OP(cmisc_alu), OP(ccr),    OP(unimp),  // 100
        OP(cfsd),      OP(cj),        OP(cfsdsp), OP(unimp),  // 101
        OP(csw),       OP(cbeqz),     OP(cswsp),  OP(unimp),  // 110
        OP(cfsw),      OP(cbnez),     OP(cfswsp), OP(unimp),  // 111
    };
#endif
    // clang-format on

#ifdef ENABLE_COMPUTED_GOTO
#ifdef ENABLE_RV32C
#define DISPATCH_RV32C()                                            \
    inst &= 0x0000FFFF;                                             \
    int16_t c_index = (inst & FC_FUNC3) >> 11 | (inst & FC_OPCODE); \
    rv->inst_len = INST_16;                                         \
    goto *jump_table_rvc[c_index];
#else
#define DISPATCH_RV32C()
#endif

#define DISPATCH()                                      \
    {                                                   \
        if (rv->csr_cycle >= cycles_target || rv->halt) \
            goto quit;                                  \
        /* fetch the next instruction */                \
        inst = rv->io.mem_ifetch(rv, rv->PC);           \
        /* standard uncompressed instruction */         \
        if ((inst & 3) == 3) {                          \
            uint32_t index = (inst & INST_6_2) >> 2;    \
            rv->inst_len = INST_32;                     \
            goto *jump_table[index];                    \
        } else {                                        \
            /* Compressed Extension Instruction */      \
            DISPATCH_RV32C()                            \
        }                                               \
    }

#define EXEC(instr)                    \
    {                                  \
        /* dispatch this opcode */     \
        if (!op_##instr(rv, inst))     \
            goto quit;                 \
        /* increment the cycles csr */ \
        rv->csr_cycle++;               \
    }

#define TARGET(instr)         \
    op_##instr : EXEC(instr); \
    DISPATCH();

    DISPATCH();

    // main loop
    TARGET(load)
    TARGET(op_imm)
    TARGET(auipc)
    TARGET(store)
    TARGET(op)
    TARGET(lui)
    TARGET(branch)
    TARGET(jalr)
    TARGET(jal)
    TARGET(system)
#ifdef ENABLE_RV32C
    TARGET(caddi4spn)
    TARGET(caddi)
    TARGET(cslli)
    TARGET(cjal)
    TARGET(clw)
    TARGET(cli)
    TARGET(clwsp)
    TARGET(clui)
    TARGET(cmisc_alu)
    TARGET(ccr)
    TARGET(cj)
    TARGET(csw)
    TARGET(cbeqz)
    TARGET(cswsp)
    TARGET(cbnez)
#endif
#ifdef ENABLE_Zifencei
    TARGET(misc_mem)
#endif
#ifdef ENABLE_RV32A
    TARGET(amo)
#endif
    TARGET(unimp)

quit:
    return;

#undef DISPATCH_RV32C
#undef DISPATCH
#undef EXEC
#undef TARGET
#else   // ENABLE_COMPUTED_GOTO = 0
    while (rv->csr_cycle < cycles_target && !rv->halt) {
        // fetch the next instruction
        inst = rv->io.mem_ifetch(rv, rv->PC);

        // standard uncompressed instruction
        if ((inst & 3) == 3) {
            index = (inst & INST_6_2) >> 2;

            // dispatch this opcode
            TABLE_TYPE op = jump_table[index];
            assert(op);
            rv->inst_len = INST_32;
            if (!op(rv, inst))
                break;

            // increment the cycles csr
            rv->csr_cycle++;
        } else {
            // standard compressed instruction
            const uint16_t c_index =
                (inst & FR_C_15_13 >> 11) | (inst & FR_C_1_0);

            // dispactch c_opcode (compressed instructions)
            TABLE_TYPE_RVC op = jump_tablec[c_index];
            assert(op);
            rv->inst_len = INST_16;
            if (!op(rv, inst))
                break;

            // increment the cycles csr
            rv->csr_cycle++;
        }
#endif  // ENABLE_COMPUTED_GOTO
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
