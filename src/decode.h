/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "riscv.h"

/* RISC-V instruction list in format _(instruction-name, can-branch) */
/* clang-format off */
#define RISCV_INSN_LIST                    \
    _(nop, 0)                              \
    /* RV32I Base Instruction Set */       \
    _(lui, 0)                              \
    _(auipc, 0)                            \
    _(jal, 1)                              \
    _(jalr, 1)                             \
    _(beq, 1)                              \
    _(bne, 1)                              \
    _(blt, 1)                              \
    _(bge, 1)                              \
    _(bltu, 1)                             \
    _(bgeu, 1)                             \
    _(lb, 0)                               \
    _(lh, 0)                               \
    _(lw, 0)                               \
    _(lbu, 0)                              \
    _(lhu, 0)                              \
    _(sb, 0)                               \
    _(sh, 0)                               \
    _(sw, 0)                               \
    _(addi, 0)                             \
    _(slti, 0)                             \
    _(sltiu, 0)                            \
    _(xori, 0)                             \
    _(ori, 0)                              \
    _(andi, 0)                             \
    _(slli, 0)                             \
    _(srli, 0)                             \
    _(srai, 0)                             \
    _(add, 0)                              \
    _(sub, 0)                              \
    _(sll, 0)                              \
    _(slt, 0)                              \
    _(sltu, 0)                             \
    _(xor, 0)                              \
    _(srl, 0)                              \
    _(sra, 0)                              \
    _(or, 0)                               \
    _(and, 0)                              \
    _(ecall, 1)                            \
    _(ebreak, 1)                           \
    /* RISC-V Privileged Instruction */    \
    _(wfi, 0)                              \
    _(uret, 0)                             \
    _(sret, 0)                             \
    _(hret, 0)                             \
    _(mret, 1)                             \
    /* RV32 Zifencei Standard Extension */ \
    IIF(RV32_HAS(Zifencei))(               \
        _(fencei, 1)                       \
    )                                      \
    /* RV32 Zicsr Standard Extension */    \
    IIF(RV32_HAS(Zicsr))(                  \
        _(csrrw, 0)                        \
        _(csrrs, 0)                        \
        _(csrrc, 0)                        \
        _(csrrwi, 0)                       \
        _(csrrsi, 0)                       \
        _(csrrci, 0)                       \
    )                                      \
    /* RV32M Standard Extension */         \
    IIF(RV32_HAS(EXT_M))(                  \
        _(mul, 0)                          \
        _(mulh, 0)                         \
        _(mulhsu, 0)                       \
        _(mulhu, 0)                        \
        _(div, 0)                          \
        _(divu, 0)                         \
        _(rem, 0)                          \
        _(remu, 0)                         \
    )                                      \
    /* RV32A Standard Extension */         \
    IIF(RV32_HAS(EXT_A))(                  \
        _(lrw, 0)                          \
        _(scw, 0)                          \
        _(amoswapw, 0)                     \
        _(amoaddw, 0)                      \
        _(amoxorw, 0)                      \
        _(amoandw, 0)                      \
        _(amoorw, 0)                       \
        _(amominw, 0)                      \
        _(amomaxw, 0)                      \
        _(amominuw, 0)                     \
        _(amomaxuw, 0)                     \
    )                                      \
    /* RV32F Standard Extension */         \
    IIF(RV32_HAS(EXT_F))(                  \
        _(flw, 0)                          \
        _(fsw, 0)                          \
        _(fmadds, 0)                       \
        _(fmsubs, 0)                       \
        _(fnmsubs, 0)                      \
        _(fnmadds, 0)                      \
        _(fadds, 0)                        \
        _(fsubs, 0)                        \
        _(fmuls, 0)                        \
        _(fdivs, 0)                        \
        _(fsqrts, 0)                       \
        _(fsgnjs, 0)                       \
        _(fsgnjns, 0)                      \
        _(fsgnjxs, 0)                      \
        _(fmins, 0)                        \
        _(fmaxs, 0)                        \
        _(fcvtws, 0)                       \
        _(fcvtwus, 0)                      \
        _(fmvxw, 0)                        \
        _(feqs, 0)                         \
        _(flts, 0)                         \
        _(fles, 0)                         \
        _(fclasss, 0)                      \
        _(fcvtsw, 0)                       \
        _(fcvtswu, 0)                      \
        _(fmvwx, 0)                        \
    )                                      \
    /* RV32C Standard Extension */         \
    IIF(RV32_HAS(EXT_C))(                  \
        _(caddi4spn, 0)                    \
        _(clw, 0)                          \
        _(csw, 0)                          \
        _(cnop, 0)                         \
        _(caddi, 0)                        \
        _(cjal, 1)                         \
        _(cli, 0)                          \
        _(caddi16sp, 0)                    \
        _(clui, 0)                         \
        _(csrli, 0)                        \
        _(csrai, 0)                        \
        _(candi, 0)                        \
        _(csub, 0)                         \
        _(cxor, 0)                         \
        _(cor, 0)                          \
        _(cand, 0)                         \
        _(cj, 1)                           \
        _(cbeqz, 1)                        \
        _(cbnez, 1)                        \
        _(cslli, 0)                        \
        _(clwsp, 0)                        \
        _(cjr, 1)                          \
        _(cmv, 0)                          \
        _(cebreak, 1)                      \
        _(cjalr, 1)                        \
        _(cadd, 0)                         \
        _(cswsp, 0)                        \
    )                                      \
    /* macro operation fusion: convert specific RISC-V instruction patterns 
     * into faster and equivalent code 
     */                                    \
    _(fuse1, 0)                            \
    _(fuse2, 0)                            \
    _(fuse3, 0)                            \
    _(fuse4, 0)                            \
    _(fuse5, 0)
/* clang-format on */

/* IR list */
enum {
#define _(inst, can_branch) rv_insn_##inst,
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
    int32_t imm;
    uint8_t rd, rs1, rs2;
} opcode_fuse_t;

typedef struct rv_insn {
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
    /* fuse operation */
    int32_t imm2;
    opcode_fuse_t *fuse;

    /* instruction length */
    uint8_t insn_len;

    /* Tail-call optimization (TCO) allows a C function to replace a function
     * call to another function or itself, followed by a simple return of the
     * function's result, with a direct jump to the target function. This
     * optimization enables the self-recursive function to reuse the same
     * function stack frame.
     *
     * The @tailcall member indicates whether an intermediate representation
     * (IR) is the final instruction in a basic block. The @impl member
     * facilitates the direct invocation of the next instruction emulation
     * without the need to compute the jump address. By utilizing these two
     * members, all instruction emulations can be rewritten into a
     * self-recursive version, enabling the compiler to leverage TCO.
     */
    bool tailcall;
    bool (*impl)(riscv_t *, const struct rv_insn *);

    /* Two pointers, 'branch_taken' and 'branch_untaken', are employed to
     * avoid the overhead associated with aggressive memory copying. Instead
     * of copying the entire intermediate representation (IR) array, these
     * pointers indicate the first IR of the first basic block in the path of
     * the taken and untaken branches. This allows for direct jumping to the
     * specific IR array without the need for additional copying.
     */
    struct rv_insn *branch_taken, *branch_untaken;
} rv_insn_t;

/* decode the RISC-V instruction */
bool rv_decode(rv_insn_t *ir, const uint32_t insn);
