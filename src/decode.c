/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdlib.h>

#include "decode.h"
#include "riscv_private.h"

/* decode rd field
 * rd = insn[11:7]
 */
static inline uint32_t decode_rd(const uint32_t insn)
{
    return (insn & FR_RD) >> 7;
}

/* decode rs1 field.
 * rs1 = insn[19:15]
 */
static inline uint32_t decode_rs1(const uint32_t insn)
{
    return (insn & FR_RS1) >> 15;
}

/* decode rs2 field.
 * rs2 = insn[24:20]
 */
static inline uint32_t decode_rs2(const uint32_t insn)
{
    return (insn & FR_RS2) >> 20;
}

/* decoded funct3 field.
 * funct3 = insn[14:12]
 */
static inline uint32_t decode_funct3(const uint32_t insn)
{
    return (insn & FR_FUNCT3) >> 12;
}

/* decode funct7 field.
 * funct7 = insn[31:25]
 */
static inline uint32_t decode_funct7(const uint32_t insn)
{
    return (insn & FR_FUNCT7) >> 25;
}

/* decode U-type instruction immediate.
 * imm[31:12] = insn[31:12]
 */
static inline uint32_t decode_utype_imm(const uint32_t insn)
{
    return insn & FU_IMM_31_12;
}

/* decode J-type instruction immediate.
 * imm[20|10:1|11|19:12] = insn[31|30:21|20|19:12]
 */
static inline int32_t decode_jtype_imm(const uint32_t insn)
{
    uint32_t dst = 0;
    dst |= (insn & FJ_IMM_20);
    dst |= (insn & FJ_IMM_19_12) << 11;
    dst |= (insn & FJ_IMM_11) << 2;
    dst |= (insn & FJ_IMM_10_1) >> 9;
    /* NOTE: shifted to 2nd least significant bit */
    return ((int32_t) dst) >> 11;
}

/* decode I-type instruction immediate.
 * imm[11:0] = insn[31:20]
 */
static inline int32_t decode_itype_imm(const uint32_t insn)
{
    return ((int32_t) (insn & FI_IMM_11_0)) >> 20;
}

/* decode B-type instruction immediate.
 * imm[12] = insn[31]
 * imm[11] = insn[7]
 * imm[10:5] = insn[30:25]
 * imm[4:1] = insn[11:8]
 */
static inline int32_t decode_btype_imm(const uint32_t insn)
{
    uint32_t dst = 0;
    dst |= (insn & FB_IMM_12);
    dst |= (insn & FB_IMM_11) << 23;
    dst |= (insn & FB_IMM_10_5) >> 1;
    dst |= (insn & FB_IMM_4_1) << 12;
    /* NOTE: shifted to 2nd least significant bit */
    return ((int32_t) dst) >> 19;
}

/* decode S-type instruction immediate.
 * imm[11:5] = insn[31:25]
 * imm[4:0] = insn[11:7]
 */
static inline int32_t decode_stype_imm(const uint32_t insn)
{
    uint32_t dst = 0;
    dst |= (insn & FS_IMM_11_5);
    dst |= (insn & FS_IMM_4_0) << 13;
    return ((int32_t) dst) >> 20;
}

#if RV32_HAS(EXT_F)
/* decode R4-type rs3 field
 * rs3 = inst[31:27]
 */
static inline uint32_t decode_r4type_rs3(const uint32_t insn)
{
    return (insn & FR4_RS3) >> 27;
}
#endif

#if RV32_HAS(EXT_C)
enum {
    /* clang-format off */
    /*             ....xxxx....xxxx */
    CJ_IMM_11    = 0b0001000000000000,
    CJ_IMM_4     = 0b0000100000000000,
    CJ_IMM_9_8   = 0b0000011000000000,
    CJ_IMM_10    = 0b0000000100000000,
    CJ_IMM_6     = 0b0000000010000000,
    CJ_IMM_7     = 0b0000000001000000,
    CJ_IMM_3_1   = 0b0000000000111000,
    CJ_IMM_5     = 0b0000000000000100,
    /*             ....xxxx....xxxx */
    CB_SHAMT_5   = 0b0001000000000000,
    CB_SHAMT_4_0 = 0b0000000001111100,
    /* clang-format on */
};

/* decode rs1 field
 * rs1 = inst[11:7]
 */
static inline uint16_t c_decode_rs1(const uint16_t insn)
{
    return (uint16_t) ((insn & FC_RS1) >> 7U);
}

/* decode rs2 field
 * rs2 = inst[6:2]
 */
static inline uint16_t c_decode_rs2(const uint16_t insn)
{
    return (uint16_t) ((insn & FC_RS2) >> 2U);
}

/* decode rd field
 * rd = inst[11:7]
 */
static inline uint16_t c_decode_rd(const uint16_t insn)
{
    return (uint16_t) ((insn & FC_RD) >> 7U);
}

/* decode rs1' field
 * rs1' = inst[9:7]
 */
static inline uint16_t c_decode_rs1c(const uint16_t insn)
{
    return (uint16_t) ((insn & FC_RS1C) >> 7U);
}

/* decode rs2' field
 * rs2' = inst[4:2]
 */
static inline uint16_t c_decode_rs2c(const uint16_t insn)
{
    return (uint16_t) ((insn & FC_RS2C) >> 2U);
}

/* decode rd' field
 * rd' = inst[4:2]
 */
static inline uint16_t c_decode_rdc(const uint16_t insn)
{
    return (uint16_t) ((insn & FC_RDC) >> 2U);
}

/* decode C.ADDI4SPN nzuimm field
 * nzuimm[5:4|9:6|2|3] = inst[]
 */
static inline uint16_t c_decode_caddi4spn_nzuimm(const uint16_t insn)
{
    uint16_t tmp = 0;
    tmp |= (insn & 0x1800) >> 7;
    tmp |= (insn & 0x780) >> 1;
    tmp |= (insn & 0x40) >> 4;
    tmp |= (insn & 0x20) >> 2;
    return tmp;
}

/* decode C.ADDI16SP nzimm field
 * nzimm[9] = inst[12]
 * nzimm[4|6|8:7|5] = inst[6:2]
 */
static inline int32_t c_decode_caddi16sp_nzimm(const uint16_t insn)
{
    int32_t tmp = (insn & 0x1000) >> 3;
    tmp |= (insn & 0x40) >> 2;
    tmp |= (insn & 0x20) << 1;
    tmp |= (insn & 0x18) << 4;
    tmp |= (insn & 0x4) << 3;
    return (tmp & 0x200) ? (0xfffffc00 | tmp) : (uint32_t) tmp;
}

/* decode C.LUI nzimm field
 * nzimm[17] = inst[12]
 * nzimm[16:12] = inst[6:2]
 */
static inline uint32_t c_decode_clui_nzimm(const uint16_t insn)
{
    uint32_t tmp = (insn & 0x1000) << 5 | (insn & 0x7c) << 10;
    return (tmp & 0x20000) ? (0xfffc0000 | tmp) : tmp;
}

static inline int32_t c_decode_caddi_imm(const uint16_t insn)
{
    int32_t tmp = 0;
    uint16_t mask = (0x1000 & insn) << 3;
    for (int i = 0; i <= 10; ++i)
        tmp |= (mask >> i);
    tmp |= (insn & 0x007C) >> 2;
    return sign_extend_h(tmp);
}

/* decode CI-Format instruction immediate
 * imm[5] = inst[12]
 * imm[4:0] = inst[6:2]
 */
static inline int32_t c_decode_citype_imm(const uint16_t insn)
{
    uint32_t tmp = ((insn & FCI_IMM_12) >> 7) | ((insn & FCI_IMM_6_2) >> 2);
    return (tmp & 0x20) ? (int32_t) (0xffffffc0 | tmp) : (int32_t) tmp;
}

/* decode CJ-format instruction immediate
 * imm[11] = inst[12]
 * imm[10] = inst[8]
 * imm[9:8] = inst[10:9]
 * imm[7] = inst[6]
 * imm[6] = inst[7]
 * imm[5] = inst[2]
 * imm[4] = inst[11]
 * imm[3:1] = inst[5:3]
 */
static inline int32_t c_decode_cjtype_imm(const uint16_t insn)
{
    uint16_t tmp = 0;
    tmp |= (insn & CJ_IMM_3_1) >> 2;
    tmp |= (insn & CJ_IMM_4) >> 7;
    tmp |= (insn & CJ_IMM_5) << 3;
    tmp |= (insn & CJ_IMM_6) >> 1;
    tmp |= (insn & CJ_IMM_7) << 1;
    tmp |= (insn & CJ_IMM_9_8) >> 1;
    tmp |= (insn & CJ_IMM_10) << 2;
    tmp |= (insn & CJ_IMM_11) >> 1;

    for (int i = 1; i <= 4; ++i)
        tmp |= (0x0800 & tmp) << i;

    /* extend to 16 bit */
    return (int32_t) (int16_t) tmp;
}

/* decode CB-format shamt field
 * shamt[5] = inst[12]
 * shamt[4:0] = inst[6:2]
 */
static inline uint8_t c_decode_cbtype_shamt(const uint16_t insn)
{
    uint8_t tmp = 0;
    tmp |= (insn & CB_SHAMT_5) >> 7;
    tmp |= (insn & CB_SHAMT_4_0) >> 2;
    return tmp;
}

/* decode CB-format instruction immediate
 * imm[8] = inst[12]
 * imm[7:6] = inst[6:5]
 * imm[4:3] = inst[11:10]
 * imm[5] = inst[2]
 * imm[2:1] = inst[4:3]
 */
static inline uint16_t c_decode_cbtype_imm(const uint16_t insn)
{
    uint16_t tmp = 0;
    /*            ....xxxx....xxxx     */
    tmp |= (insn & 0b0000000000011000) >> 2;
    tmp |= (insn & 0b0000110000000000) >> 7;
    tmp |= (insn & 0b0000000000000100) << 3;
    tmp |= (insn & 0b0000000001100000) << 1;
    tmp |= (insn & 0b0001000000000000) >> 4;

    /* extend to 16 bit */
    for (int i = 1; i <= 8; ++i)
        tmp |= (0x0100 & tmp) << i;
    return tmp;
}
#endif /* RV32_HAS(EXT_C) */

/* decode I-type
 *  31       20 19   15 14    12 11   7 6      0
 * | imm[11:0] |  rs1  | funct3 |  rd  | opcode |
 */
static inline void decode_itype(rv_insn_t *ir, const uint32_t insn)
{
    ir->imm = decode_itype_imm(insn);
    ir->rs1 = decode_rs1(insn);
    ir->rd = decode_rd(insn);
}

/* decode U-type
 *  31        12 11   7 6      0
 * | imm[31:12] |  rd  | opcode |
 */
static inline void decode_utype(rv_insn_t *ir, const uint32_t insn)
{
    ir->imm = decode_utype_imm(insn);
    ir->rd = decode_rd(insn);
}

/* decode S-type
 *  31       25 24   20 19   15 14    12 11       7 6      0
 * | imm[11:5] |  rs2  |  rs1  | funct3 | imm[4:0] | opcode |
 */
static inline void decode_stype(rv_insn_t *ir, const uint32_t insn)
{
    ir->imm = decode_stype_imm(insn);
    ir->rs2 = decode_rs2(insn);
    ir->rs1 = decode_rs1(insn);
}

/* decode R-type
 *  31    25 24   20 19   15 14    12 11   7 6      0
 * | funct7 |  rs2  |  rs1  | funct3 |  rd  | opcode |
 */
static inline void decode_rtype(rv_insn_t *ir, const uint32_t insn)
{
    ir->rs2 = decode_rs2(insn);
    ir->rs1 = decode_rs1(insn);
    ir->rd = decode_rd(insn);
}

/* decode B-type
 *     31     30     25   24 20 19 15 14    12 11       8     7     6      0
 * | imm[12] | imm[10:5] | rs2 | rs1 | funct3 | imm[4:1] | imm[11] | opcode |
 */
static inline void decode_btype(rv_insn_t *ir, const uint32_t insn)
{
    ir->imm = decode_btype_imm(insn);
    ir->rs2 = decode_rs2(insn);
    ir->rs1 = decode_rs1(insn);
}

/* decode J-type
 *     31     30       21     20    19        12 11   7 6      0
 * | imm[20] | imm[10:1] | imm[11] | imm[19:12] |  rd  | opcode |
 */
static inline void decode_jtype(rv_insn_t *ir, const uint32_t insn)
{
    ir->imm = decode_jtype_imm(insn);
    ir->rd = decode_rd(insn);
}

#if RV32_HAS(EXT_F)
/* decode R4-type
 *  31   27 26    25 24   20 19   15 14    12 11   7 6      0
 * |  rs3  | funct2 |  rs2  |  rs1  | funct3 |  rd  | opcode |
 */
static inline void decode_r4type(rv_insn_t *ir, const uint32_t insn)
{
    ir->rd = decode_rd(insn);
    ir->rs1 = decode_rs1(insn);
    ir->rs2 = decode_rs2(insn);
    ir->rs3 = decode_r4type_rs3(insn);
    ir->rm = decode_funct3(insn);
}
#endif

/* LOAD: I-type
 *  31       20 19   15 14    12 11   7 6      0
 * | imm[11:0] |  rs1  | funct3 |  rd  | opcode |
 */
static inline bool op_load(rv_insn_t *ir, const uint32_t insn)
{
    /* inst imm[11:0] rs1 funct3 rd opcode
     * ----+---------+---+------+--+-------
     * LB   imm[11:0] rs1 000    rd 0000011
     * LH   imm[11:0] rs1 001    rd 0000011
     * LW   imm[11:0] rs1 010    rd 0000011
     * LD   imm[11:0] rs1 011    rd 0000011
     * LBU  imm[11:0] rs1 100    rd 0000011
     * LHU  imm[11:0] rs1 101    rd 0000011
     * LWU  imm[11:0] rs1 110    rd 0000011
     */

    /* decode I-type */
    decode_itype(ir, insn);

    /* dispatch from funct3 field */
    switch (decode_funct3(insn)) {
    case 0: /* LB: Load Byte */
        ir->opcode = rv_insn_lb;
        break;
    case 1: /* LH: Load Halfword */
        ir->opcode = rv_insn_lh;
        break;
    case 2: /* LW: Load Word */
        ir->opcode = rv_insn_lw;
        break;
    case 4: /* LBU: Load Byte Unsigned */
        ir->opcode = rv_insn_lbu;
        break;
    case 5: /* LHU: Load Halfword Unsigned */
        ir->opcode = rv_insn_lhu;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

/* OP-IMM: I-type
 *  31       20 19   15 14    12 11   7 6      0
 * | imm[11:0] |  rs1  | funct3 |  rd  | opcode |
 */
static inline bool op_op_imm(rv_insn_t *ir, const uint32_t insn)
{
    /* inst  imm[11:5] imm[4:0]   rs1 funct3 rd opcode
     * -----+---------+----------+---+------+--+-------
     * ADDI  imm[11:0]            rs1 000    rd 0010011
     * SLLI  0000000   shamt[4:0] rs1 001    rd 0010011
     * SLTI  imm[11:0]            rs1 010    rd 0010011
     * SLTIU imm[11:0]            rs1 011    rd 0010011
     * XORI  imm[11:0]            rs1 100    rd 0010011
     * SLRI  0000000   shamt[4:0] rs1 101    rd 0010011
     * SRAI  0100000   shamt[4:0] rs1 101    rd 0010011
     * ORI   imm[11:0]            rs1 110    rd 0010011
     * ANDI  imm[11:0]            rs1 111    rd 0010011
     */

    /* decode I-type */
    decode_itype(ir, insn);

    /* nop can be implemented as "addi x0, x0, 0".
     * Any integer computational instruction writing into "x0" is NOP.
     */
    if (unlikely(ir->rd == rv_reg_zero)) {
        ir->opcode = rv_insn_nop;
        return true;
    }

    /* dispatch from funct3 field */
    switch (decode_funct3(insn)) {
    case 0: /* ADDI: Add Immediate */
        ir->opcode = rv_insn_addi;
        break;
    case 1: /* SLLI: Shift Left Logical */
        ir->opcode = rv_insn_slli;
        if (unlikely(ir->imm & (1 << 5)))
            return false;
        break;
    case 2: /* SLTI: Set on Less Than Immediate */
        ir->opcode = rv_insn_slti;
        break;
    case 3: /* SLTIU: Set on Less Than Immediate Unsigned */
        ir->opcode = rv_insn_sltiu;
        break;
    case 4: /* XORI: Exclusive OR Immediate */
        ir->opcode = rv_insn_xori;
        break;
    case 5:
        /* SLL, SRL, and SRA perform logical left, logical right, and
         * arithmetic right shifts on the value in register rs1.
         */
        ir->opcode = (ir->imm & ~0x1f)
                         ? rv_insn_srai  /* SRAI: Shift Right Arithmetic */
                         : rv_insn_srli; /* SRLI: Shift Right Logical */
        if (unlikely(ir->imm & (1 << 5)))
            return false;
        break;
    case 6: /* ORI: OR Immediate */
        ir->opcode = rv_insn_ori;
        break;
    case 7: /* ANDI: AND Immediate */
        ir->opcode = rv_insn_andi;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

/* AUIPC: U-type
 *  31        12 11   7 6      0
 * | imm[31:12] |  rd  | opcode |
 */
static inline bool op_auipc(rv_insn_t *ir, const uint32_t insn)
{
    /* inst  imm[31:12] rd opcode
     * -----+----------+--+-------
     * AUPIC imm[31:12] rd 0010111
     */

    /* decode U-type */
    decode_utype(ir, insn);

    /* Any integer computational instruction writing into "x0" is NOP. */
    if (unlikely(ir->rd == rv_reg_zero)) {
        ir->opcode = rv_insn_nop;
        return true;
    }

    ir->opcode = rv_insn_auipc;
    return true;
}

/* STORE: S-type
 *  31       25 24   20 19   15 14    12 11       7 6      0
 * | imm[11:5] |  rs2  |  rs1  | funct3 | imm[4:0] | opcode |
 */
static inline bool op_store(rv_insn_t *ir, const uint32_t insn)
{
    /* inst imm[11:5] rs2 rs1 funct3 imm[4:0] opcode
     * ----+---------+---+---+------+--------+-------
     * SB   imm[11:5] rs2 rs1 000    imm[4:0] 0100011
     * SH   imm[11:5] rs2 rs1 001    imm[4:0] 0100011
     * SW   imm[11:5] rs2 rs1 010    imm[4:0] 0100011
     * SD   imm[11:5] rs2 rs1 011    imm[4:0] 0100011
     */

    /* decode S-type */
    decode_stype(ir, insn);

    /* dispatch from funct3 field */
    switch (decode_funct3(insn)) {
    case 0: /* SB: Store Byte */
        ir->opcode = rv_insn_sb;
        break;
    case 1: /* SH: Store Halfword */
        ir->opcode = rv_insn_sh;
        break;
    case 2: /* SW: Store Word */
        ir->opcode = rv_insn_sw;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

/* OP: R-type
 *  31    25 24   20 19   15 14    12 11   7 6      0
 * | funct7 |  rs2  |  rs1  | funct3 |  rd  | opcode |
 */
static inline bool op_op(rv_insn_t *ir, const uint32_t insn)
{
    /* inst funct7  rs2 rs1 funct3 rd opcode
     * ----+-------+---+---+------+--+-------
     * ADD  0000000 rs2 rs1 000    rd 0110011
     * SUB  0100000 rs2 rs1 000    rd 0110011
     * SLL  0000000 rs2 rs1 001    rd 0110011
     * SLT  0000000 rs2 rs1 010    rd 0110011
     * SLTU 0000000 rs2 rs1 011    rd 0110011
     * XOR  0000000 rs2 rs1 100    rd 0110011
     * SRL  0000000 rs2 rs1 101    rd 0110011
     * SRA  0100000 rs2 rs1 101    rd 0110011
     * OR   0000000 rs2 rs1 110    rd 0110011
     * AND  0000000 rs2 rs1 111    rd 0110011
     */

    /* decode R-type */
    decode_rtype(ir, insn);

    /* nop can be implemented as "add x0, x1, x2" */
    if (unlikely(ir->rd == rv_reg_zero)) {
        ir->opcode = rv_insn_nop;
        return true;
    }

    uint8_t funct3 = decode_funct3(insn);

    /* dispatch from funct7 field */
    switch (decode_funct7(insn)) {
    case 0b0000000:
        switch (funct3) {
        case 0b000: /* ADD */
            ir->opcode = rv_insn_add;
            break;
        case 0b001: /* SLL: Shift Left Logical */
            ir->opcode = rv_insn_sll;
            break;
        case 0b010: /* SLT: Set on Less Than */
            ir->opcode = rv_insn_slt;
            break;
        case 0b011: /* SLTU: Set on Less Than Unsigned */
            ir->opcode = rv_insn_sltu;
            break;
        case 0b100: /* XOR: Exclusive OR */
            ir->opcode = rv_insn_xor;
            break;
        case 0b101: /* SRL: Shift Right Logical */
            ir->opcode = rv_insn_srl;
            break;
        case 0b110: /* OR */
            ir->opcode = rv_insn_or;
            break;
        case 0b111: /* AND */
            ir->opcode = rv_insn_and;
            break;
        default: /* illegal instruction */
            return false;
        }
        break;

#if RV32_HAS(EXT_M)
    /* inst   funct7  rs2 rs1 funct3 rd opcode
     * ------+-------+---+---+------+--+-------
     * MUL    0000001 rs2 rs1 000    rd 0110011
     * MULH   0000001 rs2 rs1 001    rd 0110011
     * MULHSU 0000001 rs2 rs1 010    rd 0110011
     * MULHU  0000001 rs2 rs1 011    rd 0110011
     * DIV    0000001 rs2 rs1 100    rd 0110011
     * DIVU   0000001 rs2 rs1 101    rd 0110011
     * REM    0000001 rs2 rs1 110    rd 0110011
     * REMU   0000001 rs2 rs1 111    rd 0110011
     */
    case 0b0000001: /* RV32M instructions */
        switch (funct3) {
        case 0b000: /* MUL: Multiply */
            ir->opcode = rv_insn_mul;
            break;
        case 0b001: /* MULH: Multiply High Signed Signed */
            ir->opcode = rv_insn_mulh;
            break;
        case 0b010: /* MULHSU: Multiply High Signed Unsigned */
            ir->opcode = rv_insn_mulhsu;
            break;
        case 0b011: /* MULHU: Multiply High Unsigned Unsigned */
            ir->opcode = rv_insn_mulhu;
            break;
        case 0b100: /* DIV: Divide Signed */
            ir->opcode = rv_insn_div;
            break;
        case 0b101: /* DIVU: Divide Unsigned */
            ir->opcode = rv_insn_divu;
            break;
        case 0b110: /* REM: Remainder Signed */
            ir->opcode = rv_insn_rem;
            break;
        case 0b111: /* REMU: Remainder Unsigned */
            ir->opcode = rv_insn_remu;
            break;
        default: /* illegal instruction */
            return false;
        }
        break;
#endif /* RV32_HAS(EXT_M) */

    case 0b0100000:
        switch (funct3) {
        case 0b000: /* SUB: Substract */
            ir->opcode = rv_insn_sub;
            break;
        case 0b101: /* SRA: Shift Right Arithmetic */
            ir->opcode = rv_insn_sra;
            break;
        default: /* illegal instruction */
            return false;
        }
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

/* LUI: U-type
 *  31        12 11   7 6      0
 * | imm[31:12] |  rd  | opcode |
 */
static inline bool op_lui(rv_insn_t *ir, const uint32_t insn)
{
    /* inst imm[31:12] rd opcode
     * ----+----------+--+-------
     * LUI  imm[31:12] rd 0110111
     */

    /* decode U-type */
    decode_utype(ir, insn);

    /* Any integer computational instruction writing into "x0" is NOP. */
    if (unlikely(ir->rd == rv_reg_zero)) {
        ir->opcode = rv_insn_nop;
        return true;
    }

    ir->opcode = rv_insn_lui;
    return true;
}

/* Branch: B-type
 *     31     30     25   24 20 19 15 14    12 11       8     7     6      0
 * | imm[12] | imm[10:5] | rs2 | rs1 | funct3 | imm[4:1] | imm[11] | opcode |
 */
static inline bool op_branch(rv_insn_t *ir, const uint32_t insn)
{
    /* inst imm[12] imm[10:5] rs2 rs1 funct3 imm[4:1] imm[11] opcode
     * ----+-------+---------+---+---+------+--------+-------+-------
     * BEQ  imm[12  imm[10:5] rs2 rs1 000    imm[4:1  imm[11] 1100011
     * BNE  imm[12  imm[10:5] rs2 rs1 001    imm[4:1  imm[11] 1100011
     * BLT  imm[12  imm[10:5] rs2 rs1 100    imm[4:1  imm[11] 1100011
     * BGE  imm[12  imm[10:5] rs2 rs1 101    imm[4:1  imm[11] 1100011
     * BLTU imm[12  imm[10:5] rs2 rs1 110    imm[4:1  imm[11] 1100011
     * BGEU imm[12  imm[10:5] rs2 rs1 111    imm[4:1  imm[11] 1100011
     */

    /* decode B-type */
    decode_btype(ir, insn);

    /* dispatch from funct3 field */
    switch (decode_funct3(insn)) {
    case 0: /* BEQ: Branch if Equal */
        ir->opcode = rv_insn_beq;
        break;
    case 1: /* BNE: Branch if Not Equal */
        ir->opcode = rv_insn_bne;
        break;
    case 4: /* BLT: Branch if Less Than */
        ir->opcode = rv_insn_blt;
        break;
    case 5: /* BGE: Branch if Greater Than */
        ir->opcode = rv_insn_bge;
        break;
    case 6: /* BLTU: Branch if Less Than Unsigned */
        ir->opcode = rv_insn_bltu;
        break;
    case 7: /* BGEU: Branch if Greater Than Unsigned */
        ir->opcode = rv_insn_bgeu;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

/* JALR: I-type
 *  31       20 19   15 14    12 11   7 6      0
 * | imm[11:0] |  rs1  | funct3 |  rd  | opcode |
 */
static inline bool op_jalr(rv_insn_t *ir, const uint32_t insn)
{
    /* inst imm[11:0] rs1 funct3 rd opcode
     * ----+---------+---+------+--+-------
     * JALR imm[11:0] rs1 000    rd 1100111
     */

    /* decode I-type */
    decode_itype(ir, insn);

    ir->opcode = rv_insn_jalr;
    return true;
}

/* JAL: J-type
 *     31     30       21     20    19        12 11   7 6      0
 * | imm[20] | imm[10:1] | imm[11] | imm[19:12] |  rd  | opcode |
 */
static inline bool op_jal(rv_insn_t *ir, const uint32_t insn)
{
    /* inst imm[20] imm[10:1] imm[11] imm[19:12] rd opcode
     * ----+-------+---------+-------+----------+--+-------
     * JALR imm[20] imm[10:1] imm[11] imm[19:12] rd 1101111
     */

    /* decode J-type */
    decode_jtype(ir, insn);

    ir->opcode = rv_insn_jal;
    return true;
}

FORCE_INLINE bool csr_is_writable(const uint32_t csr)
{
    return csr < 0xc00;
}

/* SYSTEM: I-type
 *  31       20 19   15 14    12 11   7 6      0
 * | imm[11:0] |  rs1  | funct3 |  rd  | opcode |
 */
static inline bool op_system(rv_insn_t *ir, const uint32_t insn)
{
    /* inst   imm[11:0]    rs1   funct3 rd    opcode
     * ------+------------+-----+------+-----+-------
     * ECALL  000000000000 00000 000    00000 1110011
     * EBREAK 000000000001 00000 000    00000 1110011
     * WFI    000100000101 00000 000    00000 1110011
     * URET   000000000010 00000 000    00000 1110011
     * SRET   000100000010 00000 000    00000 1110011
     * HRET   001000000010 00000 000    00000 1110011
     * MRET   001100000010 00000 000    00000 1110011
     */

    /* inst        funct7  rs2 rs1 funct3 rd     opcode
     * -----------+-------+---+---+------+------+-------
     * SFENCE.VMA  0001001 rs2 rs1  000   00000  1110011
     */

    /* decode I-type */
    decode_itype(ir, insn);

    /* dispatch from funct3 field */
    switch (decode_funct3(insn)) {
    case 0:
        if ((insn >> 25) == 0b0001001) { /* SFENCE.VMA */
            ir->opcode = rv_insn_sfencevma;
            break;
        }

        /* dispatch from imm field */
        switch (ir->imm) {
        case 0: /* ECALL: Environment Call */
            ir->opcode = rv_insn_ecall;
            break;
        case 1: /* EBREAK: Environment Break */
            ir->opcode = rv_insn_ebreak;
            break;
        case 0x105: /* WFI: Wait for Interrupt */
            ir->opcode = rv_insn_wfi;
            break;
        case 0x002: /* URET: return from traps in U-mode */
        case 0x202: /* HRET: return from traps in H-mode */
            /* illegal instruction */
            return false;
#if RV32_HAS(SYSTEM)
        case 0x102: /* SRET: return from traps in S-mode */
            ir->opcode = rv_insn_sret;
            break;
#endif
        case 0x302: /* MRET */
            ir->opcode = rv_insn_mret;
            break;
        default: /* illegal instruction */
            return false;
        }
        break;

#if RV32_HAS(Zicsr)
    /* All CSR instructions atomically read-modify-write a single CSR.
     * Register operand
     * ---------------------------------------------------------
     * Instruction         rd          rs1        Read    Write
     * -------------------+-----------+----------+-------+------
     * CSRRW               x0          -          no      yes
     * CSRRW               !x0         -          yes     yes
     * CSRRS/C             -           x0         yes     no
     * CSRRS/C             -           !x0        yes     yes
     *
     * Immediate operand
     * --------------------------------------------------------
     * Instruction         rd          uimm       Read    Write
     * -------------------+-----------+----------+-------+------
     * CSRRWI              x0          -          no      yes
     * CSRRWI              !x0         -          yes     yes
     * CSRRS/CI            -           0          yes     no
     * CSRRS/CI            -           !0         yes     yes
     */

    /* inst   imm[11:0] rs1  funct3 rd opcode
     * ------+---------+----+------+--+--------
     * CSRRW  csr       rs1  001    rd 1110011
     * CSRRS  csr       rs1  010    rd 1110011
     * CSRRC  csr       rs1  011    rd 1110011
     * CSRRWI csr       uimm 101    rd 1110011
     * CSRRSI csr       uimm 110    rd 1110011
     * CSRRCI csr       uimm 111    rd 1110011
     */
    case 1: /* CSRRW: Atomic Read/Write CSR */
        ir->opcode = rv_insn_csrrw;
        break;
    case 2: /* CSRRS: Atomic Read and Set Bits in CSR */
        ir->opcode = rv_insn_csrrs;
        break;
    case 3: /* CSRRC: Atomic Read and Clear Bits in CSR */
        ir->opcode = rv_insn_csrrc;
        break;
    case 5: /* CSRRWI */
        ir->opcode = rv_insn_csrrwi;
        break;
    case 6: /* CSRRSI */
        ir->opcode = rv_insn_csrrsi;
        break;
    case 7: /* CSRRCI */
        ir->opcode = rv_insn_csrrci;
        break;
#endif /* RV32_HAS(Zicsr) */

    default: /* illegal instruction */
        return false;
    }

    return csr_is_writable(ir->imm) || (ir->rs1 == rv_reg_zero);
}

/* MISC-MEM: I-type
 *  31       20 19   15 14    12 11   7 6      0
 * | imm[11:0] |  rs1  | funct3 |  rd  | opcode |
 */
static inline bool op_misc_mem(rv_insn_t *ir, const uint32_t insn)
{
    /* inst      fm       pred      succ       rs1   funct3  rd   opcode
     * ------+---------+----------+-----------+-----+-------+----+-------
     * FENCE   FM[3:0]   pred[3:0]  succ[3:0]  rs1   000     rd   0001111
     * FENCEI            imm[11:0]             rs1   001     rd   0001111
     */
    const uint32_t funct3 = decode_funct3(insn);

    switch (funct3) {
    case 0b000:
        ir->opcode = rv_insn_fence;
        return true;
#if RV32_HAS(Zifencei)
    case 0b001:
        ir->opcode = rv_insn_fencei;
        return true;
#endif       /* RV32_HAS(Zifencei) */
    default: /* illegal instruction */
        return false;
    }

    return false;
}

#if RV32_HAS(EXT_A)
/* AMO: R-type
 *  31    27  26   25  24   20 19   15 14    12 11   7 6      0
 * | funct5 | aq | rl |  rs2  |  rs1  | funct3 |  rd  | opcode |
 */
static inline bool op_amo(rv_insn_t *ir, const uint32_t insn)
{
    /* inst      funct5 aq rl rs2   rs1 funct3 rd  opcode
     * ---------+------+--+--+-----+---+------+---+-------
     * LR.W      00010  aq rl 00000 rs1 010    rd  0101111
     * SC.W      00011  aq rl rs2   rs1 010    rd  0101111
     * AMOSWAP.W 00001  aq rl rs2   rs1 010    rd  0101111
     * AMOADD.W  00000  aq rl rs2   rs1 010    rd  0101111
     * AMOXOR.W  00100  aq rl rs2   rs1 010    rd  0101111
     * AMOAND.W  01100  aq rl rs2   rs1 010    rd  0101111
     * AMOOR.W   01000  aq rl rs2   rs1 010    rd  0101111
     * AMOMIN.W  10000  aq rl rs2   rs1 010    rd  0101111
     * AMOMAX.W  10100  aq rl rs2   rs1 010    rd  0101111
     * AMOMINU.W 11000  aq rl rs2   rs1 010    rd  0101111
     * AMOMAXU.W 11100  aq rl rs2   rs1 010    rd  0101111
     */

    /* decode R-type */
    decode_rtype(ir, insn);

    /* compute funct5 field */
    const uint32_t funct5 = (decode_funct7(insn) >> 2) & 0x1f;

    /* dispatch from funct5 field */
    switch (funct5) {
    case 0b00010: /* LR.W: Load Reserved */
        ir->opcode = rv_insn_lrw;
        break;
    case 0b00011: /* SC.W: Store Conditional */
        ir->opcode = rv_insn_scw;
        break;
    case 0b00001: /* AMOSWAP.W: Atomic Swap */
        ir->opcode = rv_insn_amoswapw;
        break;
    case 0b00000: /* AMOADD.W: Atomic ADD */
        ir->opcode = rv_insn_amoaddw;
        break;
    case 0b00100: /* AMOXOR.W: Atomic XOR */
        ir->opcode = rv_insn_amoxorw;
        break;
    case 0b01100: /* AMOAND.W: Atomic AND */
        ir->opcode = rv_insn_amoandw;
        break;
    case 0b01000: /* AMOOR.W: Atomic OR */
        ir->opcode = rv_insn_amoorw;
        break;
    case 0b10000: /* AMOMIN.W: Atomic MIN */
        ir->opcode = rv_insn_amominw;
        break;
    case 0b10100: /* AMOMAX.W: Atomic MAX */
        ir->opcode = rv_insn_amomaxw;
        break;
    case 0b11000: /* AMOMINU.W */
        ir->opcode = rv_insn_amominuw;
        break;
    case 0b11100: /* AMOMAXU.W */
        ir->opcode = rv_insn_amomaxuw;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}
#else
#define op_amo OP_UNIMP
#endif /* RV32_HAS(EXT_A) */

#if RV32_HAS(EXT_F)
/* LOAD-FP: I-type
 *  31       20 19   15 14   12 11   7 6      0
 * | imm[11:0] |  rs1  | width |  rd  | opcode |
 */
static inline bool op_load_fp(rv_insn_t *ir, const uint32_t insn)
{
    /* inst imm[11:0] rs1 width rd opcode
     * ----+---------+---+-----+--+-------
     * FLW  imm[11:0] rs1 010   rd 0000111
     */

    /* decode I-type */
    decode_itype(ir, insn);

    ir->opcode = rv_insn_flw;
    return true;
}

/* STORE-FP: S-type
 *  31       25 24   20 19   15 14   12 11       7 6      0
 * | imm[11:5] |  rs2  |  rs1  | width | imm[4:0] | opcode |
 */
static inline bool op_store_fp(rv_insn_t *ir, const uint32_t insn)
{
    /* inst imm[11:5] rs2 rs1 width imm[4:0] opcode
     * ----+---------+---+---+-----+--------+-------
     * FSW  imm[11:5] rs2 rs1 010   imm[4:0] 0100111
     */

    /* decode S-type */
    decode_stype(ir, insn);

    ir->opcode = rv_insn_fsw;
    return true;
}

/* OP-FP: R-type
 *  31    27 26   25 24   20 19   15 14    12 11   7 6      0
 * | funct5 |  fmt  |  rs2  |  rs1  |   rm   |  rd  | opcode |
 */
static inline bool op_op_fp(rv_insn_t *ir, const uint32_t insn)
{
    /* inst      funct7  rs2   rs1 rm  rd opcode
     * ---------+-------+-----+---+---+--+-------
     * FADD.S    0000000 rs2   rs1 rm  rd 1010011
     * FSUB.S    0000100 rs2   rs1 rm  rd 1010011
     * FMUL.S    0001000 rs2   rs1 rm  rd 1010011
     * FDIV.S    0001100 rs2   rs1 rm  rd 1010011
     * FSQRT.S   0101100 00000 rs1 rm  rd 1010011
     * FMV.W.X   1111000 00000 rs1 000 rd 1010011
     * FSGNJ.S   0010000 rs2   rs1 000 rd 1010011
     * FSGNJN.S  0010000 rs2   rs1 001 rd 1010011
     * FSGNJX.S  0010000 rs2   rs1 010 rd 1010011
     * FCVT.W.S  1100000 00000 rs1 rm  rd 1010011
     * FCVT.WU.S 1100000 00001 rs1 rm  rd 1010011
     * FMIN.S    0010100 rs2   rs1 000 rd 1010011
     * FMAX.S    0010100 rs2   rs1 001 rd 1010011
     * FMV.X.W   1110000 00000 rs1 000 rd 1010011
     * FCLASS.S  1110000 00000 rs1 001 rd 1010011
     * FEQ.S     1010000 rs2   rs1 010 rd 1010011
     * FLT.S     1010000 rs2   rs1 001 rd 1010011
     * FLE.S     1010000 rs2   rs1 000 rd 1010011
     * FCVT.S.W  1101000 00000 rs1 rm  rd 1010011
     * FCVT.S.WU 1101000 00001 rs1 rm  rd 1010011
     */

    /* decode R-type */
    ir->rm = decode_funct3(insn);
    decode_rtype(ir, insn);

    /* dispatch from funct7 field */
    switch (decode_funct7(insn)) {
    case 0b0000000: /* FADD.S */
        ir->opcode = rv_insn_fadds;
        break;
    case 0b0000100: /* FSUB.S */
        ir->opcode = rv_insn_fsubs;
        break;
    case 0b0001000: /* FMUL.S */
        ir->opcode = rv_insn_fmuls;
        break;
    case 0b0001100: /* FDIV.S */
        ir->opcode = rv_insn_fdivs;
        break;
    case 0b0101100: /* FSQRT.S */
        ir->opcode = rv_insn_fsqrts;
        break;
    case 0b0010000:
        /* dispatch from rm region */
        switch (ir->rm) {
        case 0b000: /* FSGNJ.S */
            ir->opcode = rv_insn_fsgnjs;
            break;
        case 0b001: /* FSGNJN.S */
            ir->opcode = rv_insn_fsgnjns;
            break;
        case 0b010: /* FSGNJX.S */
            ir->opcode = rv_insn_fsgnjxs;
            break;
        default: /* illegal instruction */
            return false;
        }
        break;
    case 0b1100000:
        /* dispatch from rs2 region */
        switch (ir->rs2) {
        case 0b00000: /* FCVT.W.S */
            ir->opcode = rv_insn_fcvtws;
            break;
        case 0b00001: /* FCVT.WU.S */
            ir->opcode = rv_insn_fcvtwus;
            break;
        }
        break;
    case 0b0010100:
        /* dispatch from rm region */
        switch (ir->rm) {
        case 0b000: /* FMIN.S */
            ir->opcode = rv_insn_fmins;
            break;
        case 0b001: /* FMAX.S */
            ir->opcode = rv_insn_fmaxs;
            break;
        default: /* illegal instruction */
            return false;
        }
        break;
    case 0b1110000:
        /* dispatch from rm region */
        switch (ir->rm) {
        case 0b000: /* FMV.X.W */
            ir->opcode = rv_insn_fmvxw;
            break;
        case 0b001: /* FCLASS.S */
            ir->opcode = rv_insn_fclasss;
            break;
        default: /* illegal instruction */
            return false;
        }
        break;
    case 0b1010000:
        /* dispatch from rm region */
        switch (ir->rm) {
        case 0b010: /* FEQ.S */
            ir->opcode = rv_insn_feqs;
            break;
        case 0b001: /* FLT.S */
            ir->opcode = rv_insn_flts;
            break;
        case 0b000: /* FLE.S */
            ir->opcode = rv_insn_fles;
            break;
        default: /* illegal instruction */
            return false;
        }
        break;
    case 0b1101000:
        /* dispatch from rs2 region */
        switch (ir->rs2) {
        case 0b00000: /* FCVT.S.W */
            ir->opcode = rv_insn_fcvtsw;
            break;
        case 0b00001: /* FCVT.S.WU */
            ir->opcode = rv_insn_fcvtswu;
            break;
        default: /* illegal instruction */
            return false;
        }
        break;
    case 0b1111000: /* FMV.W.X */
        ir->opcode = rv_insn_fmvwx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

/* F-MADD: R4-type
 *  31   27 26   25 24   20 19   15 14    12 11   7 6      0
 * |  rs3  |  fmt  |  rs2  |  rs1  |   rm   |  rd  | opcode |
 */
static inline bool op_madd(rv_insn_t *ir, const uint32_t insn)
{
    /* inst    rs3 fmt rs2 rs1 rm rd opcode
     * -------+---+---+---+---+--+--+-------
     * FMADD.S rs3 00  rs2 rs1 rm rd 1000011
     */

    /* decode R4-type */
    decode_r4type(ir, insn);

    ir->opcode = rv_insn_fmadds;
    return true;
}

/* F-MSUB: R4-type
 *  31   27 26   25 24   20 19   15 14    12 11   7 6      0
 * |  rs3  |  fmt  |  rs2  |  rs1  |   rm   |  rd  | opcode |
 */
static inline bool op_msub(rv_insn_t *ir, const uint32_t insn)
{
    /* inst    rs3 fmt rs2 rs1 rm rd opcode
     * -------+---+---+---+---+--+--+-------
     * FMSUB.S rs3 00  rs2 rs1 rm rd 1000111
     */

    /* decode R4-type */
    decode_r4type(ir, insn);

    ir->opcode = rv_insn_fmsubs;
    return true;
}

/* F-NMADD: R4-type
 *  31   27 26   25 24   20 19   15 14    12 11   7 6      0
 * |  rs3  |  fmt  |  rs2  |  rs1  |   rm   |  rd  | opcode |
 */
static inline bool op_nmadd(rv_insn_t *ir, const uint32_t insn)
{
    /* inst    rs3 fmt rs2 rs1 rm rd opcode
     * -------+---+---+---+---+--+--+-------
     * FMSUB.S rs3 00  rs2 rs1 rm rd 1001111
     */

    /* decode R4-type */
    decode_r4type(ir, insn);

    ir->opcode = rv_insn_fnmadds;
    return true;
}

/* F-NMSUB: R4-type
 *  31   27 26   25 24   20 19   15 14    12 11   7 6      0
 * |  rs3  |  fmt  |  rs2  |  rs1  |   rm   |  rd  | opcode |
 */
static inline bool op_nmsub(rv_insn_t *ir, const uint32_t insn)
{
    /* inst    rs3 fmt rs2 rs1 rm rd opcode
     * -------+---+---+---+---+--+--+-------
     * FMSUB.S rs3 00  rs2 rs1 rm rd 1001011
     */

    /* decode R4-type */
    decode_r4type(ir, insn);

    ir->opcode = rv_insn_fnmsubs;
    return true;
}

#else /* !RV32_HAS(EXT_F) */
#define op_load_fp OP_UNIMP
#define op_store_fp OP_UNIMP
#define op_op_fp OP_UNIMP
#define op_madd OP_UNIMP
#define op_msub OP_UNIMP
#define op_nmadd OP_UNIMP
#define op_nmsub OP_UNIMP
#endif /* RV32_HAS(EXT_F) */

#if RV32_HAS(EXT_C)
/* C.ADDI: CI-format
 *  15    13    12    11       7 6        2 1  0
 * | funct3 | imm[5] |  rd/rs1  | imm[4:0] | op |
 */
static inline bool op_caddi(rv_insn_t *ir, const uint32_t insn)
{
    /* inst   funct3 imm[5]   rd/rs1    imm[4:0]   op
     * ------+------+--------+---------+----------+--
     * C.NOP  000    nzimm[5] 00000     nzimm[4:0] 01
     * C.ADDI 000    nzimm[5] rs1/rd!=0 nzimm[4:0] 01
     */

    ir->rd = c_decode_rd(insn);

    /* dispatch from rd/rs1 field */
    switch (ir->rd) {
    case 0: /* C.NOP */
        ir->opcode = rv_insn_cnop;
        break;
    default: /* C.ADDI */
        /* Add 6-bit signed immediate to rds, serving as NOP for X0 register. */
        ir->imm = c_decode_citype_imm(insn);
        ir->opcode = rv_insn_caddi;
        break;
    }
    return true;
}

/* C.ADDI4SPN: CIW-format
 *  15    13 12    5 4   2 1  0
 * | funct3 |  imm  | rd' | op |
 */
static inline bool op_caddi4spn(rv_insn_t *ir, const uint32_t insn)
{
    /* inst       funct3 imm                 rd' op
     * ----------+------+-------------------+---+--
     * C.ADDI4SPN 000    nzuimm[5:4|9:6|2|3] rd' 00
     */

    ir->imm = c_decode_caddi4spn_nzuimm(insn);
    ir->rd = c_decode_rdc(insn) | 0x08;

    /* Code point: nzuimm = 0 is reserved */
    if (!ir->imm)
        return false;
    ir->opcode = rv_insn_caddi4spn;
    return true;
}

/* C.LI: CI-format
 *  15    13    12    11       7 6        2 1  0
 * | funct3 | imm[5] |  rd/rs1  | imm[4:0] | op |
 */
static inline bool op_cli(rv_insn_t *ir, const uint32_t insn)
{
    /* inst funct3 imm[5] rd/rs1    imm[4:0] op
     * ----+------+------+---------+--------+--
     * C.LI 010    imm[5] rs1/rd!=0 imm[4:0] 01
     */

    ir->imm = c_decode_citype_imm(insn);
    ir->rd = c_decode_rd(insn);
    ir->opcode = rv_insn_cli;
    return true;
}

/* C.LUI: CI-format
 *  15    13    12    11       7 6        2 1  0
 * | funct3 | imm[5] |  rd/rs1  | imm[4:0] | op |
 */
static inline bool op_clui(rv_insn_t *ir, const uint32_t insn)
{
    /* inst       funct3 imm[5]    rd/rs1    imm[4:0]         op
     * ----------+------+---------+---------+----------------+--
     * C.ADDI16SP 011    nzimm[9]  2         nzimm[4|6|8:7|5] 01
     * C.LUI      011    nzimm[17] rd!={0,2} nzimm[16:12]     01
     */

    ir->rd = c_decode_rd(insn);
    /* dispatch from rd/rs1 region */
    switch (ir->rd) {
    case 0: /* Code point: rd = x0 is HINTS */
        ir->opcode = rv_insn_cnop;
        break;
    case 2: { /* C.ADDI16SP */
        ir->imm = c_decode_caddi16sp_nzimm(insn);
        /* Code point: nzimm = 0 is reserved */
        if (!(uint32_t) ir->imm)
            return false;
        ir->opcode = rv_insn_caddi16sp;
        break;
    }
    default: { /* C.LUI */
        ir->imm = c_decode_clui_nzimm(insn);
        /* Code point: nzimm = 0 is reserved */
        if (!ir->imm)
            return false;
        ir->opcode = rv_insn_clui;
        break;
    }
    }
    return true;
}

/* MISC-ALU: CB-format CA-format
 *
 * C.SRLI C.SRAI C.ANDI: CB-format
 *  15    13     12     11    10 9        7 6            2 1  0
 * | funct3 | shamt[5] | funct2 | rd'/rs1' |  shamt[4:0]  | op |
 *
 * CA-format
 *  15                        10 9        7 6      5 4    2 1  0
 * |           funct6           | rd'/rs1' | funct2 | rs2' | op |
 */
static inline bool op_cmisc_alu(rv_insn_t *ir, const uint32_t insn)
{
    /* inst   funct3 shamt[5]  funct2 rd'/rs1' shamt[4:0]  op
     * ------+------+---------+------+--------+-----------+--
     * C.SRLI 100    nzuimm[5] 00     rd'/rs1' nzuimm[4:0] 01
     * C.SRAI 100    nzuimm[5] 01     rd'/rs1' nzuimm[4:0] 01
     * C.ANDI 100    imm[5]    10     rd'/rs1' imm[4:0]    01
     * C.SUB  100    0         11     rd'/rs1' 00 rs2'     01
     * C.XOR  100    0         11     rd'/rs1' 01 rs2'     01
     * C.OR   100    0         11     rd'/rs1' 10 rs2'     01
     * C.AND  100    0         11     rd'/rs1' 11 rs2'     01
     * C.SUBW 100    1         11     rd'/rs1' 00 rs2'     01
     * C.ADDW 100    1         11     rd'/rs1' 01 rs2'     01
     */

    /* dispatch from funct2 field */
    uint8_t funct2 = (insn & 0x0C00) >> 10;
    switch (funct2) {
    case 0: /* C.SRLI */
        ir->shamt = c_decode_cbtype_shamt(insn);
        ir->rs1 = c_decode_rs1c(insn) | 0x08;

        /* Code point: shamt[5] = 1 is reserved */
        if (ir->shamt & 0x20)
            return false;

        /* Code point: rd = x0 is HINTS
         * Code point: shamt = 0 is HINTS
         */
        ir->opcode = (!ir->rs1 || !ir->shamt) ? rv_insn_cnop : rv_insn_csrli;
        break;
    case 1: /* C.SRAI */
        ir->shamt = c_decode_cbtype_shamt(insn);
        ir->rs1 = c_decode_rs1(insn);

        /* Code point: shamt[5] = 1 is reserved */
        if (ir->shamt & 0x20)
            return false;
        ir->opcode = rv_insn_csrai;
        break;
    case 2: /* C.ANDI */
        ir->rs1 = c_decode_rs1c(insn) | 0x08;
        ir->imm = c_decode_caddi_imm(insn);
        ir->opcode = rv_insn_candi;
        break;
    case 3:; /* Arithmistic */
        ir->rs1 = c_decode_rs1c(insn) | 0x08;
        ir->rs2 = c_decode_rs2c(insn) | 0x08;
        ir->rd = ir->rs1;

        /* dispatch from funct6[2] | funct2[1:0] */
        switch (((insn & 0x1000) >> 10) | ((insn & 0x0060) >> 5)) {
        case 0: /* SUB */
            ir->opcode = rv_insn_csub;
            break;
        case 1: /* XOR */
            ir->opcode = rv_insn_cxor;
            break;
        case 2: /* OR */
            ir->opcode = rv_insn_cor;
            break;
        case 3: /* AND */
            ir->opcode = rv_insn_cand;
            break;
        case 4: /* SUBW */
        case 5: /* ADDW */
            assert(!"RV64/128C instructions");
            break;
        case 6: /* Reserved */
        case 7: /* Reserved */
            assert(!"Instruction reserved");
            break;
        default:
            __UNREACHABLE;
            break;
        }
        break;
    }
    return true;
}

/* C.SLLI: CI-format
 *  15    13     12     11       7 6          2 1  0
 * | funct3 | shamt[5] |  rd/rs1  | shamt[4:0] | op |
 */
static inline bool op_cslli(rv_insn_t *ir, const uint32_t insn)
{
    /* inst   funct3 shamt[5]  rd/rs1    shamt[4:0]  op
     * ------+------+---------+---------+-----------+--
     * C.SLLI 000    nzuimm[5] rs1/rd!=0 nzuimm[4:0] 01
     */
    uint32_t tmp = 0;
    tmp |= (insn & FCI_IMM_12) >> 7;
    tmp |= (insn & FCI_IMM_6_2) >> 2;
    ir->imm = tmp;
    ir->rd = c_decode_rd(insn);
    ir->opcode = ir->rd ? rv_insn_cslli : rv_insn_cnop;
    return true;
}

/* C.LWSP: CI-format
 *  15    13  12   11   7 6   2 1  0
 * | funct3 | imm |  rd  | imm | op |
 */
static inline bool op_clwsp(rv_insn_t *ir, const uint32_t insn)
{
    /* inst   funct3 imm     rd    imm           op
     * ------+------+-------+-----+-------------+--
     * C.LWSP 000    uimm[5] rd!=0 uimm[4:2|7:6] 01
     */
    uint16_t tmp = 0;
    tmp |= (insn & 0x70) >> 2;
    tmp |= (insn & 0x0c) << 4;
    tmp |= (insn & 0x1000) >> 7;
    ir->imm = tmp;
    ir->rd = c_decode_rd(insn);

    /* reserved for rd = x0 */
    ir->opcode = ir->rd ? rv_insn_clwsp : rv_insn_cnop;
    return true;
}

/* C.SWSP: CSS-Format
 *  15    13 12    7 6   2 1  0
 * | funct3 |  imm  | rs2 | op |
 */
static inline bool op_cswsp(rv_insn_t *ir, const uint32_t insn)
{
    /* inst   funct3 imm           rs2 op
     * ------+------+-------------+---+--
     * C.LWSP 110    uimm[5:2|7:6] rs2 10
     */
    ir->imm = (insn & 0x1e00) >> 7 | (insn & 0x180) >> 1;
    ir->rs2 = c_decode_rs2(insn);
    ir->opcode = rv_insn_cswsp;
    return true;
}

/* C.LW: CL-format
 *  15    13 12   10 9    7 6   5 4   2 1  0
 * | funct3 |  imm  | rs1' | imm | rd' | op |
 */
static inline bool op_clw(rv_insn_t *ir, const uint32_t insn)
{
    /* inst funct3 imm       rs1' imm       rd' op
     * ----+------+---------+----+---------+---+--
     * C.LW 010    uimm[5:3] rs1' uimm[7:6] rd' 00
     */
    uint16_t tmp = 0;
    tmp |= (insn & 0b0000000001000000) >> 4;
    tmp |= (insn & FC_IMM_12_10) >> 7;
    tmp |= (insn & 0b0000000000100000) << 1;
    ir->imm = tmp;
    ir->rd = c_decode_rdc(insn) | 0x08;
    ir->rs1 = c_decode_rs1c(insn) | 0x08;
    ir->opcode = rv_insn_clw;
    return true;
}

/* C.SW: CS-format
 *  15    13 12   10 9    7 6   5 4    2 1  0
 * | funct3 |  imm  | rs1' | imm | rs2' | op |
 */
static inline bool op_csw(rv_insn_t *ir, const uint32_t insn)
{
    /* inst funct3 imm       rs1' imm       rs2' op
     * ----+------+---------+----+---------+----+--
     * C.SW 110    uimm[5:3] rs1' uimm[2|6] rs2' 00
     */
    uint32_t tmp = 0;
    /*               ....xxxx....xxxx     */
    tmp |= (insn & 0b0000000001000000) >> 4;
    tmp |= (insn & FC_IMM_12_10) >> 7;
    tmp |= (insn & 0b0000000000100000) << 1;
    ir->imm = tmp;
    ir->rs1 = c_decode_rs1c(insn) | 0x08;
    ir->rs2 = c_decode_rs2c(insn) | 0x08;
    ir->opcode = rv_insn_csw;
    return true;
}

/* C.J: CR-format
 *  15    13 12    2 1  0
 * | funct3 |  imm  | op |
 */
static inline bool op_cj(rv_insn_t *ir, const uint32_t insn)
{
    /* inst funct3 imm                        op
     * ----+------+--------------------------+--
     * C.J  101    imm[11|4|9:8|10|6|7|3:1|5] 01
     */
    ir->imm = c_decode_cjtype_imm(insn);
    ir->opcode = rv_insn_cj;
    return true;
}

/* C.JAL: CR-format
 *  15    13 12    2 1  0
 * | funct3 |  imm  | op |
 */
static inline bool op_cjal(rv_insn_t *ir, const uint32_t insn)
{
    /* inst  funct3 imm                        op
     * -----+------+--------------------------+--
     * C.JAL 001    imm[11|4|9:8|10|6|7|3:1|5] 01
     */
    ir->imm = sign_extend_h(c_decode_cjtype_imm(insn));
    ir->opcode = rv_insn_cjal;
    return true;
}

/* C.CR: CR-format
 *  15    12 11    7 6    2 1  0
 * | funct4 |  rs1  |  rs2 | op |
 */
static inline bool op_ccr(rv_insn_t *ir, const uint32_t insn)
{
    /* inst     funct4 rs1       rs2    op
     * --------+------+---------+------+--
     * C.JR     100    rs1!=0    0      10
     * C.MV     100    rd!=0     rs2!=0 10
     * C.EBREAK 100    0         0      10
     * C.JALR   100    rs1!=0    0      10
     * C.ADD    100    rs1/rd!=0 rs2!=0 10
     */
    ir->rs1 = c_decode_rs1(insn);
    ir->rs2 = c_decode_rs2(insn);
    ir->rd = ir->rs1;

    /* dispatch from funct4[0] field */
    switch ((insn & 0x1000) >> 12) {
    case 0:
        /* dispatch from rs2 field */
        switch (ir->rs2) {
        case 0: /* C.JR */
            /* Code point: rd = x0 is reserved */
            if (!ir->rs1)
                return false;
            ir->opcode = rv_insn_cjr;
            break;
        default: /* C.MV */
            /* Code point: rd = x0 is HINTS */
            ir->opcode = ir->rd ? rv_insn_cmv : rv_insn_cnop;
            break;
        }
        break;
    case 1:
        if (!ir->rs1 && !ir->rs2) /* C.EBREAK */
            ir->opcode = rv_insn_ebreak;
        else if (ir->rs1 && ir->rs2) { /* C.ADD */
            /* Code point: rd = x0 is HINTS */
            ir->opcode = ir->rd ? rv_insn_cadd : rv_insn_cnop;
        } else if (ir->rs1 && !ir->rs2) /* C.JALR */
            ir->opcode = rv_insn_cjalr;
        else { /* rs2 != x0 AND rs1 = x0 */
            /* Hint */
            ir->opcode = rv_insn_cnop;
        }
        break;
    default:
        __UNREACHABLE;
        break;
    }
    return true;
}

/* C.BEQZ: CB-format
 *  15    13 12     10 9    7 6       2 1  0
 * | funct3 |   imm   | rs1' |   imm   | op |
 */
static inline bool op_cbeqz(rv_insn_t *ir, const uint32_t insn)
{
    /* inst   funct3 imm        rs1' imm            op
     * ------+------+----------+----+--------------+--
     * C.BEQZ 110    imm[8|4:3] rs1' imm[7:6|2:1|5] 01
     */
    ir->imm = sign_extend_h(c_decode_cbtype_imm(insn));
    ir->rs1 = c_decode_rs1c(insn) | 0x08;
    ir->opcode = rv_insn_cbeqz;
    return true;
}

/* C.BNEZ: CB-format
 *  15    13 12     10 9    7 6       2 1  0
 * | funct3 |   imm   | rs1' |   imm   | op |
 */
static inline bool op_cbnez(rv_insn_t *ir, const uint32_t insn)
{
    /* inst   funct3 imm        rs1' imm            op
     * ------+------+----------+----+--------------+--
     * C.BNEZ 111    imm[8|4:3] rs1' imm[7:6|2:1|5] 01
     */
    ir->imm = sign_extend_h(c_decode_cbtype_imm(insn));
    ir->rs1 = c_decode_rs1c(insn) | 0x08;
    ir->opcode = rv_insn_cbnez;
    return true;
}

#else /* !RV32_HAS(EXT_C) */
#define op_caddi4spn OP_UNIMP
#define op_caddi OP_UNIMP
#define op_cslli OP_UNIMP
#define op_cjal OP_UNIMP
#define op_clw OP_UNIMP
#define op_cli OP_UNIMP
#define op_clwsp OP_UNIMP
#define op_clui OP_UNIMP
#define op_cmisc_alu OP_UNIMP
#define op_ccr OP_UNIMP
#define op_cj OP_UNIMP
#define op_csw OP_UNIMP
#define op_cbeqz OP_UNIMP
#define op_cswsp OP_UNIMP
#define op_cbnez OP_UNIMP
#endif /* RV32_HAS(EXT_C) */

#if RV32_HAS(EXT_C) && RV32_HAS(EXT_F)
/* C.FLWSP: CI-format
 *  15    13  12   11   7 6   2 1  0
 * | funct3 | imm |  rd  | imm | op |
 */
static inline bool op_cflwsp(rv_insn_t *ir, const uint32_t insn)
{
    /* inst    funct3 imm     rd    imm           op
     * -------+------+-------+-----+-------------+--
     * C.FLWSP 001    uimm[5] rd    uimm[4:2|7:6] 10
     */
    uint16_t tmp = 0;
    tmp |= (insn & 0x70) >> 2;
    tmp |= (insn & 0x0c) << 4;
    tmp |= (insn & 0x1000) >> 7;
    ir->imm = tmp;
    ir->rd = c_decode_rd(insn);
    ir->opcode = rv_insn_cflwsp;
    return true;
}

/* C.FSWSP: CSS-Format
 *  15    13 12    7 6   2 1  0
 * | funct3 |  imm  | rs2 | op |
 */
static inline bool op_cfswsp(rv_insn_t *ir, const uint32_t insn)
{
    /* inst    funct3 imm           rs2 op
     * -------+------+-------------+---+--
     * C.FSWSP 111    uimm[5:2|7:6] rs2 10
     */
    ir->imm = (insn & 0x1e00) >> 7 | (insn & 0x180) >> 1;
    ir->rs2 = c_decode_rs2(insn);
    ir->opcode = rv_insn_cfswsp;
    return true;
}

/* C.FLW: CL-format
 *  15    13 12   10 9    7 6   5 4   2 1  0
 * | funct3 |  imm  | rs1' | imm | rd' | op |
 */
static inline bool op_cflw(rv_insn_t *ir, const uint32_t insn)
{
    /* inst  funct3 imm       rs1' imm       rd' op
     * -----+------+---------+----+---------+---+--
     * C.FLW 010    uimm[5:3] rs1' uimm[7:6] rd' 00
     */
    uint16_t tmp = 0;
    tmp |= (insn & 0b0000000001000000) >> 4;
    tmp |= (insn & FC_IMM_12_10) >> 7;
    tmp |= (insn & 0b0000000000100000) << 1;
    ir->imm = tmp;
    ir->rd = c_decode_rdc(insn) | 0x08;
    ir->rs1 = c_decode_rs1c(insn) | 0x08;
    ir->opcode = rv_insn_cflw;
    return true;
}

/* C.FSW: CS-format
 *  15    13 12   10 9    7 6   5 4    2 1  0
 * | funct3 |  imm  | rs1' | imm | rs2' | op |
 */
static inline bool op_cfsw(rv_insn_t *ir, const uint32_t insn)
{
    /* inst  funct3 imm       rs1' imm       rs2' op
     * -----+------+---------+----+---------+----+--
     * C.FSW 110    uimm[5:3] rs1' uimm[2|6] rs2' 00
     */
    uint32_t tmp = 0;
    /*               ....xxxx....xxxx     */
    tmp |= (insn & 0b0000000001000000) >> 4;
    tmp |= (insn & FC_IMM_12_10) >> 7;
    tmp |= (insn & 0b0000000000100000) << 1;
    ir->imm = tmp;
    ir->rs1 = c_decode_rs1c(insn) | 0x08;
    ir->rs2 = c_decode_rs2c(insn) | 0x08;
    ir->opcode = rv_insn_cfsw;
    return true;
}

#else /* !(RV32_HAS(EXT_C) && RV32_HAS(EXT_F)) */
#define op_cfsw OP_UNIMP
#define op_cflw OP_UNIMP
#define op_cfswsp OP_UNIMP
#define op_cflwsp OP_UNIMP
#endif /* RV32_HAS(EXT_C) && RV32_HAS(EXT_F) */

/* handler for all unimplemented opcodes */
static inline bool op_unimp(rv_insn_t *ir UNUSED, uint32_t insn UNUSED)
{
    return false;
}

/* RV32 decode handler type */
typedef bool (*decode_t)(rv_insn_t *ir, uint32_t insn);

/* decode RISC-V instruction */
bool rv_decode(rv_insn_t *ir, uint32_t insn)
{
    assert(ir);

#define OP_UNIMP op_unimp
#define OP(insn) op_##insn

    /* RV32 base opcode map */
    /* clang-format off */
    static const decode_t rv_jump_table[] = {
    //  000         001           010        011           100         101        110        111
        OP(load),   OP(load_fp),  OP(unimp), OP(misc_mem), OP(op_imm), OP(auipc), OP(unimp), OP(unimp), // 00
        OP(store),  OP(store_fp), OP(unimp), OP(amo),      OP(op),     OP(lui),   OP(unimp), OP(unimp), // 01
        OP(madd),   OP(msub),     OP(nmsub), OP(nmadd),    OP(op_fp),  OP(unimp), OP(unimp), OP(unimp), // 10
        OP(branch), OP(jalr),     OP(unimp), OP(jal),      OP(system), OP(unimp), OP(unimp), OP(unimp), // 11
    };

#if RV32_HAS(EXT_C)
    /* RV32C opcode map */
    static const decode_t rvc_jump_table[] = {
    //  00             01             10          11
        OP(caddi4spn), OP(caddi),     OP(cslli),  OP(unimp),  // 000
        OP(unimp),      OP(cjal),      OP(unimp), OP(unimp),  // 001
        OP(clw),       OP(cli),       OP(clwsp),  OP(unimp),  // 010
        OP(cflw),      OP(clui),      OP(cflwsp), OP(unimp),  // 011
        OP(unimp),     OP(cmisc_alu), OP(ccr),    OP(unimp),  // 100
        OP(unimp),      OP(cj),        OP(unimp), OP(unimp),  // 101
        OP(csw),       OP(cbeqz),     OP(cswsp),  OP(unimp),  // 110
        OP(cfsw),      OP(cbnez),     OP(cfswsp), OP(unimp),  // 111
    };
#endif
    /* clang-format on */

    /* Compressed Extension Instruction */
#if RV32_HAS(EXT_C)
    /* If the last 2-bit is one of 0b00, 0b01, and 0b10, it is
     * a 16-bit instruction.
     */
    if (is_compressed(insn)) {
        insn &= 0x0000FFFF;
        const uint16_t c_index = (insn & FC_FUNC3) >> 11 | (insn & FC_OPCODE);

        /* decode instruction (compressed instructions) */
        const decode_t op = rvc_jump_table[c_index];
        assert(op);
        return op(ir, insn);
    }
#endif

    /* standard uncompressed instruction */
    const uint32_t index = (insn & INSN_6_2) >> 2;

    /* decode instruction */
    const decode_t op = rv_jump_table[index];
    assert(op);
    return op(ir, insn);

#undef OP_UNIMP
#undef OP
}
