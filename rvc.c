#include "rvc.h"
#include <assert.h>

uint32_t addi4spn_to_addi(uint32_t inst)
{
    // parse nzuimm
    const uint16_t nzuimm = ((inst & 0x0780) >> 1) | ((inst & 0x1800) >> 7) |
                            ((inst & 0x0020) >> 2) | ((inst & 0x0040) >> 4);
    // parse rd
    const uint8_t rd = 0x08 | ((inst & 0x001C) >> 2);

    // Ensure the nzuimm != 0
    assert(nzuimm != 0);

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

uint32_t clw_to_lw(uint32_t inst)
{
    // parse imm
    const uint16_t imm = ((inst & 0x0020) << 1) | ((inst & 0x1C00) >> 7) |
                         ((inst & 0x0040) >> 4);
    // parse rs1
    const uint8_t rs1 = 0x08 | ((inst & 0x0380) >> 7);
    // parse rd
    const uint8_t rd = 0x08 | ((inst & 0x001C) >> 2);

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

uint32_t csw_to_sw(uint32_t inst)
{
    // parse imm
    const uint16_t imm = ((inst & 0x0020) << 1) | ((inst & 0x1C00) >> 7) |
                         ((inst & 0x0040) >> 4);
    // parse rs1
    const uint8_t rs1 = 0x08 | ((inst & 0x0380) >> 7);
    // parse rs2
    const uint8_t rs2 = 0x08 | ((inst & 0x001C) >> 2);

    // init instruction and set imm[11:5]
    uint32_t inst_extend = 0 | (imm & 0xFFE0);
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