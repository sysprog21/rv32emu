/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    /* RV32I Base Instruction Set */
    rv_insn_lui,
    rv_insn_auipc,
    rv_insn_jal,
    rv_insn_jalr,
    rv_insn_beq,
    rv_insn_bne,
    rv_insn_blt,
    rv_insn_bge,
    rv_insn_bltu,
    rv_insn_bgeu,
    rv_insn_lb,
    rv_insn_lh,
    rv_insn_lw,
    rv_insn_lbu,
    rv_insn_lhu,
    rv_insn_sb,
    rv_insn_sh,
    rv_insn_sw,
    rv_insn_addi,
    rv_insn_slti,
    rv_insn_sltiu,
    rv_insn_xori,
    rv_insn_ori,
    rv_insn_andi,
    rv_insn_slli,
    rv_insn_srli,
    rv_insn_srai,
    rv_insn_add,
    rv_insn_sub,
    rv_insn_sll,
    rv_insn_slt,
    rv_insn_sltu,
    rv_insn_xor,
    rv_insn_srl,
    rv_insn_sra,
    rv_insn_or,
    rv_insn_and,
    rv_insn_ecall,
    rv_insn_ebreak,

    /* RISC-V Privileged Instruction */
    rv_insn_wfi,
    rv_insn_uret,
    rv_insn_sret,
    rv_insn_hret,
    rv_insn_mret,

#if RV32_HAS(Zifencei)
    /* RV32 Zifencei Standard Extension */
    rv_insn_fencei,
#endif /* RV32_HAS(Zifencei) */

#if RV32_HAS(Zicsr)
    /* RV32 Zicsr Standard Extension */
    rv_insn_csrrw,
    rv_insn_csrrs,
    rv_insn_csrrc,
    rv_insn_csrrwi,
    rv_insn_csrrsi,
    rv_insn_csrrci,
#endif /* RV32_HAS(Zicsr) */

#if RV32_HAS(EXT_M)
    /* RV32M Standard Extension */
    rv_insn_mul,
    rv_insn_mulh,
    rv_insn_mulhsu,
    rv_insn_mulhu,
    rv_insn_div,
    rv_insn_divu,
    rv_insn_rem,
    rv_insn_remu,
#endif /* RV32_HAS(EXT_M) */

#if RV32_HAS(EXT_A)
    /* RV32A Standard Extension */
    rv_insn_lrw,
    rv_insn_scw,
    rv_insn_amoswapw,
    rv_insn_amoaddw,
    rv_insn_amoxorw,
    rv_insn_amoandw,
    rv_insn_amoorw,
    rv_insn_amominw,
    rv_insn_amomaxw,
    rv_insn_amominuw,
    rv_insn_amomaxuw,
#endif

#if RV32_HAS(EXT_F)
    /* RV32F Standard Extension */
    rv_insn_flw,
    rv_insn_fsw,
    rv_insn_fmadds,
    rv_insn_fmsubs,
    rv_insn_fnmsubs,
    rv_insn_fnmadds,
    rv_insn_fadds,
    rv_insn_fsubs,
    rv_insn_fmuls,
    rv_insn_fdivs,
    rv_insn_fsqrts,
    rv_insn_fsgnjs,
    rv_insn_fsgnjns,
    rv_insn_fsgnjxs,
    rv_insn_fmins,
    rv_insn_fmaxs,
    rv_insn_fcvtws,
    rv_insn_fcvtwus,
    rv_insn_fmvxw,
    rv_insn_feqs,
    rv_insn_flts,
    rv_insn_fles,
    rv_insn_fclasss,
    rv_insn_fcvtsw,
    rv_insn_fcvtswu,
    rv_insn_fmvwx,
#endif

#if RV32_HAS(EXT_C)
    /* RV32C Standard Extension */
    rv_insn_caddi4spn,
    rv_insn_clw,
    rv_insn_csw,
    rv_insn_cnop,
    rv_insn_caddi,
    rv_insn_cjal,
    rv_insn_cli,
    rv_insn_caddi16sp,
    rv_insn_clui,
    rv_insn_csrli,
    rv_insn_csrai,
    rv_insn_candi,
    rv_insn_csub,
    rv_insn_cxor,
    rv_insn_cor,
    rv_insn_cand,
    rv_insn_cj,
    rv_insn_cbeqz,
    rv_insn_cbnez,
    rv_insn_cslli,
    rv_insn_clwsp,
    rv_insn_cjr,
    rv_insn_cmv,
    rv_insn_cebreak,
    rv_insn_cjalr,
    rv_insn_cadd,
    rv_insn_cswsp,
#endif
};

/* clang-format off */
/* instruction decode masks */
enum {
    //               ....xxxx....xxxx....xxxx....xxxx
    INSN_6_2     = 0b00000000000000000000000001111100,
    //               ....xxxx....xxxx....xxxx....xxxx
    FR_OPCODE    = 0b00000000000000000000000001111111, // R-type
    FR_RD        = 0b00000000000000000000111110000000,
    FR_FUNCT3    = 0b00000000000000000111000000000000,
    FR_RS1       = 0b00000000000011111000000000000000,
    FR_RS2       = 0b00000001111100000000000000000000,
    FR_FUNCT7    = 0b11111110000000000000000000000000,
    //               ....xxxx....xxxx....xxxx....xxxx
    FI_IMM_11_0  = 0b11111111111100000000000000000000, // I-type
    //               ....xxxx....xxxx....xxxx....xxxx
    FS_IMM_4_0   = 0b00000000000000000000111110000000, // S-type
    FS_IMM_11_5  = 0b11111110000000000000000000000000,
    //               ....xxxx....xxxx....xxxx....xxxx
    FB_IMM_11    = 0b00000000000000000000000010000000, // B-type
    FB_IMM_4_1   = 0b00000000000000000000111100000000,
    FB_IMM_10_5  = 0b01111110000000000000000000000000,
    FB_IMM_12    = 0b10000000000000000000000000000000,
    //               ....xxxx....xxxx....xxxx....xxxx
    FU_IMM_31_12 = 0b11111111111111111111000000000000, // U-type
    //               ....xxxx....xxxx....xxxx....xxxx
    FJ_IMM_19_12 = 0b00000000000011111111000000000000, // J-type
    FJ_IMM_11    = 0b00000000000100000000000000000000,
    FJ_IMM_10_1  = 0b01111111111000000000000000000000,
    FJ_IMM_20    = 0b10000000000000000000000000000000,
    //               ....xxxx....xxxx....xxxx....xxxx
    FR4_FMT      = 0b00000110000000000000000000000000, // R4-type
    FR4_RS3      = 0b11111000000000000000000000000000,
    //               ....xxxx....xxxx....xxxx....xxxx
    FC_OPCODE    = 0b00000000000000000000000000000011, // compressed-instuction
    FC_FUNC3     = 0b00000000000000001110000000000000,
    //               ....xxxx....xxxx....xxxx....xxxx
    FC_RS1C      = 0b00000000000000000000001110000000,
    FC_RS2C      = 0b00000000000000000000000000011100,
    FC_RS1       = 0b00000000000000000000111110000000,
    FC_RS2       = 0b00000000000000000000000001111100,
    //               ....xxxx....xxxx....xxxx....xxxx
    FC_RDC       = 0b00000000000000000000000000011100,
    FC_RD        = 0b00000000000000000000111110000000,
    //               ....xxxx....xxxx....xxxx....xxxx
    FC_IMM_12_10 = 0b00000000000000000001110000000000, // CL,CS,CB
    FC_IMM_6_5   = 0b00000000000000000000000001100000,
    //               ....xxxx....xxxx....xxxx....xxxx
    FCI_IMM_12   = 0b00000000000000000001000000000000, 
    FCI_IMM_6_2  = 0b00000000000000000000000001111100,
    //               ....xxxx....xxxx....xxxx....xxxx
    FCSS_IMM     = 0b00000000000000000001111110000000,
    //               ....xxxx....xxxx....xxxx....xxxx
    FCJ_IMM      = 0b00000000000000000001111111111100,
    //               ....xxxx....xxxx....xxxx....xxxx
};
/* clang-format on */

enum {
    INSN_16 = 2,
    INSN_32 = 4,
};

typedef struct {
    union {
        int32_t imm;
        uint8_t rs3;
    };
    uint8_t rd, rs1, rs2;
    /* store IR list */
    uint8_t opcode;

#if RV32_HAS(EXT_C)
    uint8_t shamt;
#endif

    /* instruction length */
    uint8_t insn_len;
} rv_insn_t;

/* translated basic block */
typedef struct block {
    /* number of instructions encompased */
    uint32_t n_insn;
    /* address range of the basic block */
    uint32_t pc_start, pc_end;
    /* maximum of instructions encompased */
    uint32_t insn_capacity;
    /* block predictoin */
    struct block *predict;
    /* memory blocks */
    rv_insn_t *ir;
} block_t;

struct block_map {
    /* max number of entries in the block map */
    uint32_t block_capacity;
    /* number of entries currently in the map */
    uint32_t size;
    /* block map */
    struct block **map;
};

/* clear all block in the block map */
void block_map_clear(struct block_map *map);

/* decode the RISC-V instruction */
bool rv_decode(rv_insn_t *ir, const uint32_t insn);
