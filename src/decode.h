/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* RISC-V instruction list */
/* clang-format off */
#define RISCV_INSN_LIST                    \
    _(nop)                                 \
    /* RV32I Base Instruction Set */       \
    _(lui)                                 \
    _(auipc)                               \
    _(jal)                                 \
    _(jalr)                                \
    _(beq)                                 \
    _(bne)                                 \
    _(blt)                                 \
    _(bge)                                 \
    _(bltu)                                \
    _(bgeu)                                \
    _(lb)                                  \
    _(lh)                                  \
    _(lw)                                  \
    _(lbu)                                 \
    _(lhu)                                 \
    _(sb)                                  \
    _(sh)                                  \
    _(sw)                                  \
    _(addi)                                \
    _(slti)                                \
    _(sltiu)                               \
    _(xori)                                \
    _(ori)                                 \
    _(andi)                                \
    _(slli)                                \
    _(srli)                                \
    _(srai)                                \
    _(add)                                 \
    _(sub)                                 \
    _(sll)                                 \
    _(slt)                                 \
    _(sltu)                                \
    _(xor)                                 \
    _(srl)                                 \
    _(sra)                                 \
    _(or)                                  \
    _(and)                                 \
    _(ecall)                               \
    _(ebreak)                              \
    /* RISC-V Privileged Instruction */    \
    _(wfi)                                 \
    _(uret)                                \
    _(sret)                                \
    _(hret)                                \
    _(mret)                                \
    /* RV32 Zifencei Standard Extension */ \
    IIF(RV32_HAS(Zifencei))(               \
        _(fencei)                          \
    )                                      \
    /* RV32 Zicsr Standard Extension */    \
    IIF(RV32_HAS(Zicsr))(                  \
        _(csrrw)                           \
        _(csrrs)                           \
        _(csrrc)                           \
        _(csrrwi)                          \
        _(csrrsi)                          \
        _(csrrci)                          \
    )                                      \
    /* RV32M Standard Extension */         \
    IIF(RV32_HAS(EXT_M))(                  \
        _(mul)                             \
        _(mulh)                            \
        _(mulhsu)                          \
        _(mulhu)                           \
        _(div)                             \
        _(divu)                            \
        _(rem)                             \
        _(remu)                            \
    )                                      \
    /* RV32A Standard Extension */         \
    IIF(RV32_HAS(EXT_A))(                  \
        _(lrw)                             \
        _(scw)                             \
        _(amoswapw)                        \
        _(amoaddw)                         \
        _(amoxorw)                         \
        _(amoandw)                         \
        _(amoorw)                          \
        _(amominw)                         \
        _(amomaxw)                         \
        _(amominuw)                        \
        _(amomaxuw)                        \
    )                                      \
    /* RV32F Standard Extension */         \
    IIF(RV32_HAS(EXT_F))(                  \
        _(flw)                             \
        _(fsw)                             \
        _(fmadds)                          \
        _(fmsubs)                          \
        _(fnmsubs)                         \
        _(fnmadds)                         \
        _(fadds)                           \
        _(fsubs)                           \
        _(fmuls)                           \
        _(fdivs)                           \
        _(fsqrts)                          \
        _(fsgnjs)                          \
        _(fsgnjns)                         \
        _(fsgnjxs)                         \
        _(fmins)                           \
        _(fmaxs)                           \
        _(fcvtws)                          \
        _(fcvtwus)                         \
        _(fmvxw)                           \
        _(feqs)                            \
        _(flts)                            \
        _(fles)                            \
        _(fclasss)                         \
        _(fcvtsw)                          \
        _(fcvtswu)                         \
        _(fmvwx)                           \
    )                                      \
    /* RV32C Standard Extension */         \
    IIF(RV32_HAS(EXT_C))(                  \
        _(caddi4spn)                       \
        _(clw)                             \
        _(csw)                             \
        _(cnop)                            \
        _(caddi)                           \
        _(cjal)                            \
        _(cli)                             \
        _(caddi16sp)                       \
        _(clui)                            \
        _(csrli)                           \
        _(csrai)                           \
        _(candi)                           \
        _(csub)                            \
        _(cxor)                            \
        _(cor)                             \
        _(cand)                            \
        _(cj)                              \
        _(cbeqz)                           \
        _(cbnez)                           \
        _(cslli)                           \
        _(clwsp)                           \
        _(cjr)                             \
        _(cmv)                             \
        _(cebreak)                         \
        _(cjalr)                           \
        _(cadd)                            \
        _(cswsp)                           \
    )
/* clang-format on */

/* IR list */
enum {
#define _(inst) rv_insn_##inst,
    RISCV_INSN_LIST
#undef _
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
    uint32_t n_insn;           /**< number of instructions encompased */
    uint32_t pc_start, pc_end; /**< address range of the basic block */
    /* maximum of instructions encompased */
    uint32_t insn_capacity; /**< maximum of instructions encompased */
    struct block *predict;  /**< block prediction */
    rv_insn_t *ir;          /**< IR as memory blocks */
} block_t;

typedef struct {
    uint32_t block_capacity; /**< max number of entries in the block map */
    uint32_t size;           /**< number of entries currently in the map */
    block_t **map;           /**< block map */
} block_map_t;

/* clear all block in the block map */
void block_map_clear(block_map_t *map);

/* decode the RISC-V instruction */
bool rv_decode(rv_insn_t *ir, const uint32_t insn);
