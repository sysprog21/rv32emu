/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if RV32_HAS(EXT_F)
#include <math.h>
#if defined(__APPLE__)
static inline int isinff(float x)
{
    return __builtin_fabsf(x) == __builtin_inff();
}
static inline int isnanf(float x)
{
    return x != x;
}
#endif
#endif

#if RV32_HAS(GDBSTUB)
extern struct target_ops gdbstub_ops;
#endif

#include "riscv.h"
#include "riscv_private.h"

/* RISC-V exception code list */
#define RV_EXCEPTION_LIST                                       \
    _(insn_misaligned, 0)  /* Instruction address misaligned */ \
    _(illegal_insn, 2)     /* Illegal instruction */            \
    _(breakpoint, 3)       /* Breakpoint */                     \
    _(load_misaligned, 4)  /* Load address misaligned */        \
    _(store_misaligned, 6) /* Store/AMO address misaligned */   \
    _(ecall_M, 11)         /* Environment call from M-mode */

enum {
#define _(type, code) rv_exception_code##type = code,
    RV_EXCEPTION_LIST
#undef _
};

static void rv_exception_default_handler(struct riscv_t *rv)
{
    rv->csr_mepc += rv->insn_len;
    rv->PC = rv->csr_mepc; /* mret */
}

#define EXCEPTION_HANDLER_IMPL(type, code)                                \
    static void rv_except_##type(struct riscv_t *rv, uint32_t mtval)      \
    {                                                                     \
        /* mtvec (Machine Trap-Vector Base Address Register)              \
         * mtvec[MXLEN-1:2]: vector base address                          \
         * mtvec[1:0] : vector mode                                       \
         */                                                               \
        const uint32_t base = rv->csr_mtvec & ~0x3;                       \
        const uint32_t mode = rv->csr_mtvec & 0x3;                        \
        /* mepc  (Machine Exception Program Counter)                      \
         * mtval (Machine Trap Value Register)                            \
         * mcause (Machine Cause Register): store exception code          \
         */                                                               \
        rv->csr_mepc = rv->PC;                                            \
        rv->csr_mtval = mtval;                                            \
        rv->csr_mcause = code;                                            \
        if (!rv->csr_mtvec) { /* in case CSR is not configured */         \
            rv_exception_default_handler(rv);                             \
            return;                                                       \
        }                                                                 \
        switch (mode) {                                                   \
        case 0: /* DIRECT: All exceptions set PC to base */               \
            rv->PC = base;                                                \
            break;                                                        \
        /* VECTORED: Asynchronous interrupts set PC to base + 4 * code */ \
        case 1:                                                           \
            rv->PC = base + 4 * code;                                     \
            break;                                                        \
        }                                                                 \
    }

/* RISC-V exception handlers */
#define _(type, code) EXCEPTION_HANDLER_IMPL(type, code)
RV_EXCEPTION_LIST
#undef _

/* RV32I Base Instruction Set
 *
 * bits  0-6:  opcode
 * bits  7-10: func3
 * bit  11: bit 5 of func7
 */

static inline bool op_load(struct riscv_t *rv, uint32_t insn UNUSED)
{
    /* I-type
     *  31       20 19   15 14    12 11   7 6      0
     * | imm[11:0] |  rs1  | funct3 |  rd  | opcode |
     */
    const int32_t imm = dec_itype_imm(insn);
    const uint32_t rs1 = dec_rs1(insn);
    const uint32_t funct3 = dec_funct3(insn);
    const uint32_t rd = dec_rd(insn);

    /* load address */
    const uint32_t addr = rv->X[rs1] + imm;

    /* dispatch by read size
     *
     * imm[11:0] rs1 000 rd 0000011 LB
     * imm[11:0] rs1 001 rd 0000011 LH
     * imm[11:0] rs1 010 rd 0000011 LW
     * imm[11:0] rs1 011 rd 0000011 LD
     * imm[11:0] rs1 100 rd 0000011 LBU
     * imm[11:0] rs1 101 rd 0000011 LHU
     * imm[11:0] rs1 110 rd 0000011 LWU
     */
    switch (funct3) {
    case 0: /* LB: Load Byte */
        rv->X[rd] = sign_extend_b(rv->io.mem_read_b(rv, addr));
        break;
    case 1: /* LH: Load Halfword */
        if (addr & 1) {
            rv_except_load_misaligned(rv, addr);
            return false;
        }
        rv->X[rd] = sign_extend_h(rv->io.mem_read_s(rv, addr));
        break;
    case 2: /* LW: Load Word */
        if (addr & 3) {
            rv_except_load_misaligned(rv, addr);
            return false;
        }
        rv->X[rd] = rv->io.mem_read_w(rv, addr);
        break;
    case 4: /* LBU: Load Byte Unsigned */
        rv->X[rd] = rv->io.mem_read_b(rv, addr);
        break;
    case 5: /* LHU: Load Halfword Unsigned */
        if (addr & 1) {
            rv_except_load_misaligned(rv, addr);
            return false;
        }
        rv->X[rd] = rv->io.mem_read_s(rv, addr);
        break;
    default:
        rv_except_illegal_insn(rv, insn);
        return false;
    }

    /* step over instruction */
    rv->PC += rv->insn_len;

    /* enforce zero register */
    if (rd == rv_reg_zero)
        rv->X[rv_reg_zero] = 0;
    return true;
}

#if RV32_HAS(Zifencei)
static inline bool op_misc_mem(struct riscv_t *rv, uint32_t insn UNUSED)
{
    /* FIXME: fill real implementations */
    rv->PC += 4;
    return true;
}
#else
#define op_misc_mem OP_UNIMP
#endif /* RV32_HAS(Zifencei) */

static inline bool op_op_imm(struct riscv_t *rv, uint32_t insn)
{
    /* I-type
     *  31       20 19   15 14    12 11   7 6      0
     * | imm[11:0] |  rs1  | funct3 |  rd  | opcode |
     */
    const int32_t imm = dec_itype_imm(insn);
    const uint32_t rd = dec_rd(insn);
    const uint32_t rs1 = dec_rs1(insn);
    const uint32_t funct3 = dec_funct3(insn);

    /* imm[11:0]     rs1 000 rd 0010011 I ADDI
     * 0000000 shamt rs1 001 rd 0010011 I SLLI
     * imm[11:0]     rs1 010 rd 0010011 I SLTI
     * imm[11:0]     rs1 011 rd 0010011 I SLTIU
     * imm[11:0]     rs1 100 rd 0010011 I XORI
     * 0000000 shamt rs1 101 rd 0010011 I SLRI
     * 0100000 shamt rs1 101 rd 0010011 I SRAI
     * imm[11:0]     rs1 110 rd 0010011 I ORI
     * imm[11:0]     rs1 111 rd 0010011 I ANDI
     */

    /* dispatch operation type */
    switch (funct3) {
    case 0: /* ADDI: Add Immediate */
        /* Adds the sign-extended 12-bit immediate to register rs1. Arithmetic
         * overflow is ignored and the result is simply the low XLEN bits of the
         * result. ADDI rd, rs1, 0 is used to implement the MV rd, rs1 assembler
         * pseudo-instruction.
         */
        rv->X[rd] = (int32_t) (rv->X[rs1]) + imm;
        break;
    case 1: /* SLLI: Shift Left Logical */
        /* Performs logical left shift on the value in register rs1 by the shift
         * amount held in the lower 5 bits of the immediate.
         */
        rv->X[rd] = rv->X[rs1] << (imm & 0x1f);
        break;
    case 2: /* SLTI: Set on Less Than Immediate */
        /* Place the value 1 in register rd if register rs1 is less than the
         * signextended immediate when both are treated as signed numbers, else
         * 0 is written to rd.
         */
        rv->X[rd] = ((int32_t) (rv->X[rs1]) < imm) ? 1 : 0;
        break;
    case 3: /* SLTIU: Set on Less Than Immediate Unsigned */
        /* Place the value 1 in register rd if register rs1 is less than the
         * immediate when both are treated as unsigned numbers, else 0 is
         * written to rd.
         */
        rv->X[rd] = (rv->X[rs1] < (uint32_t) imm) ? 1 : 0;
        break;
    case 4: /* XORI: Exclusive OR Immediate */
        rv->X[rd] = rv->X[rs1] ^ imm;
        break;
    case 5:
        /* SLL, SRL, and SRA perform logical left, logical right, and
         * arithmetic right shifts on the value in register rs1.
         */
        if (imm & ~0x1f) { /* SRAI: Shift Right Arithmetic */
            /* Performs arithmetic right shift on the value in register rs1 by
             * the shift amount held in the lower 5 bits of the immediate.
             */
            rv->X[rd] = ((int32_t) rv->X[rs1]) >> (imm & 0x1f);
        } else { /* SRLI: Shift Right Logical */
            /* Performs logical right shift on the value in register rs1 by the
             * shift amount held in the lower 5 bits of the immediate.
             */
            rv->X[rd] = rv->X[rs1] >> (imm & 0x1f);
        }
        break;
    case 6: /* ORI: OR Immediate */
        rv->X[rd] = rv->X[rs1] | imm;
        break;
    case 7: /* ANDI: AND Immediate */
        /* Performs bitwise AND on register rs1 and the sign-extended 12-bit
         * immediate and place the result in rd.
         */
        rv->X[rd] = rv->X[rs1] & imm;
        break;
    default:
        rv_except_illegal_insn(rv, insn);
        return false;
    }

    /* step over instruction */
    rv->PC += rv->insn_len;

    /* enforce zero register */
    if (rd == rv_reg_zero)
        rv->X[rv_reg_zero] = 0;
    return true;
}

/* Add upper immediate to pc
 *
 * AUIPC is used to build pc-relative addresses and uses the U-type format.
 * AUIPC forms a 32-bit offset from the 20-bit U-immediate, filling in the
 * lowest 12 bits with zeros, adds this offset to the address of the AUIPC
 * instruction, then places the result in register rd.
 */
static inline bool op_auipc(struct riscv_t *rv, uint32_t insn)
{
    /* U-type
     *  31        12 11   7 6      0
     * | imm[31:12] |  rd  | opcode |
     */
    const uint32_t rd = dec_rd(insn);
    const uint32_t val = dec_utype_imm(insn) + rv->PC;
    rv->X[rd] = val;

    /* step over instruction */
    rv->PC += rv->insn_len;

    /* enforce zero register */
    if (rd == rv_reg_zero)
        rv->X[rv_reg_zero] = 0;
    return true;
}

static inline bool op_store(struct riscv_t *rv, uint32_t insn)
{
    /* S-type
     *  31       25 24   20 19   15 14    12 11       7 6      0
     * | imm[11:5] |  rs2  |  rs1  | funct3 | imm[4:0] | opcode |
     */
    const int32_t imm = dec_stype_imm(insn);
    const uint32_t rs1 = dec_rs1(insn), rs2 = dec_rs2(insn);
    const uint32_t funct3 = dec_funct3(insn);

    /* store address */
    const uint32_t addr = rv->X[rs1] + imm;
    const uint32_t data = rv->X[rs2];

    /* dispatch by write size
     *
     * imm[11:5] rs2 rs1 000 imm[4:0] 0100011 SB
     * imm[11:5] rs2 rs1 001 imm[4:0] 0100011 SH
     * imm[11:5] rs2 rs1 010 imm[4:0] 0100011 SW
     * imm[11:5] rs2 rs1 011 imm[4:0] 0100011 SD
     */
    switch (funct3) {
    case 0: /* SB: Store Byte */
        rv->io.mem_write_b(rv, addr, data);
        break;
    case 1: /* SH: Store Halfword */
        if (addr & 1) {
            rv_except_store_misaligned(rv, addr);
            return false;
        }
        rv->io.mem_write_s(rv, addr, data);
        break;
    case 2: /* SW: Store Word */
        if (addr & 3) {
            rv_except_store_misaligned(rv, addr);
            return false;
        }
        rv->io.mem_write_w(rv, addr, data);
        break;
    default:
        rv_except_illegal_insn(rv, insn);
        return false;
    }

    /* step over instruction */
    rv->PC += 4;
    return true;
}

static inline bool op_op(struct riscv_t *rv, uint32_t insn)
{
    /* R-type
     *  31    25 24   20 19   15 14    12 11   7 6      0
     * | funct7 |  rs2  |  rs1  | funct3 |  rd  | opcode |
     */
    const uint32_t rd = dec_rd(insn);
    const uint32_t funct3 = dec_funct3(insn);
    const uint32_t rs1 = dec_rs1(insn), rs2 = dec_rs2(insn);
    const uint32_t funct7 = dec_funct7(insn);

    /* TODO: skip zero register here */

    /* 0000000 rs2 rs1 000 rd 0110011 R ADD
     * 0000000 rs2 rs1 001 rd 0110011 R SLL
     * 0000000 rs2 rs1 010 rd 0110011 R SLT
     * 0000000 rs2 rs1 011 rd 0110011 R SLTU
     * 0000000 rs2 rs1 100 rd 0110011 R XOR
     * 0000000 rs2 rs1 101 rd 0110011 R SRL
     * 0000000 rs2 rs1 110 rd 0110011 R OR
     * 0000000 rs2 rs1 111 rd 0110011 R AND
     * 0100000 rs2 rs1 000 rd 0110011 R SUB
     * 0100000 rs2 rs1 101 rd 0110011 R SRA
     */

    switch (funct7) {
    case 0b0000000:
        switch (funct3) {
        case 0b000: /* ADD */
            rv->X[rd] = (int32_t) (rv->X[rs1]) + (int32_t) (rv->X[rs2]);
            break;
        case 0b001: /* SLL: Shift Left Logical */
            rv->X[rd] = rv->X[rs1] << (rv->X[rs2] & 0x1f);
            break;
        case 0b010: /* SLT: Set on Less Than */
            rv->X[rd] =
                ((int32_t) (rv->X[rs1]) < (int32_t) (rv->X[rs2])) ? 1 : 0;
            break;
        case 0b011: /* SLTU: Set on Less Than Unsigned */
            rv->X[rd] = (rv->X[rs1] < rv->X[rs2]) ? 1 : 0;
            break;
        case 0b100: /* XOR: Exclusive OR */
            rv->X[rd] = rv->X[rs1] ^ rv->X[rs2];
            break;
        case 0b101: /* SRL: Shift Right Logical */
            rv->X[rd] = rv->X[rs1] >> (rv->X[rs2] & 0x1f);
            break;
        case 0b110: /* OR */
            rv->X[rd] = rv->X[rs1] | rv->X[rs2];
            break;
        case 0b111: /* AND */
            rv->X[rd] = rv->X[rs1] & rv->X[rs2];
            break;
        default:
            rv_except_illegal_insn(rv, insn);
            return false;
        }
        break;
#if RV32_HAS(EXT_M)
    case 0b0000001: /* RV32M instructions */
        switch (funct3) {
        case 0b000: /* MUL: Multiply */
            rv->X[rd] = (int32_t) rv->X[rs1] * (int32_t) rv->X[rs2];
            break;
        case 0b001: { /* MULH: Multiply High Signed Signed */
            const int64_t a = (int32_t) rv->X[rs1], b = (int32_t) rv->X[rs2];
            rv->X[rd] = ((uint64_t) (a * b)) >> 32;
            break;
        }
        case 0b010: { /* MULHSU: Multiply High Signed Unsigned */
            const int64_t a = (int32_t) rv->X[rs1];
            const uint64_t b = rv->X[rs2];
            rv->X[rd] = ((uint64_t) (a * b)) >> 32;
            break;
        }
        case 0b011: /* MULHU: Multiply High Unsigned Unsigned */
            rv->X[rd] = ((uint64_t) rv->X[rs1] * (uint64_t) rv->X[rs2]) >> 32;
            break;
        case 0b100: { /* DIV: Divide Signed */
            const int32_t dividend = (int32_t) rv->X[rs1];
            const int32_t divisor = (int32_t) rv->X[rs2];
            if (divisor == 0) {
                rv->X[rd] = ~0U;
            } else if (divisor == -1 && rv->X[rs1] == 0x80000000U) {
                /* overflow */
                rv->X[rd] = rv->X[rs1];
            } else {
                rv->X[rd] = dividend / divisor;
            }
            break;
        }
        case 0b101: { /* DIVU: Divide Unsigned */
            const uint32_t dividend = rv->X[rs1], divisor = rv->X[rs2];
            if (divisor == 0) {
                rv->X[rd] = ~0U;
            } else {
                rv->X[rd] = dividend / divisor;
            }
            break;
        }
        case 0b110: { /* REM: Remainder Signed */
            const int32_t dividend = rv->X[rs1], divisor = rv->X[rs2];
            if (divisor == 0) {
                rv->X[rd] = dividend;
            } else if (divisor == -1 && rv->X[rs1] == 0x80000000U) {
                /* overflow */
                rv->X[rd] = 0;
            } else {
                rv->X[rd] = dividend % divisor;
            }
            break;
        }
        case 0b111: { /* REMU: Remainder Unsigned */
            const uint32_t dividend = rv->X[rs1], divisor = rv->X[rs2];
            if (divisor == 0) {
                rv->X[rd] = dividend;
            } else {
                rv->X[rd] = dividend % divisor;
            }
            break;
        }
        default:
            rv_except_illegal_insn(rv, insn);
            return false;
        }
        break;
#endif /* RV32_HAS(EXT_M) */
    case 0b0100000:
        switch (funct3) {
        case 0b000: /* SUB: Substract */
            rv->X[rd] = (int32_t) (rv->X[rs1]) - (int32_t) (rv->X[rs2]);
            break;
        case 0b101: /* SRA: Shift Right Arithmetic */
            rv->X[rd] = ((int32_t) rv->X[rs1]) >> (rv->X[rs2] & 0x1f);
            break;
        default:
            rv_except_illegal_insn(rv, insn);
            return false;
        }
        break;
    default:
        rv_except_illegal_insn(rv, insn);
        return false;
    }

    /* step over instruction */
    rv->PC += rv->insn_len;

    /* enforce zero register */
    if (rd == rv_reg_zero)
        rv->X[rv_reg_zero] = 0;
    return true;
}

/* Load Upper Immediate
 *
 * LUI is used to build 32-bit constants and uses the U-type format. LUI places
 * the U-immediate value in the top 20 bits of the destination register rd,
 * filling in the lowest 12 bits with zeros. The 32-bit result is sign-extended
 * to 64 bits.
 */
static inline bool op_lui(struct riscv_t *rv, uint32_t insn)
{
    /* U-type
     *  31        12 11   7 6      0
     * | imm[31:12] |  rd  | opcode |
     */
    const uint32_t rd = dec_rd(insn);
    const uint32_t val = dec_utype_imm(insn);
    rv->X[rd] = val;

    /* step over instruction */
    rv->PC += rv->insn_len;

    /* enforce zero register */
    if (rd == rv_reg_zero)
        rv->X[rv_reg_zero] = 0;
    return true;
}

static inline bool op_branch(struct riscv_t *rv, uint32_t insn)
{
    const uint32_t pc = rv->PC;

    /* B-type
     *     31     30     25   24 20 19 15 14    12 11       8     7     6    0
     * | imm[12] | imm[10:5] | rs2 | rs1 | funct3 | imm[4:1] | imm[11] |opcode|
     */
    const uint32_t func3 = dec_funct3(insn);
    const int32_t imm = dec_btype_imm(insn);
    const uint32_t rs1 = dec_rs1(insn), rs2 = dec_rs2(insn);

    /* track if branch is taken or not */
    bool taken = false;

    /* dispatch by branch type */
    switch (func3) {
    case 0: /* BEQ: Branch if Equal */
        taken = (rv->X[rs1] == rv->X[rs2]);
        break;
    case 1: /* BNE: Branch if Not Equal */
        taken = (rv->X[rs1] != rv->X[rs2]);
        break;
    case 4: /* BLT: Branch if Less Than */
        taken = ((int32_t) rv->X[rs1] < (int32_t) rv->X[rs2]);
        break;
    case 5: /* BGE: Branch if Greater Than */
        taken = ((int32_t) rv->X[rs1] >= (int32_t) rv->X[rs2]);
        break;
    case 6: /* BLTU: Branch if Less Than Unsigned */
        taken = (rv->X[rs1] < rv->X[rs2]);
        break;
    case 7: /* BGEU: Branch if Greater Than Unsigned */
        taken = (rv->X[rs1] >= rv->X[rs2]);
        break;
    default:
        rv_except_illegal_insn(rv, insn);
        return false;
    }

    /* perform branch action */
    if (taken) {
        rv->PC += imm;
        if (rv->PC &
#if RV32_HAS(EXT_C)
            0x1
#else
            0x3
#endif
        )
            rv_except_insn_misaligned(rv, pc);
    } else {
        /* step over instruction */
        rv->PC += rv->insn_len;
    }

    /* can branch */
    return false;
}

/* Jump and Link Register (JALR): store successor instruction address into rd.
 *
 * The indirect jump instruction JALR uses the I-type encoding. The target
 * address is obtained by adding the sign-extended 12-bit I-immediate to the
 * register rs1, then setting the least-significant bit of the result to zero.
 * The address of the instruction following the jump (pc+4) is written to
 * register rd. Register x0 can be used as the destination if the result is not
 * required.
 */
static inline bool op_jalr(struct riscv_t *rv, uint32_t insn)
{
    const uint32_t pc = rv->PC;

    /* I-type
     *  31       20 19   15 14    12 11   7 6      0
     * | imm[11:0] |  rs1  | funct3 |  rd  | opcode |
     */
    const uint32_t rd = dec_rd(insn);
    const uint32_t rs1 = dec_rs1(insn);
    const int32_t imm = dec_itype_imm(insn);

    /* compute return address */
    const uint32_t ra = rv->PC + rv->insn_len;

    /* jump */
    rv->PC = (rv->X[rs1] + imm) & ~1U;

    /* link */
    if (rd != rv_reg_zero)
        rv->X[rd] = ra;

    /* check for exception */
    if (rv->PC &
#if RV32_HAS(EXT_C)
        0x1
#else
        0x3
#endif
    ) {
        rv_except_insn_misaligned(rv, pc);
        return false;
    }

    /* can branch */
    return false;
}

/* Jump and Link (JAL): store successor instruction address into rd.
 * add sext J imm (offset) to pc.
 */
static inline bool op_jal(struct riscv_t *rv, uint32_t insn)
{
    const uint32_t pc = rv->PC;

    /* J-type
     *     31     30       21     20    19        12 11   7 6      0
     * | imm[20] | imm[10:1] | imm[11] | imm[19:12] |  rd  | opcode |
     */
    const uint32_t rd = dec_rd(insn);
    const int32_t rel = dec_jtype_imm(insn);

    /* compute return address */
    const uint32_t ra = rv->PC + rv->insn_len;
    rv->PC += rel;

    /* link */
    if (rd != rv_reg_zero)
        rv->X[rd] = ra;

    if (rv->PC &
#if RV32_HAS(EXT_C)
        0x1
#else
        0x3
#endif
    ) {
        rv_except_insn_misaligned(rv, pc);
        return false;
    }

    /* can branch */
    return false;
}

/* get a pointer to a CSR */
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
#if RV32_HAS(EXT_F)
    case CSR_FFLAGS:
        return (uint32_t *) (&rv->csr_fcsr);
    case CSR_FCSR:
        return (uint32_t *) (&rv->csr_fcsr);
#endif
    default:
        return NULL;
    }
}

static bool csr_is_writable(uint32_t csr)
{
    return csr < 0xc00;
}

/* CSRRW (Atomic Read/Write CSR) instruction atomically swaps values in the
 * CSRs and integer registers. CSRRW reads the old value of the CSR,
 * zero - extends the value to XLEN bits, then writes it to integer register rd.
 * The initial value in rs1 is written to the CSR.
 * If rd == x0, then the instruction shall not read the CSR and shall not cause
 * any of the side effects that might occur on a CSR read.
 */
static uint32_t csr_csrrw(struct riscv_t *rv, uint32_t csr, uint32_t val)
{
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    uint32_t out = *c;
#if RV32_HAS(EXT_F)
    if (csr == CSR_FFLAGS)
        out &= FFLAG_MASK;
#endif
    if (csr_is_writable(csr))
        *c = val;

    return out;
}

/* perform csrrs (atomic read and set) */
static uint32_t csr_csrrs(struct riscv_t *rv, uint32_t csr, uint32_t val)
{
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    uint32_t out = *c;
#if RV32_HAS(EXT_F)
    if (csr == CSR_FFLAGS)
        out &= FFLAG_MASK;
#endif
    if (csr_is_writable(csr))
        *c |= val;

    return out;
}

/* perform csrrc (atomic read and clear)
 * Read old value of CSR, zero-extend to XLEN bits, write to rd
 * Read value from rs1, use as bit mask to clear bits in CSR
 */
static uint32_t csr_csrrc(struct riscv_t *rv, uint32_t csr, uint32_t val)
{
    uint32_t *c = csr_get_ptr(rv, csr);
    if (!c)
        return 0;

    uint32_t out = *c;
#if RV32_HAS(EXT_F)
    if (csr == CSR_FFLAGS)
        out &= FFLAG_MASK;
#endif
    if (csr_is_writable(csr))
        *c &= ~val;
    return out;
}

static inline bool op_system(struct riscv_t *rv, uint32_t insn)
{
    /* I-type
     * system instruction
     *  31     20 19   15 14    12 11   7 6      0
     * | funct12 |  rs1  | funct3 |  rd  | opcode |
     *
     * csr instruction
     *  31     20 19   15 14    12 11   7 6      0
     * |   csr   |  rs1  | funct3 |  rd  | opcode |
     */
    const int32_t funct12 = dec_funct12(insn);
    const int32_t csr = dec_csr(insn);
    const uint32_t funct3 = dec_funct3(insn);
    const uint32_t rs1 = dec_rs1(insn);
    const uint32_t rd = dec_rd(insn);

    /* dispatch by func3 field */
    switch (funct3) {
    case 0:
        switch (funct12) { /* dispatch from imm field */
        case 0:            /* ECALL: Environment Call */
            rv->io.on_ecall(rv);
            return true;
        case 1: /* EBREAK: Environment Break */
            rv->io.on_ebreak(rv);
            return true;
        case 0x002: /* URET: Return from handling an interrupt or exception */
        case 0x102: /* SRET */
        case 0x202: /* HRET */
        case 0x105: /* WFI */
            rv_except_illegal_insn(rv, insn);
            return false;
        case 0x302: /* MRET */
            rv->PC = rv->csr_mepc;
            /* this is a branch */
            return false;
        default:
            rv_except_illegal_insn(rv, insn);
            return false;
        }
        break;
#if RV32_HAS(Zicsr)
    /* All CSR instructions atomically read-modify-write a single CSR.
     *    Register operand
     *    ---------------------------------------------------------
     *    Instruction         rd          rs1        Read    Write
     *    ---------------------------------------------------------
     *    CSRRW               x0          -          no      yes
     *    CSRRW               !x0         -          yes     yes
     *    CSRRS/C             -           x0         yes     no
     *    CSRRS/C             -           !x0        yes     yes
     *
     *    Immediate operand
     *    --------------------------------------------------------
     *    Instruction         rd          uimm       Read    Write
     *    ---------------------------------------------------------
     *    CSRRWI              x0          -          no      yes
     *    CSRRWI              !x0         -          yes     yes
     *    CSRRS/CI            -           0          yes     no
     *    CSRRS/CI            -           !0         yes     yes
     */
    case 1: { /* CSRRW: Atomic Read/Write CSR */
        uint32_t tmp = csr_csrrw(rv, csr, rv->X[rs1]);
        rv->X[rd] = rd ? tmp : rv->X[rd];
        break;
    }
    case 2: { /* CSRRS: Atomic Read and Set Bits in CSR */
        uint32_t tmp =
            csr_csrrs(rv, csr, (rs1 == rv_reg_zero) ? 0U : rv->X[rs1]);
        rv->X[rd] = rd ? tmp : rv->X[rd];
        break;
    }
    case 3: { /* CSRRC: Atomic Read and Clear Bits in CSR */
        uint32_t tmp =
            csr_csrrc(rv, csr, (rs1 == rv_reg_zero) ? ~0U : rv->X[rs1]);
        rv->X[rd] = rd ? tmp : rv->X[rd];
        break;
    }
    case 5: { /* CSRRWI */
        uint32_t tmp = csr_csrrw(rv, csr, rs1);
        rv->X[rd] = rd ? tmp : rv->X[rd];
        break;
    }
    case 6: { /* CSRRSI */
        uint32_t tmp = csr_csrrs(rv, csr, rs1);
        rv->X[rd] = rd ? tmp : rv->X[rd];
        break;
    }
    case 7: { /* CSRRCI */
        uint32_t tmp = csr_csrrc(rv, csr, rs1);
        rv->X[rd] = rd ? tmp : rv->X[rd];
        break;
    }
#endif /* RV32_HAS(Zicsr) */
    default:
        rv_except_illegal_insn(rv, insn);
        return false;
    }

    /* step over instruction */
    rv->PC += rv->insn_len;

    /* enforce zero register */
    if (rd == rv_reg_zero)
        rv->X[rv_reg_zero] = 0;
    return true;
}

#if RV32_HAS(EXT_A)
/* At present, AMO is not implemented atomically because the emulated RISC-V
 * core just runs on single thread, and no out-of-order execution happens.
 * In addition, rl/aq are not handled.
 */
static inline bool op_amo(struct riscv_t *rv, uint32_t insn)
{
    const uint32_t rd = dec_rd(insn);
    const uint32_t rs1 = dec_rs1(insn), rs2 = dec_rs2(insn);
    const uint32_t f7 = dec_funct7(insn);
    const uint32_t funct5 = (f7 >> 2) & 0x1f;

    switch (funct5) {
    case 0b00010: /* LR.W: Load Reserved */
        rv->X[rd] = rv->io.mem_read_w(rv, rv->X[rs1]);
        /* skip registration of the 'reservation set'
         * FIXME: uimplemented
         */
        break;
    case 0b00011: /* SC.W: Store Conditional */
        /* assume the 'reservation set' is valid
         * FIXME: unimplemented
         */
        rv->io.mem_write_w(rv, rv->X[rs1], rv->X[rs2]);
        rv->X[rd] = 0;
        break;
    case 0b00001: { /* AMOSWAP.W: Atomic Swap */
        rv->X[rd] = rv->io.mem_read_w(rv, rs1);
        rv->io.mem_write_s(rv, rs1, rv->X[rs2]);
        break;
    }
    case 0b00000: { /* AMOADD.W: Atomic ADD */
        rv->X[rd] = rv->io.mem_read_w(rv, rs1);
        const int32_t res = (int32_t) rv->X[rd] + (int32_t) rv->X[rs2];
        rv->io.mem_write_s(rv, rs1, res);
        break;
    }
    case 0b00100: { /* AMOXOR.W: Atomix XOR */
        rv->X[rd] = rv->io.mem_read_w(rv, rs1);
        const int32_t res = rv->X[rd] ^ rv->X[rs2];
        rv->io.mem_write_s(rv, rs1, res);
        break;
    }
    case 0b01100: { /* AMOAND.W: Atomic AND */
        rv->X[rd] = rv->io.mem_read_w(rv, rs1);
        const int32_t res = rv->X[rd] & rv->X[rs2];
        rv->io.mem_write_s(rv, rs1, res);
        break;
    }
    case 0b01000: { /* AMOOR.W: Atomic OR */
        rv->X[rd] = rv->io.mem_read_w(rv, rs1);
        const int32_t res = rv->X[rd] | rv->X[rs2];
        rv->io.mem_write_s(rv, rs1, res);
        break;
    }
    case 0b10000: { /* AMOMIN.W: Atomic MIN */
        rv->X[rd] = rv->io.mem_read_w(rv, rs1);
        const int32_t a = rv->X[rd], b = rv->X[rs2];
        const int32_t res = a < b ? a : b;
        rv->io.mem_write_s(rv, rs1, res);
        break;
    }
    case 0b10100: { /* AMOMAX.W: Atomic MAX */
        rv->X[rd] = rv->io.mem_read_w(rv, rs1);
        const int32_t a = rv->X[rd], b = rv->X[rs2];
        const int32_t res = a > b ? a : b;
        rv->io.mem_write_s(rv, rs1, res);
        break;
    }
    case 0b11000: { /* AMOMINU.W */
        rv->X[rd] = rv->io.mem_read_w(rv, rs1);
        const uint32_t a = rv->X[rd], b = rv->X[rs2];
        const uint32_t res = a < b ? a : b;
        rv->io.mem_write_s(rv, rs1, res);
        break;
    }
    case 0b11100: { /* AMOMAXU.W */
        rv->X[rd] = rv->io.mem_read_w(rv, rs1);
        const uint32_t a = rv->X[rd], b = rv->X[rs2];
        const uint32_t res = a > b ? a : b;
        rv->io.mem_write_s(rv, rs1, res);
        break;
    }
    default:
        rv_except_illegal_insn(rv, insn);
        return false;
    }

    /* step over instruction */
    rv->PC += 4;

    /* enforce zero register */
    if (rd == rv_reg_zero)
        rv->X[rv_reg_zero] = 0;
    return true;
}
#else
#define op_amo OP_UNIMP
#endif /* RV32_HAS(EXT_A) */

#if RV32_HAS(EXT_F)
static inline bool op_load_fp(struct riscv_t *rv, uint32_t insn)
{
    const uint32_t rd = dec_rd(insn);
    const uint32_t rs1 = dec_rs1(insn);
    const int32_t imm = dec_itype_imm(insn);

    /* calculate load address */
    const uint32_t addr = rv->X[rs1] + imm;

    /* copy into the float register */
    const uint32_t data = rv->io.mem_read_w(rv, addr);
    memcpy(rv->F + rd, &data, 4);

    /* step over instruction */
    rv->PC += 4;
    return true;
}

static inline bool op_store_fp(struct riscv_t *rv, uint32_t insn)
{
    const uint32_t rs1 = dec_rs1(insn);
    const uint32_t rs2 = dec_rs2(insn);
    const int32_t imm = dec_stype_imm(insn);

    /* calculate store address */
    const uint32_t addr = rv->X[rs1] + imm;

    /* copy from float registers */
    uint32_t data;
    memcpy(&data, (const void *) (rv->F + rs2), 4);
    rv->io.mem_write_w(rv, addr, data);

    /* step over instruction */
    rv->PC += 4;
    return true;
}

static inline bool op_fp(struct riscv_t *rv, uint32_t insn)
{
    const uint32_t rd = dec_rd(insn);
    const uint32_t rs1 = dec_rs1(insn), rs2 = dec_rs2(insn);
    const uint32_t rm = dec_funct3(insn); /* FIXME: rounding */
    const uint32_t funct7 = dec_funct7(insn);

    /* dispatch based on func7 (low 2 bits are width) */
    switch (funct7) {
    case 0b0000000: /* FADD */
        if (isnanf(rv->F[rs1]) || isnanf(rv->F[rs2]) ||
            isnanf(rv->F[rs1] + rv->F[rs2])) {
            /* raise invalid operation */
            rv->F_int[rd] = RV_NAN; /* F_int is the integer shortcut of F */
            rv->csr_fcsr |= FFLAG_INVALID_OP;
        } else {
            rv->F[rd] = rv->F[rs1] + rv->F[rs2];
        }
        if (isinff(rv->F[rd])) {
            rv->csr_fcsr |= FFLAG_OVERFLOW;
            rv->csr_fcsr |= FFLAG_INEXACT;
        }
        break;
    case 0b0000100: /* FSUB */
        if (isnanf(rv->F[rs1]) || isnanf(rv->F[rs2])) {
            rv->F_int[rd] = RV_NAN;
        } else {
            rv->F[rd] = rv->F[rs1] - rv->F[rs2];
        }
        break;
    case 0b0001000: /* FMUL */
        rv->F[rd] = rv->F[rs1] * rv->F[rs2];
        break;
    case 0b0001100: /* FDIV */
        rv->F[rd] = rv->F[rs1] / rv->F[rs2];
        break;
    case 0b0101100: /* FSQRT */
        rv->F[rd] = sqrtf(rv->F[rs1]);
        break;
    case 0b0010000: {
        uint32_t f1, f2, res;
        memcpy(&f1, rv->F + rs1, 4);
        memcpy(&f2, rv->F + rs2, 4);

        switch (rm) {
        case 0b000: /* FSGNJ.S */
            res = (f1 & ~FMASK_SIGN) | (f2 & FMASK_SIGN);
            break;
        case 0b001: /* FSGNJN.S */
            res = (f1 & ~FMASK_SIGN) | (~f2 & FMASK_SIGN);
            break;
        case 0b010: /* FSGNJX.S */
            res = f1 ^ (f2 & FMASK_SIGN);
            break;
        default:
            rv_except_illegal_insn(rv, insn);
            return false;
        }

        memcpy(rv->F + rd, &res, 4);
        break;
    }
    case 0b0010100:
        switch (rm) {
        case 0b000: { /* FMIN */
            /*
            In IEEE754-201x, fmin(x, y) return
                - min(x,y) if both numbers are not NaN
                - if one is NaN and another is a number, return the number
                - if both are NaN, return NaN

            When input is signaling NaN, raise invalid operation
            */
            uint32_t x, y;
            memcpy(&x, rv->F + rs1, 4);
            memcpy(&y, rv->F + rs2, 4);
            if (is_nan(x) || is_nan(y)) {
                if (is_snan(x) || is_snan(y))
                    rv->csr_fcsr |= FFLAG_INVALID_OP;
                if (is_nan(x) && !is_nan(y)) {
                    rv->F[rd] = rv->F[rs2];
                } else if (!is_nan(x) && is_nan(y)) {
                    rv->F[rd] = rv->F[rs1];
                } else {
                    rv->F_int[rd] = RV_NAN;
                }
            } else {
                uint32_t a_sign, b_sign;
                a_sign = x & FMASK_SIGN;
                b_sign = y & FMASK_SIGN;
                if (a_sign != b_sign) {
                    if (a_sign) {
                        rv->F[rd] = rv->F[rs1];
                    } else {
                        rv->F[rd] = rv->F[rs2];
                    }
                } else {
                    if ((rv->F[rs1] < rv->F[rs2])) {
                        rv->F[rd] = rv->F[rs1];
                    } else {
                        rv->F[rd] = rv->F[rs2];
                    }
                }
            }
            break;
        }
        case 0b001: { /* FMAX */
            uint32_t x, y;
            memcpy(&x, rv->F + rs1, 4);
            memcpy(&y, rv->F + rs2, 4);
            if (is_nan(x) || is_nan(y)) {
                if (is_snan(x) || is_snan(y))
                    rv->csr_fcsr |= FFLAG_INVALID_OP;
                if (is_nan(x) && !is_nan(y)) {
                    rv->F[rd] = rv->F[rs2];
                } else if (!is_nan(x) && is_nan(y)) {
                    rv->F[rd] = rv->F[rs1];
                } else {
                    rv->F_int[rd] = RV_NAN;
                }
            } else {
                uint32_t a_sign, b_sign;
                a_sign = x & FMASK_SIGN;
                b_sign = y & FMASK_SIGN;
                if (a_sign != b_sign) {
                    if (a_sign) {
                        rv->F[rd] = rv->F[rs2];
                    } else {
                        rv->F[rd] = rv->F[rs1];
                    }
                } else {
                    if ((rv->F[rs1] > rv->F[rs2])) {
                        rv->F[rd] = rv->F[rs1];
                    } else {
                        rv->F[rd] = rv->F[rs2];
                    }
                }
            }


            break;
        }
        default:
            rv_except_illegal_insn(rv, insn);
            return false;
        }
        break;
    case 0b1100000:
        switch (rs2) {
        case 0b00000: /* FCVT.W.S */
            rv->X[rd] = (int32_t) rv->F[rs1];
            break;
        case 0b00001: /* FCVT.WU.S */
            rv->X[rd] = (uint32_t) rv->F[rs1];
            break;
        default:
            rv_except_illegal_insn(rv, insn);
            return false;
        }
        break;
    case 0b1110000:
        switch (rm) {
        case 0b000: /* FMV.X.W */
            memcpy(rv->X + rd, rv->F + rs1, 4);
            break;
        case 0b001: { /* FCLASS.S */
            uint32_t bits;
            memcpy(&bits, rv->F + rs1, 4);
            rv->X[rd] = calc_fclass(bits);
            break;
        }
        default:
            rv_except_illegal_insn(rv, insn);
            return false;
        }
        break;
    case 0b1010000:
        switch (rm) {
        case 0b010: /* FEQ.S */
            rv->X[rd] = (rv->F[rs1] == rv->F[rs2]) ? 1 : 0;
            break;
        case 0b001: /* FLT.S */
            rv->X[rd] = (rv->F[rs1] < rv->F[rs2]) ? 1 : 0;
            break;
        case 0b000: /* FLE.S */
            rv->X[rd] = (rv->F[rs1] <= rv->F[rs2]) ? 1 : 0;
            break;
        default:
            rv_except_illegal_insn(rv, insn);
            return false;
        }
        break;
    case 0b1101000:
        switch (rs2) {
        case 0b00000: /* FCVT.S.W */
            rv->F[rd] = (float) (int32_t) rv->X[rs1];
            break;
        case 0b00001: /* FCVT.S.WU */
            rv->F[rd] = (float) (uint32_t) rv->X[rs1];
            break;
        default:
            rv_except_illegal_insn(rv, insn);
            return false;
        }
        break;
    case 0b1111000: /* FMV.W.X */
        memcpy(rv->F + rd, rv->X + rs1, 4);
        break;
    default:
        rv_except_illegal_insn(rv, insn);
        return false;
    }

    /* enforce zero register */
    if (rd == rv_reg_zero)
        rv->X[rv_reg_zero] = 0;

    /* step over instruction */
    rv->PC += 4;
    return true;
}

static inline bool op_madd(struct riscv_t *rv, uint32_t insn)
{
    const uint32_t rd = dec_rd(insn);
    const uint32_t rs1 = dec_rs1(insn);
    const uint32_t rs2 = dec_rs2(insn);
    const uint32_t rs3 = dec_r4type_rs3(insn);

    /* compute */
    rv->F[rd] = rv->F[rs1] * rv->F[rs2] + rv->F[rs3];

    /* step over instruction */
    rv->PC += 4;
    return true;
}

static inline bool op_msub(struct riscv_t *rv, uint32_t insn)
{
    const uint32_t rd = dec_rd(insn);
    const uint32_t rs1 = dec_rs1(insn);
    const uint32_t rs2 = dec_rs2(insn);
    const uint32_t rs3 = dec_r4type_rs3(insn);

    /* compute */
    rv->F[rd] = rv->F[rs1] * rv->F[rs2] - rv->F[rs3];

    /* step over instruction */
    rv->PC += 4;
    return true;
}

static inline bool op_nmsub(struct riscv_t *rv, uint32_t insn)
{
    const uint32_t rd = dec_rd(insn);
    const uint32_t rs1 = dec_rs1(insn);
    const uint32_t rs2 = dec_rs2(insn);
    const uint32_t rs3 = dec_r4type_rs3(insn);

    /* compute */
    rv->F[rd] = rv->F[rs3] - (rv->F[rs1] * rv->F[rs2]);

    /* step over instruction */
    rv->PC += 4;
    return true;
}

static inline bool op_nmadd(struct riscv_t *rv, uint32_t insn)
{
    const uint32_t rd = dec_rd(insn);
    const uint32_t rs1 = dec_rs1(insn);
    const uint32_t rs2 = dec_rs2(insn);
    const uint32_t rs3 = dec_r4type_rs3(insn);

    /* compute */
    rv->F[rd] = -(rv->F[rs1] * rv->F[rs2]) - rv->F[rs3];

    /* step over instruction */
    rv->PC += 4;
    return true;
}
#else /* No RV32F support */
#define op_load_fp OP_UNIMP
#define op_store_fp OP_UNIMP
#define op_fp OP_UNIMP
#define op_madd OP_UNIMP
#define op_msub OP_UNIMP
#define op_nmsub OP_UNIMP
#define op_nmadd OP_UNIMP
#endif

#if RV32_HAS(EXT_C)
/* C.ADDI adds the non-zero sign-extended 6-bit immediate to the value in
 * register rd then writes the result to rd.
 * C.ADDI expands into addi rd, rd, nzimm[5:0].
 * C.ADDI is only valid when rd̸=x0. The code point with both rd=x0 and
 * nzimm=0 encodes the C.NOP instruction; the remaining code points with
 * either rd=x0 or nzimm=0 encode HINTs.
 */
static inline bool op_caddi(struct riscv_t *rv, uint16_t insn)
{
    /* Add 6-bit signed immediate to rds, serving as NOP for X0 register. */
    uint16_t tmp =
        (uint16_t) (((insn & FCI_IMM_12) >> 5) | (insn & FCI_IMM_6_2)) >> 2;
    const int32_t imm = (0x20 & tmp) ? 0xffffffc0 | tmp : tmp;
    const uint16_t rd = c_dec_rd(insn);

    /* dispatch operation type */
    if (rd != 0) { /* C.ADDI */
        rv->X[rd] += imm;
    } else { /* C.NOP */
             /* nothing */
    }

    /* step over instruction */
    rv->PC += rv->insn_len;

    /* enforce zero register */
    if (rd == rv_reg_zero)
        rv->X[rv_reg_zero] = 0;
    return true;
}

/* C.ADDI4SPN
 *
 * C.ADDI4SPN is a CIW-format instruction that adds a zero-extended non-zero
 * immediate, scaledby 4, to the stack pointer, x2, and writes the result to
 * rd'.
 * This instruction is used to generate pointers to stack-allocated variables,
 * and expands to addi rd', x2, nzuimm[9:2].
 */
static inline bool op_caddi4spn(struct riscv_t *rv, uint16_t insn)
{
    uint16_t tmp = 0;
    tmp |= (insn & 0x1800) >> 7;
    tmp |= (insn & 0x780) >> 1;
    tmp |= (insn & 0x40) >> 4;
    tmp |= (insn & 0x20) >> 2;

    const uint16_t imm = tmp;
    const uint16_t rd = c_dec_rdc(insn) | 0x08;
    rv->X[rd] = rv->X[2] + imm;

    rv->PC += rv->insn_len;
    return true;
}

/* C.LI
 *
 * C.LI loads the sign-extended 6-bit immediate, imm, into register rd.
 * C.LI expands into addi rd, x0, imm[5:0].
 * C.LI is only valid when rd=x0; the code points with rd=x0 encode HINTs.
 */
static inline bool op_cli(struct riscv_t *rv, uint16_t insn)
{
    uint16_t tmp = (uint16_t) ((insn & 0x1000) >> 7 | (insn & 0x7c) >> 2);
    const int32_t imm = (tmp & 0x20) ? 0xffffffc0 | tmp : tmp;
    const uint16_t rd = c_dec_rd(insn);
    rv->X[rd] = imm;

    rv->PC += rv->insn_len;
    return true;
}

/* C.LUI loads the non-zero 6-bit immediate field into bits 17–12 of the
 * destination register, clears the bottom 12 bits, and sign-extends bit
 * 17 into all higher bits of the destination.
 * C.LUI expands into lui rd, nzimm[17:12].
 * C.LUI is only valid when rd̸={x0, x2}, and when the immediate is not equal
 * to zero.
 *
 * C.LUI nzimm[17] dest̸={0, 2} nzimm[16:12] C1
 */
static inline bool op_clui(struct riscv_t *rv, uint16_t insn)
{
    const uint16_t rd = c_dec_rd(insn);
    if (rd == 2) { /* C.ADDI16SP */
        /* C.ADDI16SP is used to adjust the stack pointer in procedure
         * prologues and epilogues.
         * It expands into addi x2, x2, nzimm[9:4].
         * C.ADDI16SP is only valid when nzimm̸=0; the code point with nzimm=0
         * is reserved.
         */
        uint32_t tmp = (insn & 0x1000) >> 3;
        tmp |= (insn & 0x40) >> 2;
        tmp |= (insn & 0x20) << 1;
        tmp |= (insn & 0x18) << 4;
        tmp |= (insn & 0x4) << 3;
        const uint32_t imm = (tmp & 0x200) ? (0xfffffc00 | tmp) : tmp;

        if (imm != 0)
            rv->X[rd] += imm;
        else {
            /* Code point: nzimm == 0 is reserved */
        }
    } else if (rd != 0) { /* C.LUI */
        uint32_t tmp = (insn & 0x1000) << 5 | (insn & 0x7c) << 10;
        const int32_t imm = (tmp & 0x20000) ? (0xfffc0000 | tmp) : tmp;
        if (imm != 0)
            rv->X[rd] = imm;
        else {
            /* Code point 1: nzimm == 0 is reserved */
        }
    } else {
        /* Code point 2: rd==x0 is HINTS */
    }

    rv->PC += rv->insn_len;
    return true;
}

/* C.SRLI is a CB-format instruction that performs a logical right shift of
 * the value in register rd' then writes the result to rd'. The shift amount
 * is encoded in the shamt field.
 * C.SRLI expands into srli rd', rd', shamt[5:0].
 */
static inline bool op_csrli(struct riscv_t *rv, uint16_t insn)
{
    uint32_t tmp = 0;
    tmp |= (insn & 0x1000) >> 7;
    tmp |= (insn & 0x007C) >> 2;

    const uint32_t shamt = tmp;
    const uint32_t rs1 = c_dec_rs1c(insn) | 0x08;

    /* Code point 1: shamt[t]==1 are reserved */
    if (shamt & 0x20)
        return true;
    /* Code point 2: rd == x0 is HINTS */
    if (rs1 == 0)
        return true;
    /* Code point 3: shamt == 0 is HINT */
    if (shamt == 0)
        return true;

    rv->X[rs1] >>= shamt;

    return true;
}

/* C.SRAI is defined analogously to C.SRLI, but instead performs an arithmetic
 * right shift.
 * C.SRAI expands to srai rd', rd', shamt[5:0].
 */
static inline bool op_csrai(struct riscv_t *rv, uint16_t insn)
{
    uint32_t tmp = 0;
    tmp |= (insn & 0x1000) >> 7;
    tmp |= (insn & 0x007C) >> 2;

    const uint32_t shamt = tmp;
    const uint32_t rs1 = c_dec_rs1c(insn) | 0x08;

    /* Code point 1: shamt[5] == 1 is reserved */
    if (shamt & 0x20)
        return true;
    /* Code point 2: shame == 0 is HINT */
    if (shamt == 0)
        return true;
    /* Code point 3: rs1 == x0 is HINT */
    if (rs1 == rv_reg_zero)
        return true;

    const uint32_t mask = 0x80000000 & rv->X[rs1];
    rv->X[rs1] >>= shamt;

    for (unsigned int i = 0; i < shamt; ++i)
        rv->X[rs1] |= mask >> i;

    return true;
}

/* C.ANDI is a CB-format instruction that computes the bitwise AND of the value
 * in register rd' and the sign-extended 6-bit immediate, then writes the
 * result to rd'.
 * C.ANDI expands to andi rd', rd', imm[5:0].
 */
static inline bool op_candi(struct riscv_t *rv, uint16_t insn)
{
    const uint16_t mask = (0x1000 & insn) << 3;

    uint16_t tmp = 0;
    for (int i = 0; i <= 10; ++i)
        tmp |= (mask >> i);
    tmp |= (insn & 0x007C) >> 2;

    const uint32_t imm = sign_extend_h(tmp);
    const uint32_t rs1 = c_dec_rs1c(insn) | 0x08;

    rv->X[rs1] &= imm;

    return true;
}

static inline bool op_cmisc_alu(struct riscv_t *rv, uint16_t insn)
{
    /* Find actual instruction */
    switch ((insn & 0x0C00) >> 10) {
    case 0: /* C.SRLI */
        op_csrli(rv, insn);
        break;
    case 1: /* C.SRAI */
        op_csrai(rv, insn);
        break;
    case 2: /* C.ANDI */
        op_candi(rv, insn);
        break;
    case 3:; /* Arithmistic */
        uint32_t tmp = 0;
        tmp |= (insn & 0x1000) >> 10;
        tmp |= (insn & 0x0060) >> 5;

        const uint32_t funct = tmp;
        const uint32_t rs1 = c_dec_rs1c(insn) | 0x08;
        const uint32_t rs2 = c_dec_rs2c(insn) | 0x08;
        const uint32_t rd = rs1;

        switch (funct) {
        case 0: /* SUB */
            rv->X[rd] = rv->X[rs1] - rv->X[rs2];
            break;
        case 1: /* XOR */
            rv->X[rd] = rv->X[rs1] ^ rv->X[rs2];
            break;
        case 2: /* OR */
            rv->X[rd] = rv->X[rs1] | rv->X[rs2];
            break;
        case 3: /* AND */
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

    rv->PC += rv->insn_len;
    return true;
}

/* C.SLLI is a CI-format instruction that performs a logical left shift of the
 * value in register rd then writes the result to rd. The shift amount is
 * encoded in the shamt field.
 * C.SLLI expands into slli rd, rd, shamt[5:0].
 */
static inline bool op_cslli(struct riscv_t *rv, uint16_t insn)
{
    uint32_t tmp = 0;
    tmp |= (insn & FCI_IMM_12) >> 7;
    tmp |= (insn & FCI_IMM_6_2) >> 2;

    const uint32_t shamt = tmp;
    const uint32_t rd = c_dec_rd(insn);

    if (rd)
        rv->X[rd] <<= shamt;

    rv->PC += rv->insn_len;
    return true;
}

/* CI-type */
static inline bool op_clwsp(struct riscv_t *rv, uint16_t insn)
{
    uint16_t tmp = 0;
    tmp |= (insn & 0x70) >> 2;
    tmp |= (insn & 0x0c) << 4;
    tmp |= (insn & 0x1000) >> 7;

    const uint16_t imm = tmp;
    const uint16_t rd = c_dec_rd(insn);
    const uint32_t addr = rv->X[rv_reg_sp] + imm;

    /* reserved for rd == 0 */
    if (rd == 0)
        return true;

    if (addr & 3) {
        rv_except_load_misaligned(rv, addr);
        return false;
    }

    rv->X[rd] = rv->io.mem_read_w(rv, addr);
    rv->PC += rv->insn_len;
    return true;
}

/* CSS-type */
static inline bool op_cswsp(struct riscv_t *rv, uint16_t insn)
{
    const uint16_t imm = (insn & 0x1e00) >> 7 | (insn & 0x180) >> 1;
    const uint16_t rs2 = c_dec_rs2(insn);
    const uint32_t addr = rv->X[2] + imm;
    const uint32_t data = rv->X[rs2];

    if (addr & 3) {
        rv_except_store_misaligned(rv, addr);
        return false;
    }
    rv->io.mem_write_w(rv, addr, data);

    rv->PC += rv->insn_len;
    return true;
}

/* C.LW: CL-type
 *
 * C.LW loads a 32-bit value from memory into register rd'. It computes an
 * ffective address by adding the zero-extended offset, scaled by 4, to the
 * base address in register rs1'.
 * It expands to  # lw rd', offset[6:2](rs1').
 */
static inline bool op_clw(struct riscv_t *rv, uint16_t insn)
{
    uint16_t tmp = 0;
    tmp |= (insn & 0b0000000001000000) >> 4;
    tmp |= (insn & FC_IMM_12_10) >> 7;
    tmp |= (insn & 0b0000000000100000) << 1;

    const uint16_t imm = tmp;
    const uint16_t rd = c_dec_rdc(insn) | 0x08;
    const uint16_t rs1 = c_dec_rs1c(insn) | 0x08;
    const uint32_t addr = rv->X[rs1] + imm;

    if (addr & 3) {
        rv_except_load_misaligned(rv, addr);
        return false;
    }
    rv->X[rd] = rv->io.mem_read_w(rv, addr);

    rv->PC += rv->insn_len;
    return true;
}

/* C.SD: CS-type
 *
 * C.SW stores a 32-bit value in register rs2' to memory. It computes an
 * effective address by adding the zero-extended offset, scaled by 4, to
 * the base address in register rs1'.
 * It expands to sw rs2', offset[6:2](rs1')
 */
static inline bool op_csw(struct riscv_t *rv, uint16_t insn)
{
    uint32_t tmp = 0;
    /*               ....xxxx....xxxx     */
    tmp |= (insn & 0b0000000001000000) >> 4;
    tmp |= (insn & FC_IMM_12_10) >> 7;
    tmp |= (insn & 0b0000000000100000) << 1;

    const uint32_t imm = tmp;
    const uint32_t rs1 = c_dec_rs1c(insn) | 0x08;
    const uint32_t rs2 = c_dec_rs2c(insn) | 0x08;
    const uint32_t addr = rv->X[rs1] + imm;
    const uint32_t data = rv->X[rs2];

    if (addr & 3) {
        rv_except_store_misaligned(rv, addr);
        return false;
    }
    rv->io.mem_write_w(rv, addr, data);

    rv->PC += rv->insn_len;
    return true;
}

/* CJ-type */
static inline bool op_cj(struct riscv_t *rv, uint16_t insn)
{
    const int32_t imm = (c_dec_cjtype_imm(insn));
    rv->PC += imm;
    if (rv->PC & 0x1) {
        rv_except_insn_misaligned(rv, rv->PC);
        return false;
    }

    /* can branch */
    return false;
}

static inline bool op_cjal(struct riscv_t *rv, uint16_t insn)
{
    const int32_t imm = sign_extend_h(c_dec_cjtype_imm(insn));
    rv->X[1] = rv->PC + 2;
    rv->PC += imm;
    if (rv->PC & 0x1) {
        rv_except_insn_misaligned(rv, rv->PC);
        return false;
    }

    /* can branch */
    return false;
}

/* CR-type
 *
 * C.J performs an unconditional control transfer. The offset is
 * sign-extended and added to the pc to form the jump target address.
 * C.J can therefore target a ±2 KiB range.
 * C.J expands to jal x0, offset[11:1].
 */
static inline bool op_ccr(struct riscv_t *rv, uint16_t insn)
{
    const uint32_t rs1 = c_dec_rs1(insn), rs2 = c_dec_rs2(insn);
    const uint32_t rd = rs1;

    switch ((insn & 0x1000) >> 12) {
    case 0:
        if (rs2) { /* C.MV */
                   /* C.MV copies the value in register rs2 into register rd.
                    * C.MV expands into add rd, x0, rs2.
                    */
            rv->X[rd] = rv->X[rs2];
            rv->PC += rv->insn_len;
            if (rd == rv_reg_zero)
                rv->X[rv_reg_zero] = 0;
        } else { /* C.JR */
            rv->PC = rv->X[rs1];
            return false;
        }
        break;
    case 1:
        if (rs1 == 0 && rs2 == 0) /* C.EBREAK */
            rv->io.on_ebreak(rv);
        else if (rs1 && rs2) { /* C.ADD */
            /* C.ADD adds the values in registers rd and rs2 and writes the
             * result to register rd.
             * C.ADD expands into add rd, rd, rs2.
             * C.ADD is only valid when rs2=x0; the code points with rs2=x0
             * correspond to the C.JALR and C.EBREAK instructions. The code
             * points with rs2=x0 and rd=x0 are HINTs.
             */
            rv->X[rd] = rv->X[rs1] + rv->X[rs2];
            rv->PC += rv->insn_len;
            if (rd == rv_reg_zero)
                rv->X[rv_reg_zero] = 0;
        } else if (rs1 && rs2 == 0) { /* C.JALR */
            /* Unconditional jump and store PC+2 to ra */
            const int32_t jump_to = rv->X[rs1];
            rv->X[rv_reg_ra] = rv->PC + rv->insn_len;
            rv->PC = jump_to;
            if (rv->PC & 0x1) {
                rv_except_insn_misaligned(rv, rv->PC);
                return false;
            }

            /* can branch */
            return false;
        } else {                    /* rs2 !=zero AND rd == zero */
            rv->PC += rv->insn_len; /* Hint */
        }
        break;
    default:
        assert(!"Should be unreachable.");
        break;
    }

    return true;
}

/* CB-type */
static inline bool op_cbeqz(struct riscv_t *rv, uint16_t insn)
{
    const uint32_t imm = sign_extend_h(c_dec_cbtype_imm(insn));
    const uint32_t rs1 = c_dec_rs1c(insn) | 0x08;
    rv->PC += (!rv->X[rs1]) ? imm : rv->insn_len;

    /* can branch */
    return false;
}

/* BEQZ performs conditional control transfers. The offset is sign-extended
 * and added to the pc to form the branch target address.
 * It can therefore target a ±256 B range. C.BEQZ takes the branch if the value
 * in register rs1' is zero.
 * It expands to beq rs1', x0, offset[8:1].
 */
static inline bool op_cbnez(struct riscv_t *rv, uint16_t insn)
{
    const uint32_t imm = sign_extend_h(c_dec_cbtype_imm(insn));
    const uint32_t rs1 = c_dec_rs1c(insn) | 0x08;
    rv->PC += (rv->X[rs1]) ? imm : rv->insn_len;

    /* can branch */
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
#endif /* RV32_HAS(EXT_C) */

/* No RV32C.F support */
#define op_cfldsp OP_UNIMP
#define op_cflwsp OP_UNIMP
#define op_cfswsp OP_UNIMP
#define op_cfsdsp OP_UNIMP
#define op_cfld OP_UNIMP
#define op_cflw OP_UNIMP
#define op_cfsw OP_UNIMP
#define op_cfsd OP_UNIMP

/* RV32 opcode handler type */
typedef bool (*opcode_t)(struct riscv_t *rv, uint32_t insn);
/* RV32C opcode handler type */
typedef bool (*c_opcode_t)(struct riscv_t *rv, uint16_t insn);

/* handler for all unimplemented opcodes */
static inline bool op_unimp(struct riscv_t *rv, uint32_t insn UNUSED)
{
    rv_except_illegal_insn(rv, insn);
    return false;
}

#if RV32_HAS(GDBSTUB)
void rv_debug(struct riscv_t *rv)
{
    if (!gdbstub_init(&rv->gdbstub, &gdbstub_ops,
                      (arch_info_t){
                          .reg_num = 33,
                          .reg_byte = 4,
                          .target_desc = TARGET_RV32,
                      },
                      GDBSTUB_COMM)) {
        return;
    }

    rv->breakpoint_map = breakpoint_map_new();

    if (!gdbstub_run(&rv->gdbstub, (void *) rv))
        return;

    breakpoint_map_destroy(rv->breakpoint_map);
    gdbstub_close(&rv->gdbstub);
}
#endif /* RV32_HAS(GDBSTUB) */

void rv_step(struct riscv_t *rv, int32_t cycles)
{
    assert(rv);
    const uint64_t cycles_target = rv->csr_cycle + cycles;
    uint32_t insn;

#define OP_UNIMP op_unimp
#if RV32_HAS(COMPUTED_GOTO)
#define OP(insn) &&op_##insn
#define TABLE_TYPE const void *
#define TABLE_TYPE_RVC const void *
#else /* !RV32_HAS(COMPUTED_GOTO) */
#define OP(insn) op_##insn
#define TABLE_TYPE const opcode_t
#define TABLE_TYPE_RVC const c_opcode_t
#endif

    /* clang-format off */
    static TABLE_TYPE jump_table[] = {
    //  000         001           010        011           100         101        110        111
        OP(load),   OP(load_fp),  OP(unimp), OP(misc_mem), OP(op_imm), OP(auipc), OP(unimp), OP(unimp), // 00
        OP(store),  OP(store_fp), OP(unimp), OP(amo),      OP(op),     OP(lui),   OP(unimp), OP(unimp), // 01
        OP(madd),   OP(msub),     OP(nmsub), OP(nmadd),    OP(fp),     OP(unimp), OP(unimp), OP(unimp), // 10
        OP(branch), OP(jalr),     OP(unimp), OP(jal),      OP(system), OP(unimp), OP(unimp), OP(unimp), // 11
    };
#if RV32_HAS(EXT_C)
    static TABLE_TYPE_RVC jump_table_rvc[] = {
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
    /* clang-format on */

#if RV32_HAS(COMPUTED_GOTO)
#if RV32_HAS(EXT_C)
#define DISPATCH_RV32C()                                            \
    insn &= 0x0000FFFF;                                             \
    int16_t c_index = (insn & FC_FUNC3) >> 11 | (insn & FC_OPCODE); \
    rv->insn_len = INSN_16;                                         \
    goto *jump_table_rvc[c_index];
#else
#define DISPATCH_RV32C()
#endif

#define DISPATCH()                                                \
    {                                                             \
        if (unlikely(rv->csr_cycle >= cycles_target || rv->halt)) \
            return;                                               \
        /* fetch the next instruction */                          \
        insn = rv->io.mem_ifetch(rv, rv->PC);                     \
        /* standard uncompressed instruction */                   \
        if ((insn & 3) == 3) {                                    \
            uint32_t index = (insn & INSN_6_2) >> 2;              \
            rv->insn_len = INSN_32;                               \
            goto *jump_table[index];                              \
        } else {                                                  \
            /* Compressed Extension Instruction */                \
            DISPATCH_RV32C()                                      \
        }                                                         \
    }

#define EXEC(instr)                          \
    {                                        \
        /* dispatch this opcode */           \
        if (unlikely(!op_##instr(rv, insn))) \
            return;                          \
        /* increment the cycles csr */       \
        rv->csr_cycle++;                     \
    }

#define TARGET(instr)         \
    op_##instr : EXEC(instr); \
    DISPATCH();

    DISPATCH();

    /* main loop */
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
#if RV32_HAS(EXT_C)
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
#if RV32_HAS(Zifencei)
    TARGET(misc_mem)
#endif
#if RV32_HAS(EXT_A)
    TARGET(amo)
#endif
#if RV32_HAS(EXT_F)
    TARGET(load_fp)
    TARGET(store_fp)
    TARGET(fp)
    TARGET(madd)
    TARGET(msub)
    TARGET(nmadd)
    TARGET(nmsub)
#endif
    TARGET(unimp)

#undef DISPATCH_RV32C
#undef DISPATCH
#undef EXEC
#undef TARGET
#else /* !RV32_HAS(COMPUTED_GOTO) */
    while (rv->csr_cycle < cycles_target && !rv->halt) {
        /* fetch the next instruction */
        insn = rv->io.mem_ifetch(rv, rv->PC);

        if ((insn & 3) == 3) { /* standard uncompressed instruction */
            uint32_t index = (insn & INSN_6_2) >> 2;
            rv->insn_len = INSN_32;

            /* dispatch this opcode */
            TABLE_TYPE op = jump_table[index];
            assert(op);
            if (!op(rv, insn))
                break;
        } else { /* Compressed Extension Instruction */
#if RV32_HAS(EXT_C)
            /* If the last 2-bit is one of 0b00, 0b01, and 0b10, it is
             * a 16-bit instruction.
             */
            insn &= 0x0000FFFF;
            const uint16_t c_index =
                (insn & FC_FUNC3) >> 11 | (insn & FC_OPCODE);
            rv->insn_len = INSN_16;

            /* dispactch c_opcode (compressed instructions) */
            TABLE_TYPE_RVC op = jump_table_rvc[c_index];
            assert(op);
            if (!op(rv, insn))
                break;
#endif
        }

        /* increment the cycles csr */
        rv->csr_cycle++;
    }
#endif /* RV32_HAS(COMPUTED_GOTO) */
}

riscv_user_t rv_userdata(struct riscv_t *rv)
{
    assert(rv);
    return rv->userdata;
}

bool rv_set_pc(struct riscv_t *rv, riscv_word_t pc)
{
    assert(rv);
#if RV32_HAS(EXT_C)
    if (pc & 1)
#else
    if (pc & 3)
#endif
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

    return ~0U;
}

struct riscv_t *rv_create(const struct riscv_io_t *io, riscv_user_t userdata)
{
    assert(io);
    struct riscv_t *rv = (struct riscv_t *) malloc(sizeof(struct riscv_t));
    memset(rv, 0, sizeof(struct riscv_t));

    /* copy over the IO interface */
    memcpy(&rv->io, io, sizeof(struct riscv_io_t));

    /* copy over the userdata */
    rv->userdata = userdata;

    /* reset */
    rv_reset(rv, 0U);

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

    /* set the reset address */
    rv->PC = pc;
    rv->insn_len = INSN_UNKNOWN;

    /* set the default stack pointer */
    rv->X[rv_reg_sp] = DEFAULT_STACK_ADDR;

    /* reset the csrs */
    rv->csr_mtvec = 0;
    rv->csr_cycle = 0;
    rv->csr_mstatus = 0;

#if RV32_HAS(EXT_F)
    /* reset float registers */
    memset(rv->F, 0, sizeof(float) * RV_NUM_REGS);
    rv->csr_fcsr = 0;
#endif

    rv->halt = false;
}

/* FIXME: provide real implementation */
void rv_stats(struct riscv_t *rv)
{
    printf("CSR cycle count: %" PRIu64 "\n", rv->csr_cycle);
}

void ebreak_handler(struct riscv_t *rv)
{
    assert(rv);
    rv_except_breakpoint(rv, rv->PC);
}

void ecall_handler(struct riscv_t *rv)
{
    assert(rv);
    rv_except_ecall_M(rv, 0);
    syscall_handler(rv);
}
