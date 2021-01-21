#include "rvc.h"
#include "rvc_private.h"

#include <assert.h>

// C.ADDI4SPN, funct3 = 000, opcode = 00
uint32_t caddi4spn_to_addi(uint32_t inst)
{
    // decode imm and rd
    const uint32_t nzuimm = dec_ciw_imm(inst);
    const uint32_t rd = dec_rd_short(inst);

    // encode to addi rd' x2 nzuimm[9:2]
    return enc_itype(nzuimm, 2, 0b000, rd, 0b0010011);
}

// C.LW, funct3 = 010, opcode = 00
uint32_t clw_to_lw(uint32_t inst)
{
    // decode imm, rs1 and rd
    const uint32_t imm = dec_clw_csw_imm(inst);
    const uint32_t rs1 = dec_rs1_short(inst);
    const uint32_t rd = dec_rd_short(inst);

    // encode to lw rd', offset[6:2](rs1')
    return enc_itype(imm, rs1, 0b010, rd, 0b0000011);
}

// C.SW, funct3 = 110, opcode = 00
uint32_t csw_to_sw(uint32_t inst)
{
    // decode imm, rs1 and rs2
    const uint32_t imm = dec_clw_csw_imm(inst);
    const uint32_t rs1 = dec_rs1_short(inst);
    const uint32_t rs2 = dec_rs2_short(inst);

    // encode to sw rs2', offset[6:2](rs1')
    return enc_stype(imm, rs2, rs1, 0b010, 0b0100011);
}

uint32_t cnop_to_addi()
{
    // encode to addi x0 x0 0
    return enc_itype(0, 0, 0b000, 0, 0b0010011);
}

// C.ADDI, funct3 = 000, opcode = 01
uint32_t caddi_to_addi(uint32_t inst)
{
    // decode nzimm and rd
    const uint32_t rd = dec_rd(inst);
    uint32_t nzimm = 0;
    nzimm |= (inst & CI_MASK_12) >> 7;
    nzimm |= (inst & (CI_MASK_6_4 | CI_MASK_3_2)) >> 2;
    nzimm = sign_extend(nzimm, 5);

    // if nzimm == 0, marked as HINT, implement as nop
    if (nzimm == 0)
        return cnop_to_addi();

    // encode to addi rd, rd, nzimm[5:0]
    return enc_itype(nzimm, rd, 0b000, rd, 0b0010011);
}

// C.JAL, funct3 = 001, opcode = 01
uint32_t cjal_to_jal(uint32_t inst)
{
    // decode imm
    const uint32_t imm = dec_cj_imm(inst);

    // encode to jal x1, offset[11:1]
    return enc_jtype(imm, 1, 0b1101111);
}

// C.LI, funct3 = 010, opcode = 01
uint32_t cli_to_addi(uint32_t inst)
{
    // decode imm and rd
    const uint32_t rd = dec_rd(inst);
    uint32_t imm = 0;
    imm |= (inst & CI_MASK_12) >> 7;
    imm |= (inst & (CI_MASK_6_4 | CI_MASK_3_2)) >> 2;
    imm = sign_extend(imm, 5);

    // if rd == 0, marked as HINT, implement as nop
    if (rd == 0)
        return cnop_to_addi();

    // encode to addi rd, x0, imm[5:0]
    return enc_itype(imm, 0, 0b000, rd, 0b0010011);
}

// C.ADDI16SP, funct3 = 011, opcode = 01
uint32_t caddi16sp_to_addi(uint32_t inst)
{
    // decode nzimm
    uint32_t nzimm = 0;
    nzimm |= (inst & 0x1000) >> 3;
    nzimm |= (inst & 0x0018) << 4;
    nzimm |= (inst & 0x0020) << 1;
    nzimm |= (inst & 0x0004) << 3;
    nzimm |= (inst & 0x0040) >> 2;
    nzimm = sign_extend(nzimm, 9);

    // ensure nzimm != 0
    assert(nzimm != 0);

    // encode to addi x2, x2, nzimm[9:4]
    return enc_itype(nzimm, 2, 0b000, 2, 0b0010011);
}

// C.LUI, funct3 = 011, opcode = 01
uint32_t clui_to_lui(uint32_t inst)
{
    // decode nzimm and rd
    const uint32_t rd = dec_rd(inst);
    uint32_t nzimm = 0;
    nzimm |= (inst & CI_MASK_12) << 5;
    nzimm |= (inst & (CI_MASK_6_4 | CI_MASK_3_2)) << 10;
    nzimm = sign_extend(nzimm, 17);

    // ensure nzimm != 0
    assert(nzimm != 0);

    // if rd == 0, marked as HINT, implement as nop
    if (rd == 0)
        return cnop_to_addi();

    // encode to lui rd, nzuimm[17:12]
    return enc_utype(nzimm, rd, 0b0110111);
}

uint32_t csrli_to_srli(uint32_t inst)
{
    // decode shamt and rd = rs1
    uint32_t shamt = 0;
    shamt |= (inst & CI_MASK_12) >> 7;
    // shamt[5] must be zero for RV32C
    assert(shamt == 0);
    shamt |= (inst & (CI_MASK_6_4 | CI_MASK_3_2)) >> 2;
    // ensure shamt != 0
    assert(shamt != 0);

    const uint32_t rd = dec_rs1_short(inst);

    // encode to srli rd', rd', shamt[5:0]
    return enc_rtype(0b0000000, shamt, rd, 0b101, rd, 0b0010011);
}

uint32_t csrai_to_srai(uint32_t inst)
{
    // decode shamt and rd = rs1
    uint32_t shamt = 0;
    shamt |= (inst & CI_MASK_12) >> 7;
    // shamt[5] must be zero for RV32C
    assert(shamt == 0);
    shamt |= (inst & (CI_MASK_6_4 | CI_MASK_3_2)) >> 2;
    // ensure shamt != 0
    assert(shamt != 0);

    const uint32_t rd = dec_rs1_short(inst);

    // encode to srai rd', rd', shamt[5:0]
    return enc_rtype(0b0100000, shamt, rd, 0b101, rd, 0b0010011);
}

uint32_t candi_to_andi(uint32_t inst)
{
    // decode imm and rd = rs1
    const uint32_t rd = dec_rs1_short(inst);
    uint32_t imm = 0;
    imm |= (inst & CI_MASK_12) >> 7;
    imm |= (inst & (CI_MASK_6_4 | CI_MASK_3_2)) >> 2;
    imm = sign_extend(imm, 5);

    // encode to andi rd', rd', imm[5:0]
    return enc_itype(imm, rd, 0b111, rd, 0b0010011);
}

uint32_t csub_to_sub(uint32_t inst)
{
    // decode rd = rs1 and rs2
    const uint32_t rd = dec_rs1_short(inst);
    const uint32_t rs2 = dec_rs2_short(inst);

    // encode to sub rd', rd', rs2'
    return enc_rtype(0b0100000, rs2, rd, 0b000, rd, 0b0110011);
}

uint32_t cxor_to_xor(uint32_t inst)
{
    // decode rd = rs1 and rs2
    const uint32_t rd = dec_rs1_short(inst);
    const uint32_t rs2 = dec_rs2_short(inst);

    // encode to xor rd', rd', rs2'
    return enc_rtype(0b0000000, rs2, rd, 0b100, rd, 0b0110011);
}

uint32_t cor_to_or(uint32_t inst)
{
    // decode rd = rs1 and rs2
    const uint32_t rd = dec_rs1_short(inst);
    const uint32_t rs2 = dec_rs2_short(inst);

    // encode to or rd', rd', rs2'
    return enc_rtype(0b0000000, rs2, rd, 0b110, rd, 0b0110011);
}

uint32_t cand_to_and(uint32_t inst)
{
    // decode rd = rs1 and rs2
    const uint32_t rd = dec_rs1_short(inst);
    const uint32_t rs2 = dec_rs2_short(inst);

    // encode to and rd', rd', rs2'
    return enc_rtype(0b0000000, rs2, rd, 0b111, rd, 0b0110011);
}

// C.J, funct3 = 101, opcode = 01
uint32_t cj_to_jal(uint32_t inst)
{
    // decode imm
    const uint32_t imm = dec_cj_imm(inst);

    // encode to jal x0, offset[11:1]
    return enc_jtype(imm, 0, 0b1101111);
}

// C.BEQZ, funct3 = 110, opcode = 01
uint32_t cbeqz_to_beq(uint32_t inst)
{
    // decode offset and rs1
    const uint32_t offset = dec_branch_imm(inst);
    const uint32_t rs1 = dec_rs1_short(inst);

    // encode to beq rs1', x0, offset[8:1]
    return enc_btype(offset, 0, rs1, 0b000, 0b1100011);
}

// C.BENZ, funct3 = 111, opcode = 01
uint32_t cbenz_to_bne(uint32_t inst)
{
    // decode offset and rs1
    const uint32_t offset = dec_branch_imm(inst);
    const uint32_t rs1 = dec_rs1_short(inst);

    // encode to bne rs1', x0, offset[8:1]
    return enc_btype(offset, 0, rs1, 0b001, 0b1100011);
}

// C.SLLI, funct3 = 000, opcode = 10
uint32_t cslli_to_slli(uint32_t inst)
{
    // decode shamt and rd
    uint32_t shamt = 0;
    shamt |= (inst & CI_MASK_12) >> 7;
    // shamt[5] must be zero for RV32C
    assert(shamt == 0);
    shamt |= (inst & (CI_MASK_6_4 | CI_MASK_3_2)) >> 2;
    // ensure shamt != 0
    assert(shamt != 0);

    const uint32_t rd = dec_rd(inst);
    // if rd == 0, marked as HINT, implement as nop
    if (rd == 0)
        return cnop_to_addi();

    // encode to slli rd, rd, shamt[5:0]
    return enc_rtype(0b0000000, shamt, rd, 0b001, rd, 0b0010011);
}

// C.LWSP, funct3 = 010, opcode = 10
uint32_t clwsp_to_lw(uint32_t inst)
{
    // decode offset and rd
    uint32_t offset = 0;
    offset |= (inst & CI_MASK_12) >> 7;
    offset |= (inst & CI_MASK_6_4) >> 2;
    offset |= (inst & CI_MASK_3_2) << 4;

    const uint32_t rd = dec_rd(inst);
    // ensure rd != 0
    assert(rd != 0);

    // decode to lw rd, offset[7:2](x2)
    return enc_itype(offset, 2, 0b010, rd, 0b0000011);
}

uint32_t cjr_to_jalr(uint32_t inst)
{
    // decode rs1
    const uint32_t rs1 = dec_rs1(inst);
    // ensure rs1 != 0
    assert(rs1 != 0);

    // encode to jalr x0, rs1, 0
    return enc_itype(0, rs1, 0b000, 0, 0b1100111);
}

uint32_t cmv_to_add(uint32_t inst)
{
    // decode rs2 and rd
    const uint32_t rs2 = dec_rs2(inst);
    // ensure rs2 != 0
    assert(rs2 != 0);

    const uint32_t rd = dec_rd(inst);
    // if rd == 0, marked as HINT, implement as nop
    if (rd == 0)
        return cnop_to_addi();

    // encode to add rd, x0, rs2
    return enc_rtype(0b0000000, rs2, 0, 0b000, rd, 0b0110011);
}

uint32_t cebreak_to_ebreak()
{
    // return ebreak
    return enc_itype(1, 0, 0b000, 0, 0b1110011);
}

uint32_t cjalr_to_jalr(uint32_t inst)
{
    // decode rs1
    const uint32_t rs1 = dec_rs1(inst);
    // ensure rs1 != 0
    assert(rs1 != 0);

    // encode to jalr x1, rs1, 0
    return enc_itype(0, rs1, 0b000, 1, 0b1100111);
}

uint32_t cadd_to_add(uint32_t inst)
{
    // decode rs2 and rd
    const uint32_t rs2 = dec_rs2(inst);
    // ensure rs2 != 0
    assert(rs2 != 0);

    const uint32_t rd = dec_rd(inst);
    // if rd == 0, marked as HINT, implement as nop
    if (rd == 0)
        return cnop_to_addi();

    // encode to add rd, rd, rs2
    return enc_rtype(0b0000000, rs2, rd, 0b000, rd, 0b0110011);
}

// C.SWSP, funct3 = 110, opcode = 10
uint32_t cswsp_to_sw(uint32_t inst)
{
    // decode imm and rs2
    const uint32_t offset = dec_css_imm(inst);
    const uint32_t rs2 = dec_rs2(inst);

    // encode to sw rs2, offset[7:2](x2)
    return enc_stype(offset, rs2, 2, 0b010, 0b0100011);
}

// funct3 = 011, opcode = 01
uint32_t parse_011_01(uint32_t inst)
{
    const uint32_t rd = dec_rd(inst);

    if (rd == 2)
        return caddi16sp_to_addi(inst);
    else
        return clui_to_lui(inst);
}

// funct3 = 100, opcode = 01
uint32_t parse_100_01(uint32_t inst)
{
    const uint32_t cb_funct2 = dec_cb_funct2(inst);
    const uint32_t cs_funct2 = dec_cs_funct2(inst);

    switch (cb_funct2) {
    case 0b00:
        return csrli_to_srli(inst);
    case 0b01:
        return csrai_to_srai(inst);
    case 0b10:
        return candi_to_andi(inst);
    default:
        switch (cs_funct2) {
        case 0b00:
            return csub_to_sub(inst);
        case 0b01:
            return cxor_to_xor(inst);
        case 0b10:
            return cor_to_or(inst);
        case 0b11:
            return cand_to_and(inst);
        default:
            return cnop_to_addi();  // Reserved
        }
    }
}

// funct3 = 100, opcode = 10
uint32_t parse_100_10(uint32_t inst)
{
    const uint32_t cr_funct4 = dec_cr_funct4(inst);
    const uint32_t rs1 = dec_rs1(inst);
    const uint32_t rs2 = dec_rs2(inst);

    if (cr_funct4 == 0b1000) {
        if (rs2 == 0)
            return cjr_to_jalr(inst);
        else
            return cmv_to_add(inst);
    } else if (cr_funct4 == 0b1001) {
        if (rs1 == 0 && rs2 == 0)
            return cebreak_to_ebreak();
        else if (rs2 == 0)
            return cjalr_to_jalr(inst);
        else
            return cadd_to_add(inst);
    } else
        return cnop_to_addi();
}
