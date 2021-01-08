#pragma once
#include <stdbool.h>

#include "riscv.h"

#define RV_NUM_REGS 32

// csrs
enum {
    // floating point
    CSR_FFLAGS = 0x001,
    CSR_FRM = 0x002,
    CSR_FCSR = 0x003,

    // machine trap status
    CSR_MSTATUS = 0x300,
    CSR_MISA = 0x301,
    CSR_MEDELEG = 0x302,
    CSR_MIDELEG = 0x303,
    CSR_MIE = 0x304,
    CSR_MTVEC = 0x305,
    CSR_MCOUNTEREN = 0x306,

    // machine trap handling
    CSR_MSCRATCH = 0x340,
    CSR_MEPC = 0x341,
    CSR_MCAUSE = 0x342,
    CSR_MTVAL = 0x343,
    CSR_MIP = 0x344,

    // low words
    CSR_CYCLE = 0xC00,
    CSR_TIME = 0xC01,
    CSR_INSTRET = 0xC02,

    // high words
    CSR_CYCLEH = 0xC80,
    CSR_TIMEH = 0xC81,
    CSR_INSTRETH = 0xC82,

    CSR_MVENDORID = 0xF11,
    CSR_MARCHID = 0xF12,
    CSR_MIMPID = 0xF13,
    CSR_MHARTID = 0xF14,
};

// clang-format off
// instruction decode masks
enum {
    //               ....xxxx....xxxx....xxxx....xxxx
    INST_6_2     = 0b00000000000000000000000001111100,
    //               ....xxxx....xxxx....xxxx....xxxx
    FR_OPCODE    = 0b00000000000000000000000001111111, // r-type
    FR_RD        = 0b00000000000000000000111110000000,
    FR_FUNCT3    = 0b00000000000000000111000000000000,
    FR_RS1       = 0b00000000000011111000000000000000,
    FR_RS2       = 0b00000001111100000000000000000000,
    FR_FUNCT7    = 0b11111110000000000000000000000000,
    //               ....xxxx....xxxx....xxxx....xxxx
    FI_IMM_11_0  = 0b11111111111100000000000000000000, // i-type
    //               ....xxxx....xxxx....xxxx....xxxx
    FS_IMM_4_0   = 0b00000000000000000000111110000000, // s-type
    FS_IMM_11_5  = 0b11111110000000000000000000000000,
    //               ....xxxx....xxxx....xxxx....xxxx
    FB_IMM_11    = 0b00000000000000000000000010000000, // b-type
    FB_IMM_4_1   = 0b00000000000000000000111100000000,
    FB_IMM_10_5  = 0b01111110000000000000000000000000,
    FB_IMM_12    = 0b10000000000000000000000000000000,
    //               ....xxxx....xxxx....xxxx....xxxx
    FU_IMM_31_12 = 0b11111111111111111111000000000000, // u-type
    //               ....xxxx....xxxx....xxxx....xxxx
    FJ_IMM_19_12 = 0b00000000000011111111000000000000, // j-type
    FJ_IMM_11    = 0b00000000000100000000000000000000,
    FJ_IMM_10_1  = 0b01111111111000000000000000000000,
    FJ_IMM_20    = 0b10000000000000000000000000000000,
    //               ....xxxx....xxxx....xxxx....xxxx
    FR4_FMT      = 0b00000110000000000000000000000000, // r4-type
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
// clang-format off

struct riscv_t {
    bool halt;

    // io interface
    struct riscv_io_t io;

    // integer registers
    riscv_word_t X[RV_NUM_REGS];
    riscv_word_t PC;

    // user provided data
    riscv_user_t userdata;

    // csr registers
    uint64_t csr_cycle;
    uint32_t csr_mstatus;
    uint32_t csr_mtvec;
    uint32_t csr_misa;
    uint32_t csr_mtval;
    uint32_t csr_mcause;
    uint32_t csr_mscratch;
    uint32_t csr_mepc;
    uint32_t csr_mip;
    uint32_t csr_mbadaddr;

    // current instruction length
    enum {
        INST_UNKNOWN = 0,
        INST_16 = 0x02,
        INST_32 = 0x04,
    }inst_len;
};

// decode rd field
static inline uint32_t dec_rd(uint32_t inst)
{
    return (inst & FR_RD) >> 7;
}

// decode rs1 field
static inline uint32_t dec_rs1(uint32_t inst)
{
    return (inst & FR_RS1) >> 15;
}

// decode rs2 field
static inline uint32_t dec_rs2(uint32_t inst)
{
    return (inst & FR_RS2) >> 20;
}

// decoded funct3 field
static inline uint32_t dec_funct3(uint32_t inst)
{
    return (inst & FR_FUNCT3) >> 12;
}

// decode funct7 field
static inline uint32_t dec_funct7(uint32_t inst)
{
    return (inst & FR_FUNCT7) >> 25;
}

// decode utype instruction immediate
static inline uint32_t dec_utype_imm(uint32_t inst)
{
    return inst & FU_IMM_31_12;
}

// decode jtype instruction immediate
static inline int32_t dec_jtype_imm(uint32_t inst)
{
    uint32_t dst = 0;
    dst |= (inst & FJ_IMM_20);
    dst |= (inst & FJ_IMM_19_12) << 11;
    dst |= (inst & FJ_IMM_11) << 2;
    dst |= (inst & FJ_IMM_10_1) >> 9;
    // note: shifted to 2nd least significant bit
    return ((int32_t) dst) >> 11;
}

// decode itype instruction immediate
static inline int32_t dec_itype_imm(uint32_t inst)
{
    return ((int32_t)(inst & FI_IMM_11_0)) >> 20;
}

// decode r4type format field
static inline uint32_t dec_r4type_fmt(uint32_t inst)
{
    return (inst & FR4_FMT) >> 25;
}

// decode r4type rs3 field
static inline uint32_t dec_r4type_rs3(uint32_t inst)
{
    return (inst & FR4_RS3) >> 27;
}

// decode csr instruction immediate (same as itype, zero extend)
static inline uint32_t dec_csr(uint32_t inst)
{
    return ((uint32_t)(inst & FI_IMM_11_0)) >> 20;
}

// decode btype instruction immediate
static inline int32_t dec_btype_imm(uint32_t inst)
{
    uint32_t dst = 0;
    dst |= (inst & FB_IMM_12);
    dst |= (inst & FB_IMM_11) << 23;
    dst |= (inst & FB_IMM_10_5) >> 1;
    dst |= (inst & FB_IMM_4_1) << 12;
    // note: shifted to 2nd least significant bit
    return ((int32_t) dst) >> 19;
}

// decode stype instruction immediate
static inline int32_t dec_stype_imm(uint32_t inst)
{
    uint32_t dst = 0;
    dst |= (inst & FS_IMM_11_5);
    dst |= (inst & FS_IMM_4_0) << 13;
    return ((int32_t) dst) >> 20;
}

// sign extend a 16 bit value
static inline uint32_t sign_extend_h(uint32_t x)
{
    return (int32_t)((int16_t) x);
}

// sign extend an 8 bit value
static inline uint32_t sign_extend_b(uint32_t x)
{
    return (int32_t)((int8_t) x);
}

// decode rs1 field
static inline uint16_t c_dec_rs1(uint16_t x){
    return (uint16_t)((x & FC_RS1) >> 7U);
}

// decode rs2 field
static inline uint16_t c_dec_rs2(uint16_t x){
    return (uint16_t)((x & FC_RS2) >> 2U);
}

// decode rd field
static inline uint16_t c_dec_rd(uint16_t x){
    return (uint16_t)((x & FC_RD) >> 7U);
}

// decode rs1' field
static inline uint16_t c_dec_rs1c(uint16_t x){
    return (uint16_t)((x & FC_RS1C) >> 7U);
}

// decode rs2' field
static inline uint16_t c_dec_rs2c(uint16_t x){
    return (uint16_t)((x & FC_RS2C) >> 2U);
}

// decode rd' field
static inline uint16_t c_dec_rdc(uint16_t x){
    return (uint16_t)((x & FC_RDC) >> 2U);
}

static inline uint16_t c_dec_cjtype_imm(uint16_t x){
    uint16_t temp = 0;
    //                ....xxxx....xxxx
    temp |= (x & 0b0000000000111000) >> 2;
    temp |= (x & 0b0000100000000000) >> 7;
    temp |= (x & 0b0000000000000100) << 3;
    temp |= (x & 0b0000000010000000) >> 1;
    temp |= (x & 0b0000000001000000) << 1;
    temp |= (x & 0b0000011000000000) >> 1;
    temp |= (x & 0b0000000100000000) << 2;
    temp |= (x & 0b0001000000000000) >> 1;
    // extend to 16 bit
    for(int i = 1; i < 4; ++i){
        temp |= (0x0800 & temp) << i;
    }

    return temp;
}

static inline uint16_t c_dec_cbtype_imm(uint16_t x){
    uint16_t temp = 0;
    //             ....xxxx....xxxx
    temp |= (x & 0b0000000000011000) >> 2;
    temp |= (x & 0b0000110000000000) >> 7;
    temp |= (x & 0b0000000000000100) << 3;
    temp |= (x & 0b0000000001100000) << 1;
    temp |= (x & 0b0001000000000000) >> 4;
    // extend to 16 bit
    for(int i = 1; i < 8; ++i){
        temp |= (0x0100 & temp) << i;
    }
    return temp;
}