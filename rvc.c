#include "rvc.h"
#include <assert.h>
#include <stdio.h>

// #define DEBUG_OUTPUT
#ifdef DEBUG_OUTPUT
const char *reg[32] = {"zero", "ra", "sp",  "gp",  "tp", "t0", "t1", "t2",
                       "s0",   "s1", "a0",  "a1",  "a2", "a3", "a4", "a5",
                       "a6",   "a7", "s2",  "s3",  "s4", "s5", "s6", "s7",
                       "s8",   "s9", "s10", "s11", "t3", "t4", "t5", "t6"};
#endif

// C.ADDI4SPN, funct = 000, opcode = 00
uint32_t caddi4spn_to_addi(uint32_t inst)
{
    // parse nzuimm
    const uint16_t nzuimm = ((inst & 0x0780) >> 1) | ((inst & 0x1800) >> 7) |
                            ((inst & 0x0020) >> 2) | ((inst & 0x0040) >> 4);
    // Ensure the nzuimm != 0
    assert(nzuimm != 0);

    // parse rd
    const uint8_t rd = 0x08 | ((inst & 0x001C) >> 2);

#ifdef DEBUG_OUTPUT
    printf("addi\t%s,sp,%u\n", reg[rd], nzuimm);
#endif

    // init instruction and set nzuimm
    uint32_t inst_extend = nzuimm;
    // set rs1 = x2
    inst_extend <<= 5;
    inst_extend |= 0b00010;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b000;
    // set rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b0010011;

    return inst_extend;
}

// C.LW, funct = 010, opcode = 00
uint32_t clw_to_lw(uint32_t inst)
{
    // parse imm
    const uint16_t imm = ((inst & 0x0020) << 1) | ((inst & 0x1C00) >> 7) |
                         ((inst & 0x0040) >> 4);
    // parse rs1
    const uint8_t rs1 = 0x08 | ((inst & 0x0380) >> 7);
    // parse rd
    const uint8_t rd = 0x08 | ((inst & 0x001C) >> 2);

#ifdef DEBUG_OUTPUT
    printf("lw\t%s,%u(%s)\n", reg[rd], imm, reg[rs1]);
#endif

    // init instruction and set imm
    uint32_t inst_extend = imm;
    // set rs1
    inst_extend <<= 5;
    inst_extend |= rs1;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b010;
    // set rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b0000011;

    return inst_extend;
}

// C.SW, funct = 110, opcode = 00
uint32_t csw_to_sw(uint32_t inst)
{
    // parse imm
    const uint16_t imm = ((inst & 0x0020) << 1) | ((inst & 0x1C00) >> 7) |
                         ((inst & 0x0040) >> 4);
    // parse rs1
    const uint8_t rs1 = 0x08 | ((inst & 0x0380) >> 7);
    // parse rs2
    const uint8_t rs2 = 0x08 | ((inst & 0x001C) >> 2);

#ifdef DEBUG_OUTPUT
    printf("sw\t%s,%u(%s)\n", reg[rs2], imm, reg[rs1]);
#endif

    // init instruction and set imm[11:5]
    uint32_t inst_extend = (imm & 0xFFE0);
    // set rs2
    inst_extend |= rs2;
    // set rs1
    inst_extend <<= 5;
    inst_extend |= rs1;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b010;
    // set imm[4:0]
    inst_extend <<= 5;
    inst_extend |= (imm & 0x001F);
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b0100011;

    return inst_extend;
}

uint32_t cnop()
{
#ifdef DEBUG_OUTPUT
    printf("nop\n");
#endif
    // return addi x0 x0 0
    return 0x00000013;
}

// C.ADDI, funct = 000, opcode = 01
uint32_t caddi_to_addi(uint32_t inst)
{
    // parse nzimm
    uint16_t nzimm = ((inst & 0x007C) >> 2) | ((inst & 0x1000) >> 7);
    // if nzimm == 0, marked as HINT, implement as nop
    if (nzimm == 0) {
        return cnop();
    }
    // sign extend nzimm
    uint16_t sign = (nzimm & 0x0020) >> 5;
    for (int i = 6; i <= 11; ++i)
        nzimm |= sign << i;
    // parse rd
    const uint8_t rd = ((inst & 0x0F80) >> 7);

#ifdef DEBUG_OUTPUT
    printf("addi\t%s,%s,%d\n", reg[rd], reg[rd], nzimm);
#endif

    // init instruction and set imm
    uint32_t inst_extend = nzimm;
    // set rs1 = rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b000;
    // set rd = rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b0010011;

    return inst_extend;
}

// C.JAL, funct = 001, opcode = 01
uint32_t cjal_to_jal(uint32_t inst)
{
    // parse imm[11:0] and sign extend it
    uint32_t imm = ((inst & 0x1680) >> 1) | ((inst & 0x0100) << 2) |
                   ((inst & 0x0040) << 1) | ((inst & 0x0004) << 3) |
                   ((inst & 0x0800) >> 7) | ((inst & 0x0038) >> 2);
    uint32_t sign = (imm & 0x0800) >> 11;
    for (int i = 12; i <= 20; ++i)
        imm |= sign << i;

#ifdef DEBUG_OUTPUT
    printf("jal\t0x%x\n", imm);
#endif

    // init instruction and set imm[20|10:1|11|19:12]
    uint32_t inst_extend = 0;
    // set imm[20]
    inst_extend <<= 1;
    inst_extend |= (imm & 0x00100000) >> 20;
    // set imm[10:1]
    inst_extend <<= 10;
    inst_extend |= (imm & 0x000007FE) >> 1;
    // set imm[11]
    inst_extend <<= 1;
    inst_extend |= (imm & 0x00000800) >> 11;
    // set imm[19:12]
    inst_extend <<= 8;
    inst_extend |= (imm & 0x000FF000) >> 12;
    // set rd = x1
    inst_extend <<= 5;
    inst_extend |= 0b00001;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b1101111;

    return inst_extend;
}

// C.LI, funct = 010, opcode = 01
uint32_t cli_to_addi(uint32_t inst)
{
    // parse imm
    uint16_t imm = ((inst & 0x007C) >> 2) | ((inst & 0x1000) >> 7);
    // sign extend imm
    uint16_t sign = (imm & 0x0020) >> 5;
    for (int i = 6; i <= 11; ++i)
        imm |= sign << i;
    // parse rd
    const uint8_t rd = ((inst & 0x0F80) >> 7);
    // if rd == 0, marked as HINT, implement as nop
    if (rd == 0) {
        return cnop();
    }

#ifdef DEBUG_OUTPUT
    printf("li\t%s,%d\n", reg[rd], imm);
#endif

    // init instruction and set imm
    uint32_t inst_extend = imm;
    // set rs1 = x0
    inst_extend <<= 5;
    inst_extend |= 0b00000;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b000;
    // set rd = rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b0010011;

    return inst_extend;
}

// C.ADDI16SP, funct = 011, opcode = 01
uint32_t caddi16sp_to_addi(uint32_t inst)
{
    // parse nzimm
    uint16_t nzimm = ((inst & 0x1000) >> 3) | ((inst & 0x0018) << 4) |
                     ((inst & 0x0020) << 1) | ((inst & 0x0004) << 3) |
                     ((inst & 0x0040) >> 2);
    // ensure nzimm != 0
    assert(nzimm != 0);
    // sign extend nzimm
    uint16_t sign = (nzimm & 0x0200) >> 9;
    for (int i = 10; i <= 11; ++i)
        nzimm |= sign << i;

#ifdef DEBUG_OUTPUT
    printf("addi\tsp,sp,%d\n", nzimm);
#endif

    // init instruction and set nzimm
    uint32_t inst_extend = nzimm;
    // set rs1 = x2
    inst_extend <<= 5;
    inst_extend |= 0b00010;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b000;
    // set rd = x2
    inst_extend <<= 5;
    inst_extend |= 0b00010;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b0010011;

    return inst_extend;
}

// C.LUI, funct = 011, opcode = 01
uint32_t clui_to_lui(uint32_t inst)
{
    // parse rd
    const uint8_t rd = ((inst & 0x0F80) >> 7);

    // if rd == 0, marked as HINT, implement as nop
    if (rd == 0) {
        return cnop();
    }
    // if rd == 2, translate to addi16sp
    else if (rd == 2) {
        return caddi16sp_to_addi(inst);
    }

    // parse nzimm
    uint32_t nzimm = ((inst & 0x1000) << 5) | ((inst & 0x007C) << 10);
    // ensure nzimm != 0
    assert(nzimm != 0);
    // sign extend nzimm
    uint32_t sign = (nzimm & 0x00020000) >> 17;
    for (int i = 18; i <= 32; ++i)
        nzimm |= sign << i;

#ifdef DEBUG_OUTPUT
    printf("lui\t%s,0x%x\n", reg[rd], nzimm >> 12);
#endif

    // init instruction and set nzimm
    uint32_t inst_extend = nzimm;
    // set rd
    inst_extend |= rd << 7;
    // set opcode
    inst_extend |= 0b0110111;

    return inst_extend;
}

uint32_t csrli_to_srli(uint32_t inst)
{
    // parse shamt
    const uint8_t shamt = ((inst & 0x1000) >> 7) | ((inst & 0x007C) >> 2);
    // ensure shamt[5] == 0
    assert((shamt & 0x20) == 0);
    // ensure shamt != 0
    assert(shamt != 0);
    // parse rd
    const uint8_t rd = 0x08 | ((inst & 0x0380) >> 7);

#ifdef DEBUG_OUTPUT
    printf("srli\t%s,%s,0x%x\n", reg[rd], reg[rd], shamt);
#endif

    // init instruction and set shamt
    uint32_t inst_extend = shamt;
    // set rs1 = rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b101;
    // set rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b0010011;

    return inst_extend;
}

uint32_t csrai_to_srai(uint32_t inst)
{
    // parse shamt
    const uint8_t shamt = ((inst & 0x1000) >> 7) | ((inst & 0x007C) >> 2);
    // ensure shamt[5] == 0
    assert((shamt & 0x20) == 0);
    // ensure shamt != 0
    assert(shamt != 0);
    // parse rd
    const uint8_t rd = 0x08 | ((inst & 0x0380) >> 7);

#ifdef DEBUG_OUTPUT
    printf("srai\t%s,%s,0x%x\n", reg[rd], reg[rd], shamt);
#endif

    // init instruction and set shamt
    uint32_t inst_extend = shamt;
    // set rs1 = rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b101;
    // set rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b0010011;
    // set imm[10] for srai
    inst_extend |= 0x40000000;

    return inst_extend;
}

uint32_t candi_to_andi(uint32_t inst)
{
    // parse imm
    uint16_t imm = ((inst & 0x1000) >> 7) | ((inst & 0x007C) >> 2);
    // sign extend imm
    uint16_t sign = (imm & 0x0020) >> 5;
    for (int i = 6; i <= 11; ++i)
        imm |= sign << i;
    // parse rd
    const uint8_t rd = 0x08 | ((inst & 0x0380) >> 7);

#ifdef DEBUG_OUTPUT
    printf("andi\t%s,%s,%d\n", reg[rd], reg[rd], imm);
#endif

    // init instruction and set shamt
    uint32_t inst_extend = imm;
    // set rs1 = rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b111;
    // set rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b0010011;

    return inst_extend;
}

uint32_t csub_to_sub(uint32_t inst)
{
    // parse rd
    const uint8_t rd = 0x08 | ((inst & 0x0380) >> 7);
    // parse rs2
    const uint8_t rs2 = 0x08 | ((inst & 0x001C) >> 2);

#ifdef DEBUG_OUTPUT
    printf("sub\t%s,%s,%s\n", reg[rd], reg[rd], reg[rs2]);
#endif

    // init instruction and set funct7
    uint32_t inst_extend = 0b0100000;
    // set rs2
    inst_extend <<= 5;
    inst_extend |= rs2;
    // set rs1 = rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b000;
    // set rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b0110011;

    return inst_extend;
}

uint32_t cxor_to_xor(uint32_t inst)
{
    // parse rd
    const uint8_t rd = 0x08 | ((inst & 0x0380) >> 7);
    // parse rs2
    const uint8_t rs2 = 0x08 | ((inst & 0x001C) >> 2);

#ifdef DEBUG_OUTPUT
    printf("xor\t%s,%s,%s\n", reg[rd], reg[rd], reg[rs2]);
#endif

    // init instruction and set funct7
    uint32_t inst_extend = 0b0000000;
    // set rs2
    inst_extend <<= 5;
    inst_extend |= rs2;
    // set rs1 = rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b100;
    // set rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b0110011;

    return inst_extend;
}

uint32_t cor_to_or(uint32_t inst)
{
    // parse rd
    const uint8_t rd = 0x08 | ((inst & 0x0380) >> 7);
    // parse rs2
    const uint8_t rs2 = 0x08 | ((inst & 0x001C) >> 2);

#ifdef DEBUG_OUTPUT
    printf("or\t%s,%s,%s\n", reg[rd], reg[rd], reg[rs2]);
#endif

    // init instruction and set funct7
    uint32_t inst_extend = 0b0000000;
    // set rs2
    inst_extend <<= 5;
    inst_extend |= rs2;
    // set rs1 = rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b110;
    // set rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b0110011;

    return inst_extend;
}

uint32_t cand_to_and(uint32_t inst)
{
    // parse rd
    const uint8_t rd = 0x08 | ((inst & 0x0380) >> 7);
    // parse rs2
    const uint8_t rs2 = 0x08 | ((inst & 0x001C) >> 2);

#ifdef DEBUG_OUTPUT
    printf("and\t%s,%s,%s\n", reg[rd], reg[rd], reg[rs2]);
#endif

    // init instruction and set funct7
    uint32_t inst_extend = 0b0000000;
    // set rs2
    inst_extend <<= 5;
    inst_extend |= rs2;
    // set rs1 = rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b111;
    // set rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b0110011;

    return inst_extend;
}

// CR / CB format, funct = 100, opcode = 01
uint32_t parse_cb_cs(uint32_t inst)
{
    const uint8_t funct2 = (inst & 0x0C00) >> 10;
    const uint8_t funct = (inst & 0x0060) >> 5;
    switch (funct2) {
    case 0b00:
        return csrli_to_srli(inst);
    case 0b01:
        return csrai_to_srai(inst);
    case 0b10:
        return candi_to_andi(inst);
    case 0b11:
        switch (funct) {
        case 0b00:
            return csub_to_sub(inst);
        case 0b01:
            return cxor_to_xor(inst);
        case 0b10:
            return cor_to_or(inst);
        case 0b11:
            return cand_to_and(inst);
        };
    }
    return cnop();
}

// C.J, funct = 101, opcode = 01
uint32_t cj_to_jal(uint32_t inst)
{
    // parse imm[11:0] and sign extend it
    uint32_t imm = ((inst & 0x1680) >> 1) | ((inst & 0x0100) << 2) |
                   ((inst & 0x0040) << 1) | ((inst & 0x0004) << 3) |
                   ((inst & 0x0800) >> 7) | ((inst & 0x0038) >> 2);
    uint32_t sign = (imm & 0x0800) >> 11;
    for (int i = 12; i <= 20; ++i)
        imm |= sign << i;

#ifdef DEBUG_OUTPUT
    printf("j\t0x%x\n", imm);
#endif

    // init instruction and set imm[20|10:1|11|19:12]
    uint32_t inst_extend = 0;
    // set imm[20]
    inst_extend <<= 1;
    inst_extend |= (imm & 0x00100000) >> 20;
    // set imm[10:1]
    inst_extend <<= 10;
    inst_extend |= (imm & 0x000007FE) >> 1;
    // set imm[11]
    inst_extend <<= 1;
    inst_extend |= (imm & 0x00000800) >> 11;
    // set imm[19:12]
    inst_extend <<= 8;
    inst_extend |= (imm & 0x000FF000) >> 12;
    // set rd = x0
    inst_extend <<= 5;
    inst_extend |= 0b00000;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b1101111;

    return inst_extend;
}

// C.BEQZ, funct = 110, opcode = 01
uint32_t cbeqz_to_beq(uint32_t inst)
{
    // parse imm[8:0] and sign extend it
    uint16_t imm = ((inst & 0x1000) >> 4) | ((inst & 0x0060) << 1) |
                   ((inst & 0x0004) << 3) | ((inst & 0x0C00) >> 7) |
                   ((inst & 0x0018) >> 2);
    uint16_t sign = (imm & 0x0100) >> 8;
    for (int i = 9; i <= 12; ++i)
        imm |= sign << i;
    const uint8_t rs1 = 0x08 | ((inst & 0x0380) >> 7);

#ifdef DEBUG_OUTPUT
    printf("beqz\t%s,%x\n", reg[rs1], imm);
#endif

    // init instruction and set imm[12]
    uint32_t inst_extend = imm >> 12;
    // set imm[10:5]
    inst_extend <<= 6;
    inst_extend |= (imm & 0x07E0) >> 5;
    // set rs2 = x0
    inst_extend <<= 5;
    inst_extend |= 0b00000;
    // set rs1
    inst_extend <<= 5;
    inst_extend |= rs1;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b000;
    // set imm[4:1]
    inst_extend <<= 4;
    inst_extend |= (imm & 0x001E) >> 1;
    // set imm[11]
    inst_extend <<= 1;
    inst_extend |= (imm & 0x0800) >> 11;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b1100011;

    return inst_extend;
}

// C.BENZ, funct = 111, opcode = 01
uint32_t cbenz_to_bne(uint32_t inst)
{
    // parse imm[8:0] and sign extend it
    uint16_t imm = ((inst & 0x1000) >> 4) | ((inst & 0x0060) << 1) |
                   ((inst & 0x0004) << 3) | ((inst & 0x0C00) >> 7) |
                   ((inst & 0x0018) >> 2);
    uint16_t sign = (imm & 0x0100) >> 8;
    for (int i = 9; i <= 12; ++i)
        imm |= sign << i;
    const uint8_t rs1 = 0x08 | ((inst & 0x0380) >> 7);

#ifdef DEBUG_OUTPUT
    printf("bnez\t%s,%x\n", reg[rs1], imm);
#endif

    // init instruction and set imm[12]
    uint32_t inst_extend = imm >> 12;
    // set imm[10:5]
    inst_extend <<= 6;
    inst_extend |= (imm & 0x07E0) >> 5;
    // set rs2 = x0
    inst_extend <<= 5;
    inst_extend |= 0b00000;
    // set rs1
    inst_extend <<= 5;
    inst_extend |= rs1;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b001;
    // set imm[4:1]
    inst_extend <<= 4;
    inst_extend |= (imm & 0x001E) >> 1;
    // set imm[11]
    inst_extend <<= 1;
    inst_extend |= (imm & 0x0800) >> 11;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b1100011;

    return inst_extend;
}

// C.SLLI, funct = 000, opcode = 10
uint32_t cslli_to_slli(uint32_t inst)
{
    // parse shamt
    const uint8_t shamt = ((inst & 0x1000) >> 7) | ((inst & 0x007C) >> 2);
    // ensure shamt[5] == 0
    assert((shamt & 0x20) == 0);
    // ensure shamt != 0
    assert(shamt != 0);
    // parse rd
    const uint8_t rd = 0x08 | ((inst & 0x0380) >> 7);
    // if rd == 0, marked as HINT, implement as nop
    if (rd == 0) {
        return cnop();
    }

#ifdef DEBUG_OUTPUT
    printf("slli\t%s,%s,0x%x\n", reg[rd], reg[rd], shamt);
#endif

    // init instruction and set shamt
    uint32_t inst_extend = shamt;
    // set rs1 = rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b001;
    // set rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b0010011;

    return inst_extend;
}

// C.LWSP, funct = 010, opcode = 10
uint32_t clwsp_to_lw(uint32_t inst)
{
    // parse offset[7:0]
    const uint16_t offset = ((inst & 0x000C) << 4) | ((inst & 0x1000) >> 7) |
                            ((inst & 0x0070) >> 2);
    // parse rd
    const uint8_t rd = (inst & 0x0F80) >> 7;
    // ensure rd != 0
    assert(rd != 0);

#ifdef DEBUG_OUTPUT
    printf("lw\t%s,%u(sp)\n", reg[rd], offset);
#endif

    // init instruction and set offset
    uint32_t inst_extend = 0 | offset;
    // set rs1 = x2
    inst_extend <<= 5;
    inst_extend |= 0b00010;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b010;
    // set rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b0000011;

    return inst_extend;
}

uint32_t cjr_to_jalr(uint32_t inst)
{
    // parse rs1
    const uint8_t rs1 = ((inst & 0x0F80) >> 7);
    // ensure rs1 != 0
    assert(rs1 != 0);

#ifdef DEBUG_OUTPUT
    printf("jr\t%s\n", reg[rs1]);
#endif

    // init instruction and set imm
    uint32_t inst_extend = 0;
    // set rs1
    inst_extend <<= 5;
    inst_extend |= rs1;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b000;
    // set rd = x0
    inst_extend <<= 5;
    inst_extend |= 0b00000;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b1100111;

    return inst_extend;
}

uint32_t cmv_to_add(uint32_t inst)
{
    // parse rd
    const uint8_t rd = ((inst & 0x0F80) >> 7);
    // if rd == 0, marked as HINT, implement as nop
    if (rd == 0) {
        return cnop();
    }
    // parse rs2
    const uint8_t rs2 = ((inst & 0x007C) >> 2);

#ifdef DEBUG_OUTPUT
    printf("mv\t%s,%s\n", reg[rd], reg[rs2]);
#endif

    // init instruction and set funct7
    uint32_t inst_extend = 0;
    // set rs2
    inst_extend <<= 5;
    inst_extend |= rs2;
    // set rs1 = x0
    inst_extend <<= 5;
    inst_extend |= 0b00000;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b000;
    // set rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b0110011;

    return inst_extend;
}

uint32_t cebreak()
{
#ifdef DEBUG_OUTPUT
    printf("ebreak\n");
#endif
    // return ebreak
    return 0x00100073;
}

uint32_t cjalr_to_jalr(uint32_t inst)
{
    // parse rs1
    const uint8_t rs1 = ((inst & 0x0F80) >> 7);
    // ensure rs1 != 0
    assert(rs1 != 0);

#ifdef DEBUG_OUTPUT
    printf("jalr\t%s\n", reg[rs1]);
#endif

    // init instruction and set imm
    uint32_t inst_extend = 0;
    // set rs1
    inst_extend <<= 5;
    inst_extend |= rs1;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b000;
    // set rd = x1
    inst_extend <<= 5;
    inst_extend |= 0b00001;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b1100111;

    return inst_extend;
}

uint32_t cadd_to_add(uint32_t inst)
{
    // parse rs1
    const uint8_t rd = ((inst & 0x0F80) >> 7);
    // if rd == 0, marked as HINT, implement as nop
    if (rd == 0) {
        return cnop();
    }
    // parse rs2
    const uint8_t rs2 = ((inst & 0x007C) >> 2);

#ifdef DEBUG_OUTPUT
    printf("add\t%s,%s,%s\n", reg[rd], reg[rd], reg[rs2]);
#endif

    // init instruction and set funct7
    uint32_t inst_extend = 0;
    // set rs2
    inst_extend <<= 5;
    inst_extend |= rs2;
    // set rs1 = rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b000;
    // set rd
    inst_extend <<= 5;
    inst_extend |= rd;
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b0110011;

    return inst_extend;
}

// CR format, funct = 100, opcode = 10
uint32_t parse_cr(uint32_t inst)
{
    const uint8_t funct4 = (inst & 0xF000) >> 12;
    const uint8_t rs1 = (inst & 0x0F80) >> 7;
    const uint8_t rs2 = (inst & 0x007C) >> 2;

    if (funct4 == 0b1000) {
        if (rs2 == 0)
            return cjr_to_jalr(inst);
        else
            return cmv_to_add(inst);
    } else if (funct4 == 0b1001) {
        if (rs1 == 0 && rs2 == 0)
            return cebreak();
        else if (rs2 == 0)
            return cjalr_to_jalr(inst);
        else
            return cadd_to_add(inst);
    } else
        return cnop();
}

// C.SWSP, funct = 110, opcode = 10
uint32_t cswsp_to_sw(uint32_t inst)
{
    // parse offset[7:0]
    const uint16_t offset = ((inst & 0x0180) >> 1) | ((inst & 0x1E00) >> 7);
    // parse rs2
    const uint8_t rs2 = (inst & 0x007C) >> 2;

#ifdef DEBUG_OUTPUT
    printf("sw\t%s,%u(sp)\n", reg[rs2], offset);
#endif

    // init instruction and set offset[11:5]
    uint32_t inst_extend = offset >> 5;
    // set rs2
    inst_extend <<= 5;
    inst_extend |= rs2;
    // set rs1 = x2
    inst_extend <<= 5;
    inst_extend |= 0b00010;
    // set funct3
    inst_extend <<= 3;
    inst_extend |= 0b010;
    // set imm[4:0]
    inst_extend <<= 5;
    inst_extend |= (offset & 0x001F);
    // set opcode
    inst_extend <<= 7;
    inst_extend |= 0b0100011;

    return inst_extend;
}
