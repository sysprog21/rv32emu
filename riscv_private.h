#pragma once
#include <stdbool.h>

#ifdef ENABLE_GDBSTUB
#include "mini-gdbstub/include/gdbstub.h"
#endif
#include "riscv.h"

#define RV_NUM_REGS 32

/* csrs */
enum {
    /* floating point */
    CSR_FFLAGS = 0x001,
    CSR_FRM = 0x002,
    CSR_FCSR = 0x003,

    /* machine trap status */
    CSR_MSTATUS = 0x300,
    CSR_MISA = 0x301,
    CSR_MEDELEG = 0x302,
    CSR_MIDELEG = 0x303,
    CSR_MIE = 0x304,
    CSR_MTVEC = 0x305,
    CSR_MCOUNTEREN = 0x306,

    /* machine trap handling */
    CSR_MSCRATCH = 0x340,
    CSR_MEPC = 0x341,
    CSR_MCAUSE = 0x342,
    CSR_MTVAL = 0x343,
    CSR_MIP = 0x344,

    /* low words */
    CSR_CYCLE = 0xC00,
    CSR_TIME = 0xC01,
    CSR_INSTRET = 0xC02,

    /* high words */
    CSR_CYCLEH = 0xC80,
    CSR_TIMEH = 0xC81,
    CSR_INSTRETH = 0xC82,

    CSR_MVENDORID = 0xF11,
    CSR_MARCHID = 0xF12,
    CSR_MIMPID = 0xF13,
    CSR_MHARTID = 0xF14,
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

#ifdef ENABLE_RV32F
enum {
    //                    ....xxxx....xxxx....xxxx....xxxx
    FMASK_SIGN        = 0b10000000000000000000000000000000,
    FMASK_EXPN        = 0b01111111100000000000000000000000,
    FMASK_FRAC        = 0b00000000011111111111111111111111,
    //                    ....xxxx....xxxx....xxxx....xxxx
    FFLAG_MASK        = 0b00000000000000000000000000011111,
    FFLAG_INVALID_OP  = 0b00000000000000000000000000010000,
    FFLAG_DIV_BY_ZERO = 0b00000000000000000000000000001000,
    FFLAG_OVERFLOW    = 0b00000000000000000000000000000100,
    FFLAG_UNDERFLOW   = 0b00000000000000000000000000000010,
    FFLAG_INEXACT     = 0b00000000000000000000000000000001,
    //                    ....xxxx....xxxx....xxxx....xxxx
    RV_NAN            = 0b01111111110000000000000000000000
};
#endif
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

#ifdef ENABLE_GDBSTUB
    /* gdbstub instance */
    gdbstub_t gdbstub;

    /* GDB instruction breakpoint */
    bool breakpoint_specified;
    riscv_word_t breakpoint_addr;
#endif

#ifdef ENABLE_RV32F
    /* float registers */
    union {
        riscv_float_t F[RV_NUM_REGS];
        uint32_t F_int[RV_NUM_REGS]; /* integer shortcut */
    };
    uint32_t csr_fcsr;
#endif

    /* csr registers */
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

#ifdef ENABLE_RV32F
/* compute the fclass result */
static inline uint32_t calc_fclass(uint32_t f) {
  const uint32_t sign = f & FMASK_SIGN;
  const uint32_t expn = f & FMASK_EXPN;
  const uint32_t frac = f & FMASK_FRAC;

  /* TODO: optimize with a binary decision tree */

  uint32_t out = 0;
  /* 0x001    rs1 is -INF */
  out |= (f == 0xff800000) ? 0x001 : 0;
  /* 0x002    rs1 is negative normal */
  out |= (expn && expn < 0x78000000 && sign) ? 0x002 : 0;
  /* 0x004    rs1 is negative subnormal */
  out |= (!expn && frac && sign) ? 0x004 : 0;
  /* 0x008    rs1 is -0 */
  out |= (f == 0x80000000) ? 0x008 : 0;
  /* 0x010    rs1 is +0 */
  out |= (f == 0x00000000) ? 0x010 : 0;
  /* 0x020    rs1 is positive subnormal */
  out |= (!expn && frac && !sign) ? 0x020 : 0;
  /* 0x040    rs1 is positive normal */
  out |= (expn && expn < 0x78000000 && !sign) ? 0x040 : 0;
  /* 0x080    rs1 is +INF */
  out |= (f == 0x7f800000) ? 0x080 : 0;
  /* 0x100    rs1 is a signaling NaN */
  out |= (expn == FMASK_EXPN && (frac <= 0x7ff) && frac) ? 0x100 : 0;
  /* 0x200    rs1 is a quiet NaN */
  out |= (expn == FMASK_EXPN && (frac >= 0x800)) ? 0x200 : 0;

  return out;
}
#endif

#ifdef ENABLE_RV32C
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
