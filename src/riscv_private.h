/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once
#include <stdbool.h>

#if RV32_HAS(GDBSTUB)
#include "breakpoint.h"
#include "mini-gdbstub/include/gdbstub.h"
#endif
#include "riscv.h"

#define RV_NUM_REGS 32

/* csrs */
enum {
    /* floating point */
    CSR_FFLAGS = 0x001, /* Floating-point accrued exceptions */
    CSR_FRM = 0x002,    /* Floating-point dynamic rounding mode */
    CSR_FCSR = 0x003,   /* Floating-point control and status register */

    /* Machine trap setup */
    CSR_MSTATUS = 0x300,    /* Machine status register */
    CSR_MISA = 0x301,       /* ISA and extensions */
    CSR_MEDELEG = 0x302,    /* Machine exception delegate register */
    CSR_MIDELEG = 0x303,    /* Machine interrupt delegate register */
    CSR_MIE = 0x304,        /* Machine interrupt-enable register */
    CSR_MTVEC = 0x305,      /* Machine trap-handler base address */
    CSR_MCOUNTEREN = 0x306, /* Machine counter enable */

    /* machine trap handling */
    CSR_MSCRATCH = 0x340, /* Scratch register for machine trap handlers */
    CSR_MEPC = 0x341,     /* Machine exception program counter */
    CSR_MCAUSE = 0x342,   /* Machine trap cause */
    CSR_MTVAL = 0x343,    /* Machine bad address or instruction */
    CSR_MIP = 0x344,      /* Machine interrupt pending */

    /* low words */
    CSR_CYCLE = 0xC00, /* Cycle counter for RDCYCLE instruction */
    CSR_TIME = 0xC01,  /* Timer for RDTIME instruction */
    CSR_INSTRET = 0xC02,

    /* high words */
    CSR_CYCLEH = 0xC80,
    CSR_TIMEH = 0xC81,
    CSR_INSTRETH = 0xC82,

    CSR_MVENDORID = 0xF11, /* Vendor ID */
    CSR_MARCHID = 0xF12,   /* Architecture ID */
    CSR_MIMPID = 0xF13,    /* Implementation ID */
    CSR_MHARTID = 0xF14,   /* Hardware thread ID */
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

/* clang-format off */

enum {
    INSN_UNKNOWN = 0,
    INSN_16 = 2,
    INSN_32 = 4,
};

struct riscv_t {
    bool halt;

    /* I/O interface */
    struct riscv_io_t io;

    /* integer registers */
    riscv_word_t X[RV_NUM_REGS];
    riscv_word_t PC;

    /* user provided data */
    riscv_user_t userdata;

#if RV32_HAS(GDBSTUB)
    /* gdbstub instance */
    gdbstub_t gdbstub;

    /* GDB instruction breakpoint */
    breakpoint_map_t breakpoint_map;
#endif

#if RV32_HAS(EXT_F)
    /* float registers */
    union {
        riscv_float_t F[RV_NUM_REGS];
        uint32_t F_int[RV_NUM_REGS]; /* integer shortcut */
    };
    uint32_t csr_fcsr;
#endif

    /* csr registers */
    uint64_t csr_cycle;
    uint64_t csr_time;
    uint32_t csr_mstatus;
    uint32_t csr_mtvec;
    uint32_t csr_misa;
    uint32_t csr_mtval;
    uint32_t csr_mcause;
    uint32_t csr_mscratch;
    uint32_t csr_mepc;
    uint32_t csr_mip;
    uint32_t csr_mbadaddr;

    /* current instruction length */
    uint8_t insn_len;
};

/* decode rd field */
static inline uint32_t dec_rd(const uint32_t insn)
{
    return (insn & FR_RD) >> 7;
}

/* decode rs1 field.
 * rs1 = insn[19:15]
 */
static inline uint32_t dec_rs1(const uint32_t insn)
{
    return (insn & FR_RS1) >> 15;
}

/* decode rs2 field.
 * rs2 = insn[24:20]
 */
static inline uint32_t dec_rs2(const uint32_t insn)
{
    return (insn & FR_RS2) >> 20;
}

/* decoded funct3 field.
 * funct3 = insn[14:12]
 */
static inline uint32_t dec_funct3(const uint32_t insn)
{
    return (insn & FR_FUNCT3) >> 12;
}

/* decode funct7 field.
 * funct7 = insn[31:25]
 */
static inline uint32_t dec_funct7(const uint32_t insn)
{
    return (insn & FR_FUNCT7) >> 25;
}

/* decode U-type instruction immediate.
 * imm[31:12] = insn[31:12]
 */
static inline uint32_t dec_utype_imm(const uint32_t insn)
{
    return insn & FU_IMM_31_12;
}

/* decode J-type instruction immediate.
 * imm[20|10:1|11|19:12] = insn[31|30:21|20|19:12]
 */
static inline int32_t dec_jtype_imm(const uint32_t insn)
{
    uint32_t dst = 0;
    dst |= (insn & FJ_IMM_20);
    dst |= (insn & FJ_IMM_19_12) << 11;
    dst |= (insn & FJ_IMM_11) << 2;
    dst |= (insn & FJ_IMM_10_1) >> 9;
    /* NOTE: shifted to 2nd least significant bit */
    return ((int32_t) dst) >> 11;
}

/* decode I-type instruction immediate.
 * imm[11:0] = insn[31:20]
 */
static inline int32_t dec_itype_imm(const uint32_t insn)
{
    return ((int32_t)(insn & FI_IMM_11_0)) >> 20;
}

/* decode R4-type format field */
static inline uint32_t dec_r4type_fmt(const uint32_t insn)
{
    return (insn & FR4_FMT) >> 25;
}

/* decode R4-type rs3 field */
static inline uint32_t dec_r4type_rs3(const uint32_t insn)
{
    return (insn & FR4_RS3) >> 27;
}

/* decode system instruction immediate (same as itype) */
static inline uint32_t dec_funct12(const uint32_t insn)
{
    return ((uint32_t)(insn & FI_IMM_11_0)) >> 20;
}

/* decode csr instruction immediate (same as itype, zero extend) */
static inline uint32_t dec_csr(const uint32_t insn)
{
    return ((uint32_t)(insn & FI_IMM_11_0)) >> 20;
}

/* decode B-type instruction immediate.
 * imm[12|10:5|4:1|11] = insn[31|30:25|11:8|7]
 */
static inline int32_t dec_btype_imm(const uint32_t insn)
{
    uint32_t dst = 0;
    dst |= (insn & FB_IMM_12);
    dst |= (insn & FB_IMM_11) << 23;
    dst |= (insn & FB_IMM_10_5) >> 1;
    dst |= (insn & FB_IMM_4_1) << 12;
    /* NOTE: shifted to 2nd least significant bit */
    return ((int32_t) dst) >> 19;
}

/* decode S-type instruction immediate.
 * imm[11:5] = insn[31:25]
 * imm[4:0] = insn[11:7]
 */
static inline int32_t dec_stype_imm(const uint32_t insn)
{
    uint32_t dst = 0;
    dst |= (insn & FS_IMM_11_5);
    dst |= (insn & FS_IMM_4_0) << 13;
    return ((int32_t) dst) >> 20;
}

/* sign extend a 16 bit value */
static inline uint32_t sign_extend_h(const uint32_t x)
{
    return (int32_t)((int16_t) x);
}

/* sign extend an 8 bit value */
static inline uint32_t sign_extend_b(const uint32_t x)
{
    return (int32_t)((int8_t) x);
}



#if RV32_HAS(EXT_C)
enum {
    /*             ....xxxx....xxxx */
    CJ_IMM_11  = 0b0001000000000000,
    CJ_IMM_4   = 0b0000100000000000,
    CJ_IMM_9_8 = 0b0000011000000000,
    CJ_IMM_10  = 0b0000000100000000,
    CJ_IMM_6   = 0b0000000010000000,
    CJ_IMM_7   = 0b0000000001000000,
    CJ_IMM_3_1 = 0b0000000000111000,
    CJ_IMM_5   = 0b0000000000000100,
};

/* decode rs1 field */
static inline uint16_t c_dec_rs1(const uint16_t x)
{
    return (uint16_t)((x & FC_RS1) >> 7U);
}

/* decode rs2 field */
static inline uint16_t c_dec_rs2(const uint16_t x)
{
    return (uint16_t)((x & FC_RS2) >> 2U);
}

/* decode rd field */
static inline uint16_t c_dec_rd(const uint16_t x)
{
    return (uint16_t)((x & FC_RD) >> 7U);
}

/* decode rs1' field */
static inline uint16_t c_dec_rs1c(const uint16_t x)
{
    return (uint16_t)((x & FC_RS1C) >> 7U);
}

/* decode rs2' field */
static inline uint16_t c_dec_rs2c(const uint16_t x)
{
    return (uint16_t)((x & FC_RS2C) >> 2U);
}

/* decode rd' field */
static inline uint16_t c_dec_rdc(const uint16_t x)
{
    return (uint16_t)((x & FC_RDC) >> 2U);
}

static inline int32_t c_dec_cjtype_imm(const uint16_t x)
{
    uint16_t tmp = 0;

    tmp |= (x & CJ_IMM_3_1) >> 2;
    tmp |= (x & CJ_IMM_4)   >> 7;
    tmp |= (x & CJ_IMM_5)   << 3;
    tmp |= (x & CJ_IMM_6)   >> 1;
    tmp |= (x & CJ_IMM_7)   << 1;
    tmp |= (x & CJ_IMM_9_8) >> 1;
    tmp |= (x & CJ_IMM_10)  << 2;
    tmp |= (x & CJ_IMM_11)  >> 1;

    for (int i = 1; i <= 4; ++i)
        tmp |= (0x0800 & tmp) << i;

    /* extend to 16 bit */
    return (int32_t)(int16_t) tmp;
}

static inline uint16_t c_dec_cbtype_imm(const uint16_t x)
{
    uint16_t tmp = 0;
    /*            ....xxxx....xxxx     */
    tmp |= (x & 0b0000000000011000) >> 2;
    tmp |= (x & 0b0000110000000000) >> 7;
    tmp |= (x & 0b0000000000000100) << 3;
    tmp |= (x & 0b0000000001100000) << 1;
    tmp |= (x & 0b0001000000000000) >> 4;

    /* extend to 16 bit */
    for (int i = 1; i <= 8; ++i)
        tmp |= (0x0100 & tmp) << i;
    return tmp;
}
#endif
