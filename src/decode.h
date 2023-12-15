/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "riscv.h"

enum op_field {
    F_none = 0,
    F_rs1 = 1,
    F_rs2 = 2,
    F_rs3 = 4,
    F_rd = 8,
};

#define ENC4(a, b, c, d, ...) F_##a | F_##b | F_##c | F_##d
#define ENC3(a, b, c, ...) F_##a | F_##b | F_##c
#define ENC2(a, b, ...) F_##a | F_##b
#define ENC1(a, ...) F_##a
#define ENC0(...) F_none

#define ENCN(X, A) X##A
#define ENC_GEN(X, A) ENCN(X, A)
#define ENC(...) ENC_GEN(ENC, COUNT_VARARGS(__VA_ARGS__))(__VA_ARGS__)

/* RISC-V instruction list in format _(instruction-name, can-branch, insn_len,
 * reg-mask)
 */
/* clang-format off */
#define RV_INSN_LIST                                \
    _(nop, 0, 4, ENC(rs1, rd))                      \
    /* RV32I Base Instruction Set */                \
    _(lui, 0, 4, ENC(rd))                           \
    _(auipc, 0, 4, ENC(rd))                         \
    _(jal, 1, 4, ENC(rd))                           \
    _(jalr, 1, 4, ENC(rs1, rd))                     \
    _(beq, 1, 4, ENC(rs1, rs2))                     \
    _(bne, 1, 4, ENC(rs1, rs2))                     \
    _(blt, 1, 4, ENC(rs1, rs2))                     \
    _(bge, 1, 4, ENC(rs1, rs2))                     \
    _(bltu, 1, 4, ENC(rs1, rs2))                    \
    _(bgeu, 1, 4, ENC(rs1, rs2))                    \
    _(lb, 0, 4, ENC(rs1, rd))                       \
    _(lh, 0, 4, ENC(rs1, rd))                       \
    _(lw, 0, 4, ENC(rs1, rd))                       \
    _(lbu, 0, 4, ENC(rs1, rd))                      \
    _(lhu, 0, 4, ENC(rs1, rd))                      \
    _(sb, 0, 4, ENC(rs1, rs2))                      \
    _(sh, 0, 4, ENC(rs1, rs2))                      \
    _(sw, 0, 4, ENC(rs1, rs2))                      \
    _(addi, 0, 4, ENC(rs1, rd))                     \
    _(slti, 0, 4, ENC(rs1, rd))                     \
    _(sltiu, 0, 4, ENC(rs1, rd))                    \
    _(xori, 0, 4, ENC(rs1, rd))                     \
    _(ori, 0, 4, ENC(rs1, rd))                      \
    _(andi, 0, 4, ENC(rs1, rd))                     \
    _(slli, 0, 4, ENC(rs1, rd))                     \
    _(srli, 0, 4, ENC(rs1, rd))                     \
    _(srai, 0, 4, ENC(rs1, rd))                     \
    _(add, 0, 4, ENC(rs1, rs2, rd))                 \
    _(sub, 0, 4, ENC(rs1, rs2, rd))                 \
    _(sll, 0, 4, ENC(rs1, rs2, rd))                 \
    _(slt, 0, 4, ENC(rs1, rs2, rd))                 \
    _(sltu, 0, 4, ENC(rs1, rs2, rd))                \
    _(xor, 0, 4, ENC(rs1, rs2, rd))                 \
    _(srl, 0, 4, ENC(rs1, rs2, rd))                 \
    _(sra, 0, 4, ENC(rs1, rs2, rd))                 \
    _(or, 0, 4, ENC(rs1, rs2, rd))                  \
    _(and, 0, 4, ENC(rs1, rs2, rd))                 \
    _(ecall, 1, 4, ENC(rs1, rd))                    \
    _(ebreak, 1, 4, ENC(rs1, rd))                   \
    /* RISC-V Privileged Instruction */             \
    _(wfi, 0, 4, ENC(rs1, rd))                      \
    _(uret, 0, 4, ENC(rs1, rd))                     \
    _(sret, 0, 4, ENC(rs1, rd))                     \
    _(hret, 0, 4, ENC(rs1, rd))                     \
    _(mret, 1, 4, ENC(rs1, rd))                     \
    /* RV32 Zifencei Standard Extension */          \
    IIF(RV32_HAS(Zifencei))(                        \
        _(fencei, 1, 4, ENC(rs1, rd))               \
    )                                               \
    /* RV32 Zicsr Standard Extension */             \
    IIF(RV32_HAS(Zicsr))(                           \
        _(csrrw, 0, 4, ENC(rs1, rd))                \
        _(csrrs, 0, 4, ENC(rs1, rd))                \
        _(csrrc, 0, 4, ENC(rs1, rd))                \
        _(csrrwi, 0, 4, ENC(rs1, rd))               \
        _(csrrsi, 0, 4, ENC(rs1, rd))               \
        _(csrrci, 0, 4, ENC(rs1, rd))               \
    )                                               \
    /* RV32M Standard Extension */                  \
    IIF(RV32_HAS(EXT_M))(                           \
        _(mul, 0, 4, ENC(rs1, rs2, rd))             \
        _(mulh, 0, 4, ENC(rs1, rs2, rd))            \
        _(mulhsu, 0, 4, ENC(rs1, rs2, rd))          \
        _(mulhu, 0, 4, ENC(rs1, rs2, rd))           \
        _(div, 0, 4, ENC(rs1, rs2, rd))             \
        _(divu, 0, 4, ENC(rs1, rs2, rd))            \
        _(rem, 0, 4, ENC(rs1, rs2, rd))             \
        _(remu, 0, 4, ENC(rs1, rs2, rd))            \
    )                                               \
    /* RV32A Standard Extension */                  \
    IIF(RV32_HAS(EXT_A))(                           \
        _(lrw, 0, 4, ENC(rs1, rs2, rd))             \
        _(scw, 0, 4, ENC(rs1, rs2, rd))             \
        _(amoswapw, 0, 4, ENC(rs1, rs2, rd))        \
        _(amoaddw, 0, 4, ENC(rs1, rs2, rd))         \
        _(amoxorw, 0, 4, ENC(rs1, rs2, rd))         \
        _(amoandw, 0, 4, ENC(rs1, rs2, rd))         \
        _(amoorw, 0, 4, ENC(rs1, rs2, rd))          \
        _(amominw, 0, 4, ENC(rs1, rs2, rd))         \
        _(amomaxw, 0, 4, ENC(rs1, rs2, rd))         \
        _(amominuw, 0, 4, ENC(rs1, rs2, rd))        \
        _(amomaxuw, 0, 4, ENC(rs1, rs2, rd))        \
    )                                               \
    /* RV32F Standard Extension */                  \
    IIF(RV32_HAS(EXT_F))(                           \
        _(flw, 0, 4, ENC(rs1, rd))                  \
        _(fsw, 0, 4, ENC(rs1, rs2))                 \
        _(fmadds, 0, 4, ENC(rs1, rs2, rs3, rd))     \
        _(fmsubs, 0, 4, ENC(rs1, rs2, rs3, rd))     \
        _(fnmsubs, 0, 4, ENC(rs1, rs2, rs3, rd))    \
        _(fnmadds, 0, 4, ENC(rs1, rs2, rs3, rd))    \
        _(fadds, 0, 4, ENC(rs1, rs2, rd))           \
        _(fsubs, 0, 4, ENC(rs1, rs2, rd))           \
        _(fmuls, 0, 4, ENC(rs1, rs2, rd))           \
        _(fdivs, 0, 4, ENC(rs1, rs2, rd))           \
        _(fsqrts, 0, 4, ENC(rs1, rs2, rd))          \
        _(fsgnjs, 0, 4, ENC(rs1, rs2, rd))          \
        _(fsgnjns, 0, 4, ENC(rs1, rs2, rd))         \
        _(fsgnjxs, 0, 4, ENC(rs1, rs2, rd))         \
        _(fmins, 0, 4, ENC(rs1, rs2, rd))           \
        _(fmaxs, 0, 4, ENC(rs1, rs2, rd))           \
        _(fcvtws, 0, 4, ENC(rs1, rs2, rd))          \
        _(fcvtwus, 0, 4, ENC(rs1, rs2, rd))         \
        _(fmvxw, 0, 4, ENC(rs1, rs2, rd))           \
        _(feqs, 0, 4, ENC(rs1, rs2, rd))            \
        _(flts, 0, 4, ENC(rs1, rs2, rd))            \
        _(fles, 0, 4, ENC(rs1, rs2, rd))            \
        _(fclasss, 0, 4, ENC(rs1, rs2, rd))         \
        _(fcvtsw, 0, 4, ENC(rs1, rs2, rd))          \
        _(fcvtswu, 0, 4, ENC(rs1, rs2, rd))         \
        _(fmvwx, 0, 4, ENC(rs1, rs2, rd))           \
    )                                               \
    /* RV32C Standard Extension */                  \
    IIF(RV32_HAS(EXT_C))(                           \
        _(caddi4spn, 0, 2, ENC(rd))                 \
        _(clw, 0, 2, ENC(rs1, rd))                  \
        _(csw, 0, 2, ENC(rs1, rs2))                 \
        _(cnop, 0, 2, ENC())                        \
        _(caddi, 0, 2, ENC(rd))                     \
        _(cjal, 1, 2, ENC())                        \
        _(cli, 0, 2, ENC(rd))                       \
        _(caddi16sp, 0, 2, ENC())                   \
        _(clui, 0, 2, ENC(rd))                      \
        _(csrli, 0, 2, ENC(rs1))                    \
        _(csrai, 0, 2, ENC(rs1))                    \
        _(candi, 0, 2, ENC(rs1))                    \
        _(csub, 0, 2, ENC(rs1, rs2, rd))            \
        _(cxor, 0, 2, ENC(rs1, rs2, rd))            \
        _(cor, 0, 2, ENC(rs1, rs2, rd))             \
        _(cand, 0, 2, ENC(rs1, rs2, rd))            \
        _(cj, 1, 2, ENC())                          \
        _(cbeqz, 1, 2, ENC(rs1))                    \
        _(cbnez, 1, 2, ENC(rs1))                    \
        _(cslli, 0, 2, ENC(rd))                     \
        _(clwsp, 0, 2, ENC(rd))                     \
        _(cjr, 1, 2, ENC(rs1, rs2, rd))             \
        _(cmv, 0, 2, ENC(rs1, rs2, rd))             \
        _(cebreak, 1, 2, ENC(rs1, rs2, rd))         \
        _(cjalr, 1, 2, ENC(rs1, rs2, rd))           \
        _(cadd, 0, 2, ENC(rs1, rs2, rd))            \
        _(cswsp, 0, 2, ENC(rs2))                    \
    )
/* clang-format on */

/* Macro operation fusion */

/* macro operation fusion: convert specific RISC-V instruction patterns
 * into faster and equivalent code
 */
#define FUSE_INSN_LIST \
    _(fuse1)           \
    _(fuse2)           \
    _(fuse3)           \
    _(fuse4)           \
    _(fuse5)           \
    _(fuse6)           \
    _(fuse7)

/* clang-format off */
/* IR list */
enum {
#define _(inst, can_branch, insn_len, reg_mask) rv_insn_##inst,
    RV_INSN_LIST
#undef _
    N_RV_INSNS,
#define _(inst) rv_insn_##inst,
    FUSE_INSN_LIST
#undef _
    N_TOTAL_INSNS,
};
/* clang-format on */

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

typedef struct {
    int32_t imm;
    uint8_t rd, rs1, rs2;
    uint8_t opcode;
} opcode_fuse_t;

#define HISTORY_SIZE 16
typedef struct {
    uint8_t idx;
    uint32_t PC[HISTORY_SIZE];
    struct rv_insn *target[HISTORY_SIZE];
} branch_history_table_t;

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

    uint32_t pc;

    /* Tail-call optimization (TCO) allows a C function to replace a function
     * call to another function or itself, followed by a simple return of the
     * function's result, with a direct jump to the target function. This
     * optimization enables the self-recursive function to reuse the same
     * function stack frame.
     *
     * The @next member indicates the next intermediate representation
     * (IR) or is NULL if it is the final instruction in a basic block. The
     * @impl member facilitates the direct invocation of the next instruction
     * emulation without the need to compute the jump address. By utilizing
     * these two members, all instruction emulations can be rewritten into a
     * self-recursive version, enabling the compiler to leverage TCO.
     */
    struct rv_insn *next;
    bool (*impl)(riscv_t *, const struct rv_insn *, uint64_t, uint32_t);

    /* Two pointers, 'branch_taken' and 'branch_untaken', are employed to
     * avoid the overhead associated with aggressive memory copying. Instead
     * of copying the entire intermediate representation (IR) array, these
     * pointers indicate the first IR of the first basic block in the path of
     * the taken and untaken branches. This allows for direct jumping to the
     * specific IR array without the need for additional copying.
     */
    struct rv_insn *branch_taken, *branch_untaken;
    branch_history_table_t *branch_table;
} rv_insn_t;

/* decode the RISC-V instruction */
bool rv_decode(rv_insn_t *ir, const uint32_t insn);
