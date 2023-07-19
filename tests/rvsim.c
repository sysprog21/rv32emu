/*
 * Copyright (c) 2021 Paul Kennedy
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Source: https://github.com/pmkenned/rv_sim
 */

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define NELEM(X) sizeof(X) / sizeof(X[0])

#define MASK_LSB_W(X, LSB, W) (X >> LSB) & ((1 << W) - 1)
#define MASK_MSB_LSB(X, MSB, LSB) (X >> LSB) & ((1 << (MSB - LSB + 1)) - 1)

enum { MAX_STEPS = 100 };  // TODO: make this a command-line argument

#define EXT_M 0
#define EXT_F 0
#define EXT_D 0
#define EXT_C 1

#define OPCODE_LIST_RV32I                                     \
    X(0x0000007f, 0x00000037, FMT_U, MNEM_LUI, "lui")         \
    X(0x0000007f, 0x00000017, FMT_U, MNEM_AUIPC, "auipc")     \
    X(0x0000007f, 0x0000006f, FMT_J, MNEM_JAL, "jal")         \
    X(0x0000707f, 0x00000067, FMT_I, MNEM_JALR, "jalr")       \
    X(0x0000707f, 0x00000063, FMT_B, MNEM_BEQ, "beq")         \
    X(0x0000707f, 0x00001063, FMT_B, MNEM_BNE, "bne")         \
    X(0x0000707f, 0x00004063, FMT_B, MNEM_BLT, "blt")         \
    X(0x0000707f, 0x00005063, FMT_B, MNEM_BGE, "bge")         \
    X(0x0000707f, 0x00006063, FMT_B, MNEM_BLTU, "bltu")       \
    X(0x0000707f, 0x00007063, FMT_B, MNEM_BGEU, "bgeu")       \
    X(0x0000707f, 0x00000003, FMT_I, MNEM_LB, "lb")           \
    X(0x0000707f, 0x00001003, FMT_I, MNEM_LH, "lh")           \
    X(0x0000707f, 0x00002003, FMT_I, MNEM_LW, "lw")           \
    X(0x0000707f, 0x00004003, FMT_I, MNEM_LBU, "lbu")         \
    X(0x0000707f, 0x00005003, FMT_I, MNEM_LHU, "lhu")         \
    X(0x0000707f, 0x00000023, FMT_S, MNEM_SB, "sb")           \
    X(0x0000707f, 0x00001023, FMT_S, MNEM_SH, "sh")           \
    X(0x0000707f, 0x00002023, FMT_S, MNEM_SW, "sw")           \
    X(0x0000707f, 0x00000013, FMT_I, MNEM_ADDI, "addi")       \
    X(0x0000707f, 0x00002013, FMT_I, MNEM_SLTI, "slti")       \
    X(0x0000707f, 0x00003013, FMT_I, MNEM_SLTIU, "sltiu")     \
    X(0x0000707f, 0x00004013, FMT_I, MNEM_XORI, "xori")       \
    X(0x0000707f, 0x00006013, FMT_I, MNEM_ORI, "ori")         \
    X(0x0000707f, 0x00007013, FMT_I, MNEM_ANDI, "andi")       \
    X(0xfe00707f, 0x00001013, FMT_I, MNEM_SLLI, "slli")       \
    X(0xfe00707f, 0x00005013, FMT_I, MNEM_SRLI, "srli")       \
    X(0xfe00707f, 0x40005013, FMT_I, MNEM_SRAI, "srai")       \
    X(0xfe00707f, 0x00000033, FMT_R, MNEM_ADD, "add")         \
    X(0xfe00707f, 0x40000033, FMT_R, MNEM_SUB, "sub")         \
    X(0xfe00707f, 0x00001033, FMT_R, MNEM_SLL, "sll")         \
    X(0xfe00707f, 0x00002033, FMT_R, MNEM_SLT, "slt")         \
    X(0xfe00707f, 0x00003033, FMT_R, MNEM_SLTU, "sltu")       \
    X(0xfe00707f, 0x00004033, FMT_R, MNEM_XOR, "xor")         \
    X(0xfe00707f, 0x00005033, FMT_R, MNEM_SRL, "srl")         \
    X(0xfe00707f, 0x40005033, FMT_R, MNEM_SRA, "sra")         \
    X(0xfe00707f, 0x00006033, FMT_R, MNEM_OR, "or")           \
    X(0xfe00707f, 0x00007033, FMT_R, MNEM_AND, "and")         \
    X(0xf00fffff, 0x0000000f, FMT_I, MNEM_FENCE, "fence")     \
    X(0xffffffff, 0x0000100f, FMT_I, MNEM_FENCE_I, "fence.i") \
    X(0xffffffff, 0x00000073, FMT_I, MNEM_ECALL, "ecall")     \
    X(0xffffffff, 0x00100073, FMT_I, MNEM_EBREAK, "ebreak")   \
    X(0x0000707f, 0x00001073, FMT_I, MNEM_CSRRW, "csrrw")     \
    X(0x0000707f, 0x00002073, FMT_I, MNEM_CSRRS, "csrrs")     \
    X(0x0000707f, 0x00003073, FMT_I, MNEM_CSRRC, "csrrc")     \
    X(0x0000707f, 0x00005073, FMT_I, MNEM_CSRRWI, "csrrwi")   \
    X(0x0000707f, 0x00006073, FMT_I, MNEM_CSRRSI, "csrrsi")   \
    X(0x0000707f, 0x00007073, FMT_I, MNEM_CSRRCI, "csrrci")

#if EXT_M
#define OPCODE_LIST_M                                       \
    X(0xfe00707f, 0x02000033, FMT_R, MNEM_MUL, "mul")       \
    X(0xfe00707f, 0x02001033, FMT_R, MNEM_MULH, "mulh")     \
    X(0xfe00707f, 0x02002033, FMT_R, MNEM_MULHSU, "mulhsu") \
    X(0xfe00707f, 0x02003033, FMT_R, MNEM_MULHU, "mulhu")   \
    X(0xfe00707f, 0x02004033, FMT_R, MNEM_DIV, "div")       \
    X(0xfe00707f, 0x02005033, FMT_R, MNEM_DIVU, "divu")     \
    X(0xfe00707f, 0x02006033, FMT_R, MNEM_REM, "rem")       \
    X(0xfe00707f, 0x02007033, FMT_R, MNEM_REMU, "remu")
#else
#define OPCODE_LIST_M
#endif

#if EXT_F
#define OPCODE_LIST_F                                             \
    X(0x0000707f, 0x00002007, FMT_I, MNEM_FLW, "flw")             \
    X(0x0000707f, 0x00002027, FMT_S, MNEM_FSW, "fsw")             \
    X(0x0600007f, 0x00000043, FMT_R4, MNEM_FMADD_S, "fmadd.s")    \
    X(0x0600007f, 0x00000047, FMT_R4, MNEM_FMSUB_S, "fmsub.s")    \
    X(0x0600007f, 0x0000004b, FMT_R4, MNEM_FNMSUB_S, "fnmsub.s")  \
    X(0x0600007f, 0x0000004e, FMT_R4, MNEM_FNMADD_S, "fnmadd.s")  \
    X(0xfe00007f, 0x00000053, FMT_R, MNEM_FADD_S, "fadd.s")       \
    X(0xfe00007f, 0x08000053, FMT_R, MNEM_FSUB_S, "fsub.s")       \
    X(0xfe00007f, 0x10000053, FMT_R, MNEM_FMUL_S, "fmul.s")       \
    X(0xfe00007f, 0x18000053, FMT_R, MNEM_FDIV_S, "fdiv.s")       \
    X(0xfff0007f, 0x58000053, FMT_R, MNEM_FSQRT_S, "fsqrt.s")     \
    X(0xfe00707f, 0x20000053, FMT_R, MNEM_FSGNJ_S, "fsgnj.s")     \
    X(0xfe00707f, 0x20001053, FMT_R, MNEM_FSGNJN_S, "fsgnjn.s")   \
    X(0xfe00707f, 0x20002053, FMT_R, MNEM_FSGNJX_S, "fsgnjx.s")   \
    X(0xfe00707f, 0x28000053, FMT_R, MNEM_FMIN_S, "fmin.s")       \
    X(0xfe00707f, 0x28001053, FMT_R, MNEM_FMAX_S, "fmax.s")       \
    X(0xfff0007f, 0xc0000053, FMT_R, MNEM_FCVT_W_S, "fcvt.w.s")   \
    X(0xfff0007f, 0xc0100053, FMT_R, MNEM_FCVT_WU_S, "fcvt.wu.s") \
    X(0xfff0707f, 0xe0000053, FMT_R, MNEM_FMV_X_W, "fmv.x.w")     \
    X(0xfe00707f, 0xa0002053, FMT_R, MNEM_FEQ_S, "feq.s")         \
    X(0xfe00707f, 0xa0001053, FMT_R, MNEM_FLT_S, "flt.s")         \
    X(0xfe00707f, 0xa0000053, FMT_R, MNEM_FLE_S, "fle.s")         \
    X(0xfff0707f, 0xe0001053, FMT_R, MNEM_FCLASS_S, "fclass.s")   \
    X(0xfff0007f, 0xd0000053, FMT_R, MNEM_FCVT_S_W, "fcvt.s.w")   \
    X(0xfff0007f, 0xd0100053, FMT_R, MNEM_FCVT_S_WU, "fcvt.s.wu") \
    X(0xfff0707f, 0xe0000053, FMT_R, MNEM_FMV_W_X, "fmv.w.x")
#else
#define OPCODE_LIST_F
#endif

#if EXT_D
#define OPCODE_LIST_D                                             \
    X(0x0000707f, 0x00003007, FMT_I, MNEM_FLD, "fld")             \
    X(0x0000707f, 0x00003027, FMT_S, MNEM_FSD, "fsd")             \
    X(0x0600007f, 0x02000043, FMT_R4, MNEM_FMADD_D, "fmadd.d")    \
    X(0x0600007f, 0x02000047, FMT_R4, MNEM_FMSUB_D, "fmsub.d")    \
    X(0x0600007f, 0x0200004b, FMT_R4, MNEM_FNMSUB_D, "fnmsub.d")  \
    X(0x0600007f, 0x0200004f, FMT_R4, MNEM_FNMADD_D, "fnmadd.d")  \
    X(0xfe00007f, 0x02000053, FMT_R, MNEM_FADD_D, "fadd.d")       \
    X(0xfe00007f, 0x0a000053, FMT_R, MNEM_FSUB_D, "fsub.d")       \
    X(0xfe00007f, 0x12000053, FMT_R, MNEM_FMUL_D, "fmul.d")       \
    X(0xfe00007f, 0x1a000053, FMT_R, MNEM_FDIV_D, "fdiv.d")       \
    X(0xfff0007f, 0x5a000053, FMT_R, MNEM_FSQRT_D, "fsqrt.d")     \
    X(0xfe00707f, 0x22000053, FMT_R, MNEM_FSGNJ_D, "fsgnj.d")     \
    X(0xfe00707f, 0x22001053, FMT_R, MNEM_FSGNJN_D, "fsgnjn.d")   \
    X(0xfe00707f, 0x22002053, FMT_R, MNEM_FSGNJX_D, "fsgnjx.d")   \
    X(0xfe00707f, 0x2a000053, FMT_R, MNEM_FMIN_D, "fmin.d")       \
    X(0xfe00707f, 0x2a001053, FMT_R, MNEM_FMAX_D, "fmax.d")       \
    X(0xfff0007f, 0x40100053, FMT_R, MNEM_FCVT_S_D, "fcvt.s.d")   \
    X(0xfff0007f, 0x42000053, FMT_R, MNEM_FCVT_D_S, "fcvt.d.s")   \
    X(0xfe00707f, 0xa2002053, FMT_R, MNEM_FEQ_D, "feq.d")         \
    X(0xfe00707f, 0xa2001053, FMT_R, MNEM_FLT_D, "flt.d")         \
    X(0xfe00707f, 0xa2000053, FMT_R, MNEM_FLE_D, "fle.d")         \
    X(0xfff0707f, 0xe2001053, FMT_R, MNEM_FCLASS_D, "fclass.d")   \
    X(0xfff0007f, 0xc2000053, FMT_R, MNEM_FCVT_W_D, "fcvt.w.d")   \
    X(0xfff0007f, 0xc2100053, FMT_R, MNEM_FCVT_WU_D, "fcvt.wu.d") \
    X(0xfff0007f, 0xd2000053, FMT_R, MNEM_FCVT_D_W, "fcvt.d.w")   \
    X(0xfff0007f, 0xd2100053, FMT_R, MNEM_FCVT_D_WU, "fcvt.d.wu")
#else
#define OPCODE_LIST_D
#endif

#if EXT_A
#define OPCODE_LIST_A                                             \
    X(0xf9f0707f, 0x1000202f, FMT_R, MNEM_LR_W, "lr.w")           \
    X(0xf800707f, 0x1800202f, FMT_R, MNEM_SC_W, "sc.w")           \
    X(0xf800707f, 0x0800202f, FMT_R, MNEM_AMOSWAP_W, "amoswap.w") \
    X(0xf800707f, 0x0000202f, FMT_R, MNEM_AMOADD_W, "amoadd.w")   \
    X(0xf800707f, 0x2000202f, FMT_R, MNEM_AMOXOR_W, "amoxor.w")   \
    X(0xf800707f, 0x6000202f, FMT_R, MNEM_AMOAND_W, "amoand.w")   \
    X(0xf800707f, 0x4000202f, FMT_R, MNEM_AMOOR_W, "amoor.w")     \
    X(0xf800707f, 0x8000202f, FMT_R, MNEM_AMOMIN_W, "amomin.w")   \
    X(0xf800707f, 0xa000202f, FMT_R, MNEM_AMOMAX_W, "amomax.w")   \
    X(0xf800707f, 0xc000202f, FMT_R, MNEM_AMOMINU_W, "amominu.w") \
    X(0xf800707f, 0xe000202f, FMT_R, MNEM_AMOMAXU_W, "amomaxu.w")
#else
#define OPCODE_LIST_A
#endif

#if EXT_C
#define OPCODE_LIST_C                                                 \
    X(0xef83, 0x0001, FMT_CI, MNEM_C_NOP, "c.nop")                    \
    X(0xe003, 0x0001, FMT_CI, MNEM_C_ADDI, "c.addi")                  \
    X(0xe003, 0x2001, FMT_CJ, MNEM_C_JAL, "c.jal")                    \
    X(0xe003, 0x4001, FMT_CI, MNEM_C_LI, "c.li")                      \
    X(0xef83, 0x6101, FMT_CI, MNEM_C_ADDI16SP, "c.addi16sp")          \
    X(0xe003, 0x6001, FMT_CI, MNEM_C_LUI, "c.lui")                    \
    X(0xec03, 0x8001, FMT_CI, MNEM_C_SRLI, "c.srli")                  \
    X(0xec03, 0x8401, FMT_CI, MNEM_C_SRAI, "c.srai")                  \
    X(0xec03, 0x8801, FMT_CI, MNEM_C_ANDI, "c.andi")                  \
    X(0xfc63, 0x8c01, FMT_CR, MNEM_C_SUB, "c.sub")                    \
    X(0xfc63, 0x8c21, FMT_CR, MNEM_C_XOR, "c.xor")                    \
    X(0xfc63, 0x8c41, FMT_CR, MNEM_C_OR, "c.or")                      \
    X(0xfc63, 0x8c61, FMT_CR, MNEM_C_AND, "c.and")                    \
    X(0xe003, 0xa001, FMT_CJ, MNEM_C_J, "c.j")                        \
    X(0xe003, 0xc001, FMT_CB, MNEM_C_BEQZ, "c.beqz")                  \
    X(0xe003, 0xe001, FMT_CB, MNEM_C_BNEZ, "c.bnez")                  \
    X(0xffff, 0x0000, FMT_CIW, MNEM_C_ILLEGAL, "Illegal instruction") \
    X(0xe003, 0x0000, FMT_CIW, MNEM_C_ADDI4SPN, "c.addi4spn")         \
    X(0xe003, 0x2000, FMT_CL, MNEM_C_FLD, "c.fld")                    \
    X(0xe003, 0x4000, FMT_CL, MNEM_C_LW, "c.lw")                      \
    X(0xe003, 0x6000, FMT_CL, MNEM_C_FLW, "c.flw")                    \
    X(0xe003, 0xa000, FMT_CL, MNEM_C_FSD, "c.fsd")                    \
    X(0xe003, 0xc000, FMT_CL, MNEM_C_SW, "c.sw")                      \
    X(0xe003, 0xe000, FMT_CL, MNEM_C_FSW, "c.fsw")                    \
    X(0xe003, 0x0002, FMT_CI, MNEM_C_SLLI, "c.slli")                  \
    X(0xf07f, 0x0002, FMT_CI, MNEM_C_SLLI64, "c.slli64")              \
    X(0xe003, 0x2002, FMT_CSS, MNEM_C_FLDSP, "c.fldsp")               \
    X(0xe003, 0x3002, FMT_CSS, MNEM_C_LWSP, "c.lwsp")                 \
    X(0xe003, 0x6002, FMT_CSS, MNEM_C_FLWSP, "c.flwsp")               \
    X(0xf07f, 0x8002, FMT_CJ, MNEM_C_JR, "c.jr")                      \
    X(0xf003, 0x8002, FMT_CR, MNEM_C_MV, "c.mv")                      \
    X(0xffff, 0x9002, FMT_CI, MNEM_C_EBREAK, "c.ebreak")              \
    X(0xf07f, 0x9002, FMT_CJ, MNEM_C_JALR, "c.jalr")                  \
    X(0xf003, 0x9002, FMT_CR, MNEM_C_ADD, "c.add")                    \
    X(0xe003, 0xa002, FMT_CSS, MNEM_C_FSDSP, "c.fsdsp")               \
    X(0xe003, 0xc002, FMT_CSS, MNEM_C_SWSP, "c.swsp")                 \
    X(0xe003, 0xe002, FMT_CSS, MNEM_C_FSWSP, "c.fswsp")
#else
#define OPCODE_LIST_C
#endif

// TODO: vector
// TODO: 64-bit

#if EXT_PRIV
#define OPCODE_LIST_PRIV                                \
    X(0xffffffff, 0x10200073, FMT_R, MNEM_SRET, "sret") \
    X(0xffffffff, 0x30200073, FMT_R, MNEM_MRET, "mret") \
    X(0xffffffff, 0x10500073, FMT_R, MNEM_WFI, "wfi")   \
    X(0xfe007fff, 0x12000073, FMT_R, MNEM_SFENCE_VMA, "sfence.vma")
#else
#define OPCODE_LIST_PRIV
#endif

#define OPCODE_LIST   \
    OPCODE_LIST_RV32I \
    OPCODE_LIST_M     \
    OPCODE_LIST_F     \
    OPCODE_LIST_D     \
    OPCODE_LIST_A     \
    OPCODE_LIST_C     \
    OPCODE_LIST_PRIV

#define REG_LIST      \
    X(R_X0, "x0")     \
    X(R_RA, "ra")     \
    X(R_SP, "sp")     \
    X(R_GP, "gp")     \
    X(R_TP, "tp")     \
    X(R_T0, "t0")     \
    X(R_T1, "t1")     \
    X(R_T2, "t2")     \
    X(R_S0, "s0")     \
    X(R_FP = 8, "fp") \
    X(R_S1, "s1")     \
    X(R_A0, "a0")     \
    X(R_A1, "a1")     \
    X(R_A2, "a2")     \
    X(R_A3, "a3")     \
    X(R_A4, "a4")     \
    X(R_A5, "a5")     \
    X(R_A6, "a6")     \
    X(R_A7, "a7")     \
    X(R_S2, "s2")     \
    X(R_S3, "s3")     \
    X(R_S4, "s4")     \
    X(R_S5, "s5")     \
    X(R_S6, "s6")     \
    X(R_S7, "s7")     \
    X(R_S8, "s8")     \
    X(R_S9, "s9")     \
    X(R_S10, "s10")   \
    X(R_S11, "s11")   \
    X(R_T3, "t3")     \
    X(R_T4, "t4")     \
    X(R_T5, "t5")     \
    X(R_T6, "t6")

enum {
#define X(ENUM, STR) ENUM,
    REG_LIST
#undef X
};

const char *reg_names[] = {
#define X(ENUM, STR) STR,
    REG_LIST
#undef X
};

typedef enum {
#define X(MASK, VALUE, FMT, MNEM, STR) MNEM,
    OPCODE_LIST
#undef X
        MNEM_INVALID
} mnemonic_t;

const char *op_mnemonics[] = {
#define X(MASK, VALUE, FMT, MNEM, STR) STR,
    OPCODE_LIST
#undef X
};

typedef enum {
    FMT_R,
    FMT_R4,
    FMT_I,
    FMT_S,
    FMT_B,
    FMT_U,
    FMT_J,
    FMT_CB,
    FMT_CI,
    FMT_CIW,
    FMT_CJ,
    FMT_CL,
    FMT_CR,
    FMT_CSS
} fmt_t;

typedef struct {
    fmt_t format;
    mnemonic_t mnemonic;
} instr_header_t;

typedef struct {
    instr_header_t header;
    int rd;
    int func3;
    int rs1;
    int rs2;
    int func7;
} r_instr_t;

typedef struct {
    instr_header_t header;
    int rd;
    int func3;
    int rs1;
    int imm_11_0;
} i_instr_t;

typedef struct {
    instr_header_t header;
    int imm_4_0;
    int func3;
    int rs1;
    int rs2;
    int imm_11_5;
} s_instr_t;

typedef struct {
    instr_header_t header;
    int imm_11_a7;
    int imm_4_1;
    int func3;
    int rs1;
    int rs2;
    int imm_12;
    int imm_10_5;
} b_instr_t;

typedef struct {
    instr_header_t header;
    int rd;
    int imm_31_12;
} u_instr_t;

typedef struct {
    instr_header_t header;
    int rd;
    int imm_19_12;
    int imm_11_a20;
    int imm_10_1;
    int imm_20;
} j_instr_t;

typedef union {
    r_instr_t ri;
    i_instr_t ii;
    s_instr_t si;
    b_instr_t bi;
    u_instr_t ui;
    j_instr_t ji;
} instr_t;

struct {
    uint32_t regs[32];
    uint32_t pc;
} state;

/* memory model */
/* TODO: use dictionary to implement sparse array */

uint32_t memory[256];

uint32_t M_r(uint32_t addr, int size)
{
    addr %= 1024;

    if (addr % size != 0) {
        fprintf(stderr,
                "error: unaligned read access, addr = 0x%08x, size = %d\n",
                addr, size);
        exit(EXIT_FAILURE);
    }

    uint32_t word = memory[addr / 4];
    uint32_t data;
    if (size == 1)
        data = (word >> (addr % 4) * 8) & 0xff;
    else if (size == 2)
        data = (word >> (addr % 4) * 8) & 0xffff;
    else if (size == 4)
        data = word;
    return data;
}

void M_w(uint32_t addr, uint32_t data, int size)
{
    if (addr % size != 0) {
        fprintf(stderr,
                "error: unaligned write access, addr = %08x, size = %d\n", addr,
                size);
        exit(EXIT_FAILURE);
    }

    addr %= 1024;
    uint32_t word = memory[addr / 4];
    if (size == 1) {
        word &= ~(0xff << (addr % 4) * 8);
        word |= (data & 0xff) << 8 * (addr % 4);
    } else if (size == 2) {
        word &= ~(0xffff << (addr % 4) * 8);
        word |= (data & 0xffff) << 8 * (addr % 4);
    } else if (size == 4) {
        word = data;
    }
    memory[addr / 4] = word;
}

/* TODO: handle RV64 */
static uint32_t sext(uint32_t x, int w)
{
    if (w == 32)
        return x;
    int msb = (x >> (w - 1)) & 0x1;
    uint32_t all1 = ~0;
    return msb ? x | ((all1 >> w) << w) : x & ((1 << w) - 1);
}

static uint32_t get_imm(instr_t *i_p)
{
    uint32_t imm;
    instr_header_t *header_p = (instr_header_t *) i_p;
    switch (header_p->format) {
    case FMT_I:
        imm = sext(i_p->ii.imm_11_0, 12);
        break;
    case FMT_S:
        imm = sext((i_p->si.imm_11_5 << 5) + i_p->si.imm_4_0, 12);
        break;
    case FMT_B:
        imm = sext((i_p->bi.imm_12 << 12) + (i_p->bi.imm_11_a7 << 11) +
                       (i_p->bi.imm_10_5 << 5) + (i_p->bi.imm_4_1 << 1),
                   13);
        break;
    case FMT_U:
        imm = (i_p->ui.imm_31_12 << 12);
        break;
    case FMT_J:
        imm = sext((i_p->ji.imm_20 << 20) + (i_p->ji.imm_19_12 << 12) +
                       (i_p->ji.imm_11_a20 << 11) + (i_p->ji.imm_10_1 << 1),
                   21);
        break;
    default:
        imm = 0;
        assert(0);
        break;
    }
    return imm;
}

/* TODO: confirm that all instructions print correctly */
static int snprint_instr(char *str, size_t size, instr_t *i_p)
{
    instr_header_t *header_p = (instr_header_t *) i_p;
    const char *mnemonic = op_mnemonics[header_p->mnemonic];

    const char *rd;
    const char *rs1;
    const char *rs2;
    int imm;
    int n;

    switch (header_p->format) {
    case FMT_R:
        rd = reg_names[i_p->ri.rd];
        rs1 = reg_names[i_p->ri.rs1];
        rs2 = reg_names[i_p->ri.rs2];
        n = snprintf(str, size, "%s %s,%s,%s", mnemonic, rd, rs1, rs2);
        break;
    case FMT_I:
        rd = reg_names[i_p->ii.rd];
        rs1 = reg_names[i_p->ii.rs1];
        imm = get_imm(i_p);
        if (mnemonic[0] == 'l') {
            n = snprintf(str, size, "%s %s,%d(%s)", mnemonic, rd, imm, rs1);
        } else if (mnemonic[0] == 'e') {
            n = snprintf(str, size, "%s", mnemonic);
        } else {
            n = snprintf(str, size, "%s %s,%s,%d", mnemonic, rd, rs1, imm);
        }
        break;
    case FMT_S:
        rs1 = reg_names[i_p->si.rs1];
        rs2 = reg_names[i_p->si.rs2];
        imm = get_imm(i_p);
        n = snprintf(str, size, "%s %s,%d(%s)", mnemonic, rs2, imm, rs1);
        break;
    case FMT_B:
        rs1 = reg_names[i_p->bi.rs1];
        rs2 = reg_names[i_p->bi.rs2];
        imm = get_imm(i_p);
        n = snprintf(str, size, "%s %s,%s,%d", mnemonic, rs1, rs2, imm);
        break;
    case FMT_U:
        rd = reg_names[i_p->ui.rd];
        imm = get_imm(i_p);
        n = snprintf(str, size, "%s %s,%d", mnemonic, rd, imm);
        break;
    case FMT_J:
        rd = reg_names[i_p->ji.rd];
        imm = get_imm(i_p);
        n = snprintf(str, size, "%s %s,%d", mnemonic, rd, imm);
        break;
    // TODO
    case FMT_R4:
        assert(0);
        break;
    case FMT_CB:
        assert(0);
        break;
    case FMT_CI:
        assert(0);
        break;
    case FMT_CIW:
        assert(0);
        break;
    case FMT_CJ:
        assert(0);
        break;
    case FMT_CL:
        assert(0);
        break;
    case FMT_CR:
        assert(0);
        break;
    case FMT_CSS:
        assert(0);
        break;
    default:
        assert(0);
        break;
    }
    return n;
}

/* TODO: some immediate field constraints are not included */
static instr_t decode(uint32_t op)
{
    instr_t output;

    instr_header_t *header_p = (instr_header_t *) &output;

    int rd = MASK_MSB_LSB(op, 11, 7);
    int rs1 = MASK_MSB_LSB(op, 19, 15);
    int rs2 = MASK_MSB_LSB(op, 24, 20);
    int func3 = MASK_MSB_LSB(op, 14, 12);
    int func7 = MASK_MSB_LSB(op, 31, 25);
    int imm_11_0 = MASK_MSB_LSB(op, 31, 20);
    int imm_4_0 = MASK_MSB_LSB(op, 11, 7);
    int imm_11_5 = MASK_MSB_LSB(op, 31, 25);
    int imm_11_a7 = MASK_MSB_LSB(op, 7, 7);
    int imm_4_1 = MASK_MSB_LSB(op, 11, 8);
    int imm_10_5 = MASK_MSB_LSB(op, 30, 25);
    int imm_12 = MASK_MSB_LSB(op, 31, 31);
    int imm_31_12 = MASK_MSB_LSB(op, 31, 12);
    int imm_19_12 = MASK_MSB_LSB(op, 19, 12);
    int imm_11_a20 = MASK_MSB_LSB(op, 20, 20);
    int imm_10_1 = MASK_MSB_LSB(op, 30, 21);
    int imm_20 = MASK_MSB_LSB(op, 31, 31);

    mnemonic_t m = MNEM_INVALID;
    ;

    if (0)
        ;
#define X(MASK, VALUE, FMT, MNEM, STR) else if ((op & MASK) == VALUE) m = MNEM;
    OPCODE_LIST
#undef X

    fmt_t fmt;

    switch (m) {
#define X(MASK, VALUE, FMT, MNEM, STR) \
    case MNEM:                         \
        fmt = FMT;                     \
        break;
        OPCODE_LIST
#undef X
    default:
        assert(0);
        break;
    }

    header_p->mnemonic = m;
    header_p->format = fmt;

    switch (fmt) {
    case FMT_R:
        output.ri.rd = rd;
        output.ri.func3 = func3;
        output.ri.rs1 = rs1;
        output.ri.rs2 = rs2;
        output.ri.func7 = func7;
        break;
    case FMT_I:
        output.ii.rd = rd;
        output.ii.func3 = func3;
        output.ii.rs1 = rs1;
        output.ii.imm_11_0 = imm_11_0;
        break;

    case FMT_S:
        output.si.imm_4_0 = imm_4_0;
        output.si.func3 = func3;
        output.si.rs1 = rs1;
        output.si.rs2 = rs2;
        output.si.imm_11_5 = imm_11_5;
        break;

    case FMT_B:
        output.bi.imm_11_a7 = imm_11_a7;
        output.bi.imm_4_1 = imm_4_1;
        output.bi.func3 = func3;
        output.bi.rs1 = rs1;
        output.bi.rs2 = rs2;
        output.bi.imm_12 = imm_12;
        output.bi.imm_10_5 = imm_10_5;
        break;

    case FMT_U:
        output.ui.rd = rd;
        output.ui.imm_31_12 = imm_31_12;
        break;

    case FMT_J:
        output.ji.rd = rd;
        output.ji.imm_19_12 = imm_19_12;
        output.ji.imm_11_a20 = imm_11_a20;
        output.ji.imm_10_1 = imm_10_1;
        output.ji.imm_20 = imm_20;
        break;

    // TODO
    case FMT_R4:
        assert(0);
        break;
    case FMT_CB:
        assert(0);
        break;
    case FMT_CI:
        assert(0);
        break;
    case FMT_CIW:
        assert(0);
        break;
    case FMT_CJ:
        assert(0);
        break;
    case FMT_CL:
        assert(0);
        break;
    case FMT_CR:
        assert(0);
        break;
    case FMT_CSS:
        assert(0);
        break;

    default:
        assert(0);
        break;
    }

    return output;
}

static void reset()
{
    size_t i;
    for (i = 0; i < 32; i++) {
        state.regs[i] = 0;
    }
    state.pc = 0;

    /* TODO: remove these; put them in the test itself */
    state.regs[R_A1] = 5;     // n
    state.regs[R_A0] = 0x54;  // &a[0]
    state.regs[R_RA] = 0x4c;  // return address
}

/* TODO: pc, x[0] potentially bug-prone */
static int step()
{
    uint32_t op = M_r(state.pc, 4);
    // compressed
    int compressed = ((op & 3) != 3);
    if (compressed)
        op &= 0xffff;
    instr_t instr = decode(op);
    instr_header_t *header_p = (instr_header_t *) &instr;

    int32_t *x = (int32_t *) state.regs;
    uint32_t *x_u = state.regs;
    uint32_t pc = state.pc;
    uint32_t next_pc = pc + (compressed ? 2 : 4);

    int rd;
    int rs1;
    int rs2;
    int32_t imm;
    uint32_t imm_u;
    int32_t t;

    int shamt;
    int pred;
    int succ;
    int csr;
    int zimm;

    switch (header_p->format) {
    case FMT_R:
        rd = instr.ri.rd;
        rs1 = instr.ri.rs1;
        rs2 = instr.ri.rs2;
        break;
    case FMT_I:
        rd = instr.ii.rd;
        rs1 = instr.ii.rs1;
        imm = get_imm(&instr);
        shamt = MASK_MSB_LSB(instr.ii.imm_11_0, 4, 0);
        pred = MASK_MSB_LSB(instr.ii.imm_11_0, 7, 4);
        succ = MASK_MSB_LSB(instr.ii.imm_11_0, 3, 0);
        csr = instr.ii.imm_11_0;
        zimm = instr.ii.rs1;
        break;
    case FMT_S:
        rs1 = instr.si.rs1;
        rs2 = instr.si.rs2;
        imm = get_imm(&instr);
        imm_u = (uint32_t) imm;
        break;
    case FMT_B:
        rs1 = instr.bi.rs1;
        rs2 = instr.bi.rs2;
        imm = get_imm(&instr);
        imm_u = (uint32_t) imm;
        break;
    case FMT_U:
        rd = instr.ii.rd;
        imm = get_imm(&instr);
        imm_u = (uint32_t) imm;
        break;
    case FMT_J:
        rd = instr.ii.rd;
        imm = get_imm(&instr);
        imm_u = (uint32_t) imm;
        break;
    // TODO
    case FMT_R4:
        assert(0);
        break;
    case FMT_CB:
        assert(0);
        break;
    case FMT_CI:
        assert(0);
        break;
    case FMT_CIW:
        assert(0);
        break;
    case FMT_CJ:
        assert(0);
        break;
    case FMT_CL:
        assert(0);
        break;
    case FMT_CR:
        assert(0);
        break;
    case FMT_CSS:
        assert(0);
        break;
    default:
        assert(0);
        break;
    }

    int xd_set = 0;
    int write = 0;
    int load = 0;
    int br_taken = 0;

    switch (header_p->mnemonic) {
    case MNEM_LUI:
        x[rd] = imm;
        xd_set = 1;
        break;
    case MNEM_AUIPC:
        x[rd] = pc + imm;
        xd_set = 1;
        break;
    case MNEM_JAL:
        x[rd] = pc + 4;
        next_pc = pc + imm;
        br_taken = 1;
        xd_set = 1;
        break;
    case MNEM_JALR:
        t = pc + 4;
        next_pc = (x[rs1] + imm) & ~1;
        br_taken = 1;
        x[rd] = t;
        xd_set = 1;
        break;
    case MNEM_BEQ:
        if (x[rs1] == x[rs2]) {
            next_pc = pc + imm;
            br_taken = 1;
        }
        break;
    case MNEM_BNE:
        if (x[rs1] != x[rs2]) {
            next_pc = pc + imm;
            br_taken = 1;
        }
        break;
    case MNEM_BLT:
        if (x[rs1] < x[rs2]) {
            next_pc = pc + imm;
            br_taken = 1;
        }
        break;
    case MNEM_BGE:
        if (x[rs1] >= x[rs2]) {
            next_pc = pc + imm;
            br_taken = 1;
        }
        break;
    case MNEM_BLTU:
        if (x_u[rs1] < x_u[rs2]) {
            next_pc = pc + imm;
            br_taken = 1;
        }
        break;
    case MNEM_BGEU:
        if (x_u[rs1] >= x_u[rs2]) {
            next_pc = pc + imm;
            br_taken = 1;
        }
        break;
    case MNEM_LB:
        x[rd] = sext(M_r(x[rs1] + imm, 1), 8);
        xd_set = 1;
        load = 1;
        break;
    case MNEM_LH:
        x[rd] = sext(M_r(x[rs1] + imm, 2), 16);
        xd_set = 1;
        load = 1;
        break;
    case MNEM_LW:
        x[rd] = sext(M_r(x[rs1] + imm, 4), 32);
        xd_set = 1;
        load = 1;
        break;
    case MNEM_LBU:
        x[rd] = M_r(x[rs1] + imm, 1);
        xd_set = 1;
        break;
    case MNEM_LHU:
        x[rd] = M_r(x[rs1] + imm, 2);
        xd_set = 1;
        break;
    case MNEM_SB:
        M_w(x[rs1] + imm, x[rs2], 1);
        write = 1;
        break;
    case MNEM_SH:
        M_w(x[rs1] + imm, x[rs2], 2);
        write = 1;
        break;
    case MNEM_SW:
        M_w(x[rs1] + imm, x[rs2], 4);
        write = 1;
        break;
    case MNEM_ADDI:
        x[rd] = x[rs1] + imm;
        xd_set = 1;
        break;
    case MNEM_SLTI:
        x[rd] = x[rs1] < imm;
        xd_set = 1;
        break;
    case MNEM_SLTIU:
        x[rd] = x_u[rs1] < imm_u;
        xd_set = 1;
        break;
    case MNEM_XORI:
        x[rd] = x[rs1] ^ imm;
        xd_set = 1;
        break;
    case MNEM_ORI:
        x[rd] = x[rs1] | imm;
        xd_set = 1;
        break;
    case MNEM_ANDI:
        x[rd] = x[rs1] & imm;
        xd_set = 1;
        break;
    case MNEM_SLLI:
        x[rd] = x[rs1] << shamt;
        xd_set = 1;
        break;
    case MNEM_SRLI:
        /* TODO: add static assertion that right-shifting of signed value is
         * arithmetic */
        x[rd] = x_u[rs1] >> shamt;
        xd_set = 1;
        break;
    case MNEM_SRAI:
        x[rd] = x[rs1] >> shamt;
        xd_set = 1;
        break;
    case MNEM_ADD:
        x[rd] = x[rs1] + x[rs2];
        xd_set = 1;
        break;
    case MNEM_SUB:
        x[rd] = x[rs1] - x[rs2];
        xd_set = 1;
        break;
    case MNEM_SLL:
        x[rd] = x[rs1] << x[rs2];
        xd_set = 1;
        break;
    case MNEM_SLT:
        x[rd] = x[rs1] < x[rs2];
        xd_set = 1;
        break;
    case MNEM_SLTU:
        x[rd] = x_u[rs1] < x_u[rs2];
        xd_set = 1;
        break;
    case MNEM_XOR:
        x[rd] = x[rs1] ^ x[rs1];
        xd_set = 1;
        break;
    case MNEM_SRL:
        x[rd] = x_u[rs1] >> x_u[rs2];
        xd_set = 1;
        break;
    case MNEM_SRA:
        x[rd] = x[rs1] >> x[rs2];
        xd_set = 1;
        break;
    case MNEM_OR:
        x[rd] = x[rs1] | x[rs1];
        xd_set = 1;
        break;
    case MNEM_AND:
        x[rd] = x[rs1] & x[rs1];
        xd_set = 1;
        break;
    case MNEM_FENCE:
        /* TODO */
        break;
    case MNEM_FENCE_I:
        /* TODO */
        break;
    case MNEM_ECALL:
        /* TODO */
        break;
    case MNEM_EBREAK:
        return 1;
        /* TODO */
        break;
    case MNEM_CSRRW:
        /* TODO */
        break;
    case MNEM_CSRRS:
        /* TODO */
        break;
    case MNEM_CSRRC:
        /* TODO */
        break;
    case MNEM_CSRRWI:
        /* TODO */
        break;
    case MNEM_CSRRSI:
        /* TODO */
        break;
    case MNEM_CSRRCI:
        /* TODO */
        break;
    default:
        assert(0);
        break;
    }

    char buffer[1024];
    int len = snprint_instr(buffer, sizeof(buffer), &instr);
    printf("%08x: %08x ; %s", (int) pc, op, buffer);
    for (int i = 0; i < 20 - len; i++)
        printf(" ");
    if (br_taken)
        printf("[branch]");
    if (xd_set)
        printf(" x[%d] <= %d", rd, x[rd]);
    if (write)
        printf("    M[%x] = %x", x[rs1] + imm, x[rs2]);
    if (load)
        printf("    M[%x]", x[rs1] + imm);
    printf("\n");

    state.pc = next_pc;
    x[0] = 0;

    // getchar();

    return 0;
}

static void print_state()
{
    uint32_t pc = state.pc;
    int32_t *x = (int32_t *) state.regs;
    printf("pc: %08x x8:  %08x x16: %08x x24: %08x\n", pc, x[8], x[16], x[24]);
    printf("x1: %08x x9:  %08x x17: %08x x25: %08x\n", x[1], x[9], x[17],
           x[25]);
    printf("x2: %08x x10: %08x x18: %08x x26: %08x\n", x[2], x[10], x[18],
           x[26]);
    printf("x3: %08x x11: %08x x19: %08x x27: %08x\n", x[3], x[11], x[19],
           x[27]);
    printf("x4: %08x x12: %08x x20: %08x x28: %08x\n", x[4], x[12], x[20],
           x[28]);
    printf("x5: %08x x13: %08x x21: %08x x29: %08x\n", x[5], x[13], x[21],
           x[29]);
    printf("x6: %08x x14: %08x x22: %08x x30: %08x\n", x[6], x[14], x[22],
           x[30]);
    printf("x7: %08x x15: %08x x23: %08x x31: %08x\n", x[7], x[15], x[23],
           x[31]);
}

int main()
{
    uint32_t ops[] = {
        /* insertion sort */
        0x00450693, 0x00100713, 0x00b76463, 0x00008067, 0x0006a803, 0x00068613,
        0x00070793, 0xffc62883, 0x01185a63, 0x01162023, 0xfff78793, 0xffc60613,
        0xfe0796e3, 0x00279793, 0x00f507b3, 0x0107a023, 0x00170713, 0x00468693,
        0xfc1ff06f, 0x00100073, 0x00000000, 0x00000004, 0x00000003, 0x00000007,
        0x00000002, 0x00000005,
    };
    size_t n_ops = NELEM(ops);

    /* load program into memory */
    for (size_t i = 0; i < n_ops; i++) {
        M_w(i * 4, ops[i], 4);
    }

    reset();

    print_state();
    int step_cnt = 0;
    while (1) {
        if (step())
            break;
        if (step_cnt++ > MAX_STEPS) {
            fprintf(stderr, "exceeded MAX_STEPS, quitting\n");
            break;
        }
    }
    print_state();

    return 0;
}
