#pragma once
#include <assert.h>
#include <stdint.h>

// clang-format off
// instruction decode masks
enum {
    //                ....xxxx....xxxx
    C_RD          = 0b0000111110000000, // general
    C_RS1         = 0b0000111110000000,
    C_RS2         = 0b0000000001111100,
    C_RD_S        = 0b0000000000011100,
    C_RS1_S       = 0b0000001110000000,
    C_RS2_S       = 0b0000000000011100,
    //                ....xxxx....xxxx
    CR_FUNCT4     = 0b1111000000000000, // CR-format
    //                ....xxxx....xxxx
    CI_MASK_12    = 0b0001000000000000, // CI-format
    CI_MASK_6_4   = 0b0000000001110000,
    CI_MASK_3_2   = 0b0000000000001100,
    //                ....xxxx....xxxx
    CSS_IMM_5_2   = 0b0001111000000000, // CSS-format
    CSS_IMM_7_6   = 0b0000000110000000,
    //                ....xxxx....xxxx
    CIW_IMM_5_4   = 0b0001100000000000, // CIW-format
    CIW_IMM_9_6   = 0b0000011110000000,
    CIW_IMM_2     = 0b0000000001000000,
    CIW_IMM_3     = 0b0000000000100000,
    //                ....xxxx....xxxx
    CLWSW_IMM_5_3 = 0b0001110000000000, // C.LW, C.SW
    CLWSW_IMM_2   = 0b0000000001000000,
    CLWSW_IMM_6   = 0b0000000000100000,
    //                ....xxxx....xxxx
    CS_FUNCT6     = 0b1111110000000000, // CS-format
    CS_FUNCT2     = 0b0000000001100000,
    //                ....xxxx....xxxx
    CB_FUNCT2     = 0b0000110000000000, // C.SRLI, C.SRAI, C.ANDI
    CB_OFFSET_8   = 0b0001000000000000, // C.BEQZ, C.BNEZ
    CB_OFFSET_4_3 = 0b0000110000000000,
    CB_OFFSET_7_6 = 0b0000000001100000,
    CB_OFFSET_2_1 = 0b0000000000011000,
    CB_OFFSET_5   = 0b0000000000000100,
    //                ....xxxx....xxxx
    CJ_OFFSET_11  = 0b0001000000000000, // CJ-format
    CJ_OFFSET_4   = 0b0000100000000000,
    CJ_OFFSET_9_8 = 0b0000011000000000,
    CJ_OFFSET_10  = 0b0000000100000000,
    CJ_OFFSET_6   = 0b0000000010000000,
    CJ_OFFSET_7   = 0b0000000001000000,
    CJ_OFFSET_3_1 = 0b0000000000111000,
    CJ_OFFSET_5   = 0b0000000000000100,
};
// clang-format off

// decode rd field
static inline uint32_t dec_rd(uint16_t inst)
{
    return (inst & C_RD) >> 7;
}

// decode rs1 field
static inline uint32_t dec_rs1(uint16_t inst)
{
    return (inst & C_RS1) >> 7;
}

// decode rs2 field
static inline uint32_t dec_rs2(uint16_t inst)
{
    return (inst & C_RS2) >> 2;
}

// decode rd' field and return its correspond register
static inline uint32_t dec_rd_short(uint16_t inst)
{
    return ((inst & C_RD_S) >> 2) | 0b1000;
}

// decode rs1' field and return its correspond register
static inline uint32_t dec_rs1_short(uint16_t inst)
{
    return ((inst & C_RS1_S) >> 7) | 0b1000;
}

// decode rs2' field and return its correspond register
static inline uint32_t dec_rs2_short(uint16_t inst)
{
    return ((inst & C_RS2_S) >> 2) | 0b1000;
}

// sign extend from specific position to MSB
static inline uint32_t sign_extend(uint32_t x, uint8_t sign_position)
{
    uint32_t sign = (x >> sign_position) & 1;
    for (uint8_t i = sign_position + 1; i < 32; ++i)
        x |= sign << i;
    return x;
}

// decode CR-format instruction funct4 field
static inline uint32_t dec_cr_funct4(uint16_t inst)
{
    return (inst & CR_FUNCT4) >> 12;
}

// decode CSS-format instruction immediate
static inline uint32_t dec_css_imm(uint16_t inst)
{
    // zero-extended offset, scaled by 4
    uint32_t imm = 0;
    imm |= (inst & CSS_IMM_7_6) >> 1;
    imm |= (inst & CSS_IMM_5_2) >> 7;
    return imm;
}

// decode CIW-format instruction immediate
static inline uint32_t dec_ciw_imm(uint16_t inst)
{
    // zero-extended non-zero immediate, scaled by 4
    uint32_t imm = 0;
    imm |= (inst & CIW_IMM_9_6) >> 1;
    imm |= (inst & CIW_IMM_5_4) >> 7;
    imm |= (inst & CIW_IMM_3) >> 2;
    imm |= (inst & CIW_IMM_2) >> 4;
    assert(imm != 0);
    return imm;
}

// decode immediate of C.LW and C.SW
static inline uint32_t dec_clw_csw_imm(uint16_t inst)
{
    // zero-extended offset, scaled by 4
    uint32_t imm = 0;
    imm |= (inst & CLWSW_IMM_6) << 1;
    imm |= (inst & CLWSW_IMM_5_3) >> 7;
    imm |= (inst & CLWSW_IMM_2) >> 4;
    return imm;
}

// decode CS-format instruction funct6 field
static inline uint32_t dec_cs_funct6(uint16_t inst)
{
    return (inst & CS_FUNCT6) >> 10;
}

// decode CS-format instruction funct2 field
static inline uint32_t dec_cs_funct2(uint16_t inst)
{
    return (inst & CS_FUNCT2) >> 5;
}

// decode CB-format instruction funct2 field
static inline uint32_t dec_cb_funct2(uint16_t inst)
{
    return (inst & CB_FUNCT2) >> 10;
}

// decode immediate of branch instruction
static inline uint32_t dec_branch_imm(uint16_t inst)
{
    // sign-extended offset, scaled by 2
    uint32_t imm = 0;
    imm |= (inst & CB_OFFSET_8) >> 4;
    imm |= (inst & CB_OFFSET_7_6) << 1;
    imm |= (inst & CB_OFFSET_5) << 3;
    imm |= (inst & CB_OFFSET_4_3) >> 7;
    imm |= (inst & CB_OFFSET_2_1) >> 2;
    imm = sign_extend(imm, 8);
    return imm;
}

// decode CJ-format instruction immediate
static inline uint32_t dec_cj_imm(uint16_t inst)
{
    // sign-extended offset, scaled by 2
    uint32_t imm = 0;
    imm |= (inst & CJ_OFFSET_11) >> 1;
    imm |= (inst & CJ_OFFSET_10) << 2;
    imm |= (inst & CJ_OFFSET_9_8) >> 1;
    imm |= (inst & CJ_OFFSET_7) << 1;
    imm |= (inst & CJ_OFFSET_6) >> 1;
    imm |= (inst & CJ_OFFSET_5) << 3;
    imm |= (inst & CJ_OFFSET_4) >> 7;
    imm |= (inst & CJ_OFFSET_3_1) >> 2;
    imm = sign_extend(imm, 11);
    return imm;
}

// encode R-type instruction
static inline uint32_t enc_rtype(uint32_t funct7, uint32_t rs2, uint32_t rs1, uint32_t funct3, uint32_t rd, uint32_t opcode)
{
    uint32_t inst = 0;
    inst |= funct7 << 25;
    inst |= rs2 << 20;
    inst |= rs1 << 15;
    inst |= funct3 << 12;
    inst |= rd << 7;
    inst |= opcode;
    return inst;
}

// encode I-type instruction
static inline uint32_t enc_itype(uint32_t imm, uint32_t rs1, uint32_t funct3, uint32_t rd, uint32_t opcode)
{
    uint32_t inst = 0;
    inst |= imm << 20;
    inst |= rs1 << 15;
    inst |= funct3 << 12;
    inst |= rd << 7;
    inst |= opcode;
    return inst;
}

// encode S-type instruction
static inline uint32_t enc_stype(uint32_t imm, uint32_t rs2, uint32_t rs1, uint32_t funct3, uint32_t opcode)
{
    uint32_t inst = 0;
    inst |= (imm & 0b111111100000) << 20;
    inst |= rs2 << 20;
    inst |= rs1 << 15;
    inst |= funct3 << 12;
    inst |= (imm & 0b000000011111) << 7;
    inst |= opcode;
    return inst;
}

// encode B-type instruction
static inline uint32_t enc_btype(uint32_t imm, uint32_t rs2, uint32_t rs1, uint32_t funct3, uint32_t opcode)
{
    uint32_t inst = 0;
    inst |= (imm & 0b1000000000000) << 19;
    inst |= (imm & 0b0011111100000) << 20;
    inst |= rs2 << 20;
    inst |= rs1 << 15;
    inst |= funct3 << 12;
    inst |= (imm & 0b0000000011110) << 7;
    inst |= (imm & 0b0100000000000) >> 4;
    inst |= opcode;
    return inst;
}

// encode U-type instruction
static inline uint32_t enc_utype(uint32_t imm, uint32_t rd, uint32_t opcode)
{
    uint32_t inst = 0;
    inst |= imm;
    inst |= rd << 7;
    inst |= opcode;
    return inst;
}

// encode J-type instruction
static inline uint32_t enc_jtype(uint32_t imm, uint32_t rd, uint32_t opcode)
{
    uint32_t inst = 0;
    inst |= (imm & 0x00100000) << 11;
    inst |= (imm & 0x000007FE) << 20;
    inst |= (imm & 0x00000800) << 9;
    inst |= (imm & 0x000FF000);
    inst |= rd << 7;
    inst |= opcode;
    return inst;
}
