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
 *                                     translatable, reg-mask)
 */
/* clang-format off */
#define RV_INSN_LIST                                   \
    _(nop, 0, 4, 1, ENC(rs1, rd))                      \
    /* RV32I Base Instruction Set */                   \
    _(lui, 0, 4, 1, ENC(rd))                           \
    _(auipc, 0, 4, 1, ENC(rd))                         \
    _(jal, 1, 4, 1, ENC(rd))                           \
    _(jalr, 1, 4, 1, ENC(rs1, rd))                     \
    _(beq, 1, 4, 1, ENC(rs1, rs2))                     \
    _(bne, 1, 4, 1, ENC(rs1, rs2))                     \
    _(blt, 1, 4, 1, ENC(rs1, rs2))                     \
    _(bge, 1, 4, 1, ENC(rs1, rs2))                     \
    _(bltu, 1, 4, 1, ENC(rs1, rs2))                    \
    _(bgeu, 1, 4, 1, ENC(rs1, rs2))                    \
    _(lb, 0, 4, 1, ENC(rs1, rd))                       \
    _(lh, 0, 4, 1, ENC(rs1, rd))                       \
    _(lw, 0, 4, 1, ENC(rs1, rd))                       \
    _(lbu, 0, 4, 1, ENC(rs1, rd))                      \
    _(lhu, 0, 4, 1, ENC(rs1, rd))                      \
    _(sb, 0, 4, 1, ENC(rs1, rs2))                      \
    _(sh, 0, 4, 1, ENC(rs1, rs2))                      \
    _(sw, 0, 4, 1, ENC(rs1, rs2))                      \
    _(addi, 0, 4, 1, ENC(rs1, rd))                     \
    _(slti, 0, 4, 1, ENC(rs1, rd))                     \
    _(sltiu, 0, 4, 1, ENC(rs1, rd))                    \
    _(xori, 0, 4, 1, ENC(rs1, rd))                     \
    _(ori, 0, 4, 1, ENC(rs1, rd))                      \
    _(andi, 0, 4, 1, ENC(rs1, rd))                     \
    _(slli, 0, 4, 1, ENC(rs1, rd))                     \
    _(srli, 0, 4, 1, ENC(rs1, rd))                     \
    _(srai, 0, 4, 1, ENC(rs1, rd))                     \
    _(add, 0, 4, 1, ENC(rs1, rs2, rd))                 \
    _(sub, 0, 4, 1, ENC(rs1, rs2, rd))                 \
    _(sll, 0, 4, 1, ENC(rs1, rs2, rd))                 \
    _(slt, 0, 4, 1, ENC(rs1, rs2, rd))                 \
    _(sltu, 0, 4, 1, ENC(rs1, rs2, rd))                \
    _(xor, 0, 4, 1, ENC(rs1, rs2, rd))                 \
    _(srl, 0, 4, 1, ENC(rs1, rs2, rd))                 \
    _(sra, 0, 4, 1, ENC(rs1, rs2, rd))                 \
    _(or, 0, 4, 1, ENC(rs1, rs2, rd))                  \
    _(and, 0, 4, 1, ENC(rs1, rs2, rd))                 \
    _(fence, 1, 4, 0, ENC(rs1, rd))                    \
    _(ecall, 1, 4, 1, ENC(rs1, rd))                    \
    _(ebreak, 1, 4, 1, ENC(rs1, rd))                   \
    /* RISC-V Privileged Instruction */                \
    _(wfi, 0, 4, 0, ENC(rs1, rd))                      \
    _(uret, 0, 4, 0, ENC(rs1, rd))                     \
    IIF(RV32_HAS(SYSTEM))(                             \
        _(sret, 1, 4, 0, ENC(rs1, rd))                 \
    )                                                  \
    _(hret, 0, 4, 0, ENC(rs1, rd))                     \
    _(mret, 1, 4, 0, ENC(rs1, rd))                     \
    _(sfencevma, 1, 4, 0, ENC(rs1, rs2, rd))           \
    /* RV32 Zifencei Standard Extension */             \
    IIF(RV32_HAS(Zifencei))(                           \
        _(fencei, 1, 4, 0, ENC(rs1, rd))               \
    )                                                  \
    /* RV32 Zicsr Standard Extension */                \
    IIF(RV32_HAS(Zicsr))(                              \
        _(csrrw, 1, 4, 0, ENC(rs1, rd))                \
        _(csrrs, 0, 4, 0, ENC(rs1, rd))                \
        _(csrrc, 0, 4, 0, ENC(rs1, rd))                \
        _(csrrwi, 0, 4, 0, ENC(rs1, rd))               \
        _(csrrsi, 0, 4, 0, ENC(rs1, rd))               \
        _(csrrci, 0, 4, 0, ENC(rs1, rd))               \
    )                                                  \
    /* RV32 Zba Standard Extension */                  \
    IIF(RV32_HAS(Zba))(                                \
        _(sh1add, 0, 4, 0, ENC(rs1, rs2, rd))          \
        _(sh2add, 0, 4, 0, ENC(rs1, rs2, rd))          \
        _(sh3add, 0, 4, 0, ENC(rs1, rs2, rd))          \
    )                                                  \
    /* RV32 Zbb Standard Extension */                  \
    IIF(RV32_HAS(Zbb))(                                \
        _(andn, 0, 4, 0, ENC(rs1, rs2, rd))            \
        _(orn, 0, 4, 0, ENC(rs1, rs2, rd))             \
        _(xnor, 0, 4, 0, ENC(rs1, rs2, rd))            \
        _(clz, 0, 4, 0, ENC(rs1, rd))                  \
        _(ctz, 0, 4, 0, ENC(rs1, rd))                  \
        _(cpop, 0, 4, 0, ENC(rs1, rd))                 \
        _(max, 0, 4, 0, ENC(rs1, rs2, rd))             \
        _(maxu, 0, 4, 0, ENC(rs1, rs2, rd))            \
        _(min, 0, 4, 0, ENC(rs1, rs2, rd))             \
        _(minu, 0, 4, 0, ENC(rs1, rs2, rd))            \
        _(sextb, 0, 4, 0, ENC(rs1, rd))                \
        _(sexth, 0, 4, 0, ENC(rs1, rd))                \
        _(zexth, 0, 4, 0, ENC(rs1, rd))                \
        _(rol, 0, 4, 0, ENC(rs1, rs2, rd))             \
        _(ror, 0, 4, 0, ENC(rs1, rs2, rd))             \
        _(rori, 0, 4, 0, ENC(rs1, rd))                 \
        _(orcb, 0, 4, 0, ENC(rs1, rd))                 \
        _(rev8, 0, 4, 0, ENC(rs1, rd))                 \
    )                                                  \
     /* RV32 Zbc Standard Extension */                 \
    IIF(RV32_HAS(Zbc))(                                \
        _(clmul, 0, 4, 0, ENC(rs1, rs2, rd))           \
        _(clmulh, 0, 4, 0, ENC(rs1, rs2, rd))          \
        _(clmulr, 0, 4, 0, ENC(rs1, rs2, rd))          \
    )                                                  \
    /* RV32 Zbs Standard Extension */                  \
    IIF(RV32_HAS(Zbs))(                                \
        _(bclr, 0, 4, 0, ENC(rs1, rs2, rd))            \
        _(bclri, 0, 4, 0, ENC(rs1, rs2, rd))           \
        _(bext, 0, 4, 0, ENC(rs1, rs2, rd))            \
        _(bexti, 0, 4, 0, ENC(rs1, rs2, rd))           \
        _(binv, 0, 4, 0, ENC(rs1, rs2, rd))            \
        _(binvi, 0, 4, 0, ENC(rs1, rs2, rd))           \
        _(bset, 0, 4, 0, ENC(rs1, rs2, rd))            \
        _(bseti, 0, 4, 0, ENC(rs1, rs2, rd))           \
    )                                                  \
    /* RV32M Standard Extension */                     \
    IIF(RV32_HAS(EXT_M))(                              \
        _(mul, 0, 4, 1, ENC(rs1, rs2, rd))             \
        _(mulh, 0, 4, 1, ENC(rs1, rs2, rd))            \
        _(mulhsu, 0, 4, 1, ENC(rs1, rs2, rd))          \
        _(mulhu, 0, 4, 1, ENC(rs1, rs2, rd))           \
        _(div, 0, 4, 1, ENC(rs1, rs2, rd))             \
        _(divu, 0, 4, 1, ENC(rs1, rs2, rd))            \
        _(rem, 0, 4, 1, ENC(rs1, rs2, rd))             \
        _(remu, 0, 4, 1, ENC(rs1, rs2, rd))            \
    )                                                  \
    /* RV32A Standard Extension */                     \
    IIF(RV32_HAS(EXT_A))(                              \
        _(lrw, 0, 4, 0, ENC(rs1, rs2, rd))             \
        _(scw, 0, 4, 0, ENC(rs1, rs2, rd))             \
        _(amoswapw, 0, 4, 0, ENC(rs1, rs2, rd))        \
        _(amoaddw, 0, 4, 0, ENC(rs1, rs2, rd))         \
        _(amoxorw, 0, 4, 0, ENC(rs1, rs2, rd))         \
        _(amoandw, 0, 4, 0, ENC(rs1, rs2, rd))         \
        _(amoorw, 0, 4, 0, ENC(rs1, rs2, rd))          \
        _(amominw, 0, 4, 0, ENC(rs1, rs2, rd))         \
        _(amomaxw, 0, 4, 0, ENC(rs1, rs2, rd))         \
        _(amominuw, 0, 4, 0, ENC(rs1, rs2, rd))        \
        _(amomaxuw, 0, 4, 0, ENC(rs1, rs2, rd))        \
    )                                                  \
    /* RV32F Standard Extension */                     \
    IIF(RV32_HAS(EXT_F))(                              \
        _(flw, 0, 4, 0, ENC(rs1, rd))                  \
        _(fsw, 0, 4, 0, ENC(rs1, rs2))                 \
        _(fmadds, 0, 4, 0, ENC(rs1, rs2, rs3, rd))     \
        _(fmsubs, 0, 4, 0, ENC(rs1, rs2, rs3, rd))     \
        _(fnmsubs, 0, 4, 0, ENC(rs1, rs2, rs3, rd))    \
        _(fnmadds, 0, 4, 0, ENC(rs1, rs2, rs3, rd))    \
        _(fadds, 0, 4, 0, ENC(rs1, rs2, rd))           \
        _(fsubs, 0, 4, 0, ENC(rs1, rs2, rd))           \
        _(fmuls, 0, 4, 0, ENC(rs1, rs2, rd))           \
        _(fdivs, 0, 4, 0, ENC(rs1, rs2, rd))           \
        _(fsqrts, 0, 4, 0, ENC(rs1, rs2, rd))          \
        _(fsgnjs, 0, 4, 0, ENC(rs1, rs2, rd))          \
        _(fsgnjns, 0, 4, 0, ENC(rs1, rs2, rd))         \
        _(fsgnjxs, 0, 4, 0, ENC(rs1, rs2, rd))         \
        _(fmins, 0, 4, 0, ENC(rs1, rs2, rd))           \
        _(fmaxs, 0, 4, 0, ENC(rs1, rs2, rd))           \
        _(fcvtws, 0, 4, 0, ENC(rs1, rs2, rd))          \
        _(fcvtwus, 0, 4, 0, ENC(rs1, rs2, rd))         \
        _(fmvxw, 0, 4, 0, ENC(rs1, rs2, rd))           \
        _(feqs, 0, 4, 0, ENC(rs1, rs2, rd))            \
        _(flts, 0, 4, 0, ENC(rs1, rs2, rd))            \
        _(fles, 0, 4, 0, ENC(rs1, rs2, rd))            \
        _(fclasss, 0, 4, 0, ENC(rs1, rs2, rd))         \
        _(fcvtsw, 0, 4, 0, ENC(rs1, rs2, rd))          \
        _(fcvtswu, 0, 4, 0, ENC(rs1, rs2, rd))         \
        _(fmvwx, 0, 4, 0, ENC(rs1, rs2, rd))           \
    )                                                  \
    /* RV32C Standard Extension */                     \
    IIF(RV32_HAS(EXT_C))(                              \
        _(caddi4spn, 0, 2, 1, ENC(rd))                 \
        _(clw, 0, 2, 1, ENC(rs1, rd))                  \
        _(csw, 0, 2, 1, ENC(rs1, rs2))                 \
        _(cnop, 0, 2, 1, ENC())                        \
        _(caddi, 0, 2, 1, ENC(rd))                     \
        _(cjal, 1, 2, 1, ENC())                        \
        _(cli, 0, 2, 1, ENC(rd))                       \
        _(caddi16sp, 0, 2, 1, ENC())                   \
        _(clui, 0, 2, 1, ENC(rd))                      \
        _(csrli, 0, 2, 1, ENC(rs1))                    \
        _(csrai, 0, 2, 1, ENC(rs1))                    \
        _(candi, 0, 2, 1, ENC(rs1))                    \
        _(csub, 0, 2, 1, ENC(rs1, rs2, rd))            \
        _(cxor, 0, 2, 1, ENC(rs1, rs2, rd))            \
        _(cor, 0, 2, 1, ENC(rs1, rs2, rd))             \
        _(cand, 0, 2, 1, ENC(rs1, rs2, rd))            \
        _(cj, 1, 2, 1, ENC())                          \
        _(cbeqz, 1, 2, 1, ENC(rs1))                    \
        _(cbnez, 1, 2, 1, ENC(rs1))                    \
        _(cslli, 0, 2, 1, ENC(rd))                     \
        _(clwsp, 0, 2, 1, ENC(rd))                     \
        _(cjr, 1, 2, 1, ENC(rs1, rs2, rd))             \
        _(cmv, 0, 2, 1, ENC(rs1, rs2, rd))             \
        _(cebreak, 1, 2, 1,ENC(rs1, rs2, rd))          \
        _(cjalr, 1, 2, 1, ENC(rs1, rs2, rd))           \
        _(cadd, 0, 2, 1, ENC(rs1, rs2, rd))            \
        _(cswsp, 0, 2, 1, ENC(rs2))                    \
        /* RV32FC Instruction */                       \
        IIF(RV32_HAS(EXT_F))(                          \
            _(cflwsp, 0, 2, 1, ENC(rd))                \
            _(cfswsp, 0, 2, 1, ENC(rs2))               \
            _(cflw, 0, 2, 1, ENC(rs1, rd))             \
            _(cfsw, 0, 2, 1, ENC(rs1, rs2))            \
        )                                              \
    )                                                  \
    /* Vector Extension */                             \
    IIF(RV32_HAS(EXT_V))(                              \
        /* Configuration-setting Instructions */       \
        _(vsetvli, 0, 4, 0, ENC(rs1, rd))              \
        _(vsetivli, 0, 4, 0, ENC(rs1, rd))             \
        _(vsetvl, 0, 4, 0, ENC(rs1, rd))               \
        /* Vector Load instructions */                 \
        _(vle8_v, 0, 4, 0, ENC(rs1, vd))               \
        _(vle16_v, 0, 4, 0, ENC(rs1, vd))              \
        _(vle32_v, 0, 4, 0, ENC(rs1, vd))              \
        _(vle64_v, 0, 4, 0, ENC(rs1, vd))              \
        _(vlseg2e8_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vlseg3e8_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vlseg4e8_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vlseg5e8_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vlseg6e8_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vlseg7e8_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vlseg8e8_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vlseg2e16_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlseg3e16_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlseg4e16_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlseg5e16_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlseg6e16_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlseg7e16_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlseg8e16_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlseg2e32_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlseg3e32_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlseg4e32_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlseg5e32_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlseg6e32_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlseg7e32_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlseg8e32_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlseg2e64_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlseg3e64_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlseg4e64_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlseg5e64_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlseg6e64_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlseg7e64_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlseg8e64_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vl1re8_v, 0, 4, 0, ENC(rs1, vd))             \
        _(vl2re8_v, 0, 4, 0, ENC(rs1, vd))             \
        _(vl4re8_v, 0, 4, 0, ENC(rs1, vd))             \
        _(vl8re8_v, 0, 4, 0, ENC(rs1, vd))             \
        _(vl1re16_v, 0, 4, 0, ENC(rs1, vd))            \
        _(vl2re16_v, 0, 4, 0, ENC(rs1, vd))            \
        _(vl4re16_v, 0, 4, 0, ENC(rs1, vd))            \
        _(vl8re16_v, 0, 4, 0, ENC(rs1, vd))            \
        _(vl1re32_v, 0, 4, 0, ENC(rs1, vd))            \
        _(vl2re32_v, 0, 4, 0, ENC(rs1, vd))            \
        _(vl4re32_v, 0, 4, 0, ENC(rs1, vd))            \
        _(vl8re32_v, 0, 4, 0, ENC(rs1, vd))            \
        _(vl1re64_v, 0, 4, 0, ENC(rs1, vd))            \
        _(vl2re64_v, 0, 4, 0, ENC(rs1, vd))            \
        _(vl4re64_v, 0, 4, 0, ENC(rs1, vd))            \
        _(vl8re64_v, 0, 4, 0, ENC(rs1, vd))            \
        _(vlm_v, 0, 4, 0, ENC(rs1, vd))                \
        _(vle8ff_v, 0, 4, 0, ENC(rs1, vd))             \
        _(vle16ff_v, 0, 4, 0, ENC(rs1, vd))            \
        _(vle32ff_v, 0, 4, 0, ENC(rs1, vd))            \
        _(vle64ff_v, 0, 4, 0, ENC(rs1, vd))            \
        _(vlseg2e8ff_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlseg3e8ff_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlseg4e8ff_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlseg5e8ff_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlseg6e8ff_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlseg7e8ff_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlseg8e8ff_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlseg2e16ff_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vlseg3e16ff_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vlseg4e16ff_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vlseg5e16ff_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vlseg6e16ff_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vlseg7e16ff_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vlseg8e16ff_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vlseg2e32ff_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vlseg3e32ff_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vlseg4e32ff_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vlseg5e32ff_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vlseg6e32ff_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vlseg7e32ff_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vlseg8e32ff_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vlseg2e64ff_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vlseg3e64ff_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vlseg4e64ff_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vlseg5e64ff_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vlseg6e64ff_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vlseg7e64ff_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vlseg8e64ff_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vluxei8_v, 0, 4, 0, ENC(rs1, vd))            \
        _(vluxei16_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vluxei32_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vluxei64_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vluxseg2ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vluxseg3ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vluxseg4ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vluxseg5ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vluxseg6ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vluxseg7ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vluxseg8ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vluxseg2ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vluxseg3ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vluxseg4ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vluxseg5ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vluxseg6ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vluxseg7ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vluxseg8ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vluxseg2ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vluxseg3ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vluxseg4ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vluxseg5ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vluxseg6ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vluxseg7ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vluxseg8ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vluxseg2ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vluxseg3ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vluxseg4ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vluxseg5ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vluxseg6ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vluxseg7ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vluxseg8ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vlse8_v, 0, 4, 0, ENC(rs1, vd))              \
        _(vlse16_v, 0, 4, 0, ENC(rs1, vd))             \
        _(vlse32_v, 0, 4, 0, ENC(rs1, vd))             \
        _(vlse64_v, 0, 4, 0, ENC(rs1, vd))             \
        _(vlsseg2e8_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlsseg3e8_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlsseg4e8_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlsseg5e8_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlsseg6e8_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlsseg7e8_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlsseg8e8_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vlsseg2e16_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlsseg3e16_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlsseg4e16_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlsseg5e16_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlsseg6e16_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlsseg7e16_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlsseg8e16_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlsseg2e32_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlsseg3e32_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlsseg4e32_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlsseg5e32_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlsseg6e32_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlsseg7e32_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlsseg8e32_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlsseg2e64_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlsseg3e64_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlsseg4e64_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlsseg5e64_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlsseg6e64_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlsseg7e64_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vlsseg8e64_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vloxei8_v, 0, 4, 0, ENC(rs1, vd))            \
        _(vloxei16_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vloxei32_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vloxei64_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vloxseg2ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vloxseg3ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vloxseg4ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vloxseg5ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vloxseg6ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vloxseg7ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vloxseg8ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vloxseg2ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vloxseg3ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vloxseg4ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vloxseg5ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vloxseg6ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vloxseg7ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vloxseg8ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vloxseg2ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vloxseg3ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vloxseg4ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vloxseg5ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vloxseg6ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vloxseg7ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vloxseg8ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vloxseg2ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vloxseg3ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vloxseg4ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vloxseg5ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vloxseg6ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vloxseg7ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vloxseg8ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        /* Vector store instructions */                \
        _(vse8_v, 0, 4, 0, ENC(rs1, vd))               \
        _(vse16_v, 0, 4, 0, ENC(rs1, vd))              \
        _(vse32_v, 0, 4, 0, ENC(rs1, vd))              \
        _(vse64_v, 0, 4, 0, ENC(rs1, vd))              \
        _(vsseg2e8_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vsseg3e8_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vsseg4e8_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vsseg5e8_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vsseg6e8_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vsseg7e8_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vsseg8e8_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vsseg2e16_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vsseg3e16_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vsseg4e16_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vsseg5e16_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vsseg6e16_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vsseg7e16_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vsseg8e16_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vsseg2e32_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vsseg3e32_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vsseg4e32_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vsseg5e32_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vsseg6e32_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vsseg7e32_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vsseg8e32_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vsseg2e64_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vsseg3e64_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vsseg4e64_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vsseg5e64_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vsseg6e64_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vsseg7e64_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vsseg8e64_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vs1r_v, 0, 4, 0, ENC(rs1, vd))               \
        _(vs2r_v, 0, 4, 0, ENC(rs1, vd))               \
        _(vs4r_v, 0, 4, 0, ENC(rs1, vd))               \
        _(vs8r_v, 0, 4, 0, ENC(rs1, vd))               \
        _(vsm_v, 0, 4, 0, ENC(rs1, vd))                \
        _(vsuxei8_v, 0, 4, 0, ENC(rs1, vd))            \
        _(vsuxei16_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vsuxei32_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vsuxei64_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vsuxseg2ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vsuxseg3ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vsuxseg4ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vsuxseg5ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vsuxseg6ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vsuxseg7ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vsuxseg8ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vsuxseg2ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsuxseg3ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsuxseg4ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsuxseg5ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsuxseg6ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsuxseg7ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsuxseg8ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsuxseg2ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsuxseg3ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsuxseg4ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsuxseg5ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsuxseg6ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsuxseg7ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsuxseg8ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsuxseg2ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsuxseg3ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsuxseg4ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsuxseg5ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsuxseg6ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsuxseg7ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsuxseg8ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsse8_v, 0, 4, 0, ENC(rs1, vd))              \
        _(vsse16_v, 0, 4, 0, ENC(rs1, vd))             \
        _(vsse32_v, 0, 4, 0, ENC(rs1, vd))             \
        _(vsse64_v, 0, 4, 0, ENC(rs1, vd))             \
        _(vssseg2e8_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vssseg3e8_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vssseg4e8_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vssseg5e8_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vssseg6e8_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vssseg7e8_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vssseg8e8_v, 0, 4, 0, ENC(rs1, vd))          \
        _(vssseg2e16_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vssseg3e16_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vssseg4e16_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vssseg5e16_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vssseg6e16_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vssseg7e16_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vssseg8e16_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vssseg2e32_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vssseg3e32_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vssseg4e32_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vssseg5e32_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vssseg6e32_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vssseg7e32_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vssseg8e32_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vssseg2e64_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vssseg3e64_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vssseg4e64_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vssseg5e64_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vssseg6e64_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vssseg7e64_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vssseg8e64_v, 0, 4, 0, ENC(rs1, vd))         \
        _(vsoxei8_v, 0, 4, 0, ENC(rs1, vd))            \
        _(vsoxei16_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vsoxei32_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vsoxei64_v, 0, 4, 0, ENC(rs1, vd))           \
        _(vsoxseg2ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vsoxseg3ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vsoxseg4ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vsoxseg5ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vsoxseg6ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vsoxseg7ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vsoxseg8ei8_v, 0, 4, 0, ENC(rs1, vd))        \
        _(vsoxseg2ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsoxseg3ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsoxseg4ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsoxseg5ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsoxseg6ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsoxseg7ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsoxseg8ei16_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsoxseg2ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsoxseg3ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsoxseg4ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsoxseg5ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsoxseg6ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsoxseg7ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsoxseg8ei32_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsoxseg2ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsoxseg3ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsoxseg4ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsoxseg5ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsoxseg6ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsoxseg7ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        _(vsoxseg8ei64_v, 0, 4, 0, ENC(rs1, vd))       \
        /* Vector Arithmetic instructions */           \
        /* OPI */                                      \
        _(vadd_vv, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vadd_vx, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vadd_vi, 0, 4, 0, ENC(rs2, rd))              \
        _(vsub_vv, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vsub_vx, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vrsub_vx, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vrsub_vi, 0, 4, 0, ENC(rs2, rd))             \
        _(vminu_vv, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vminu_vx, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vmin_vv, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vmin_vx, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vmaxu_vv, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vmaxu_vx, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vmax_vv, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vmax_vx, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vand_vv, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vand_vx, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vand_vi, 0, 4, 0, ENC(rs2, rd))              \
        _(vor_vv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vor_vx, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vor_vi, 0, 4, 0, ENC(rs2, rd))               \
        _(vxor_vv, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vxor_vx, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vxor_vi, 0, 4, 0, ENC(rs2, rd))              \
        _(vrgather_vv, 0, 4, 0, ENC(rs1, rs2, vd))     \
        _(vrgather_vx, 0, 4, 0, ENC(rs1, rs2, vd))     \
        _(vrgather_vi, 0, 4, 0, ENC(rs2, rd))          \
        _(vslideup_vx, 0, 4, 0, ENC(rs1, rs2, vd))     \
        _(vslideup_vi, 0, 4, 0, ENC(rs2, rd))          \
        _(vrgatherei16_vv, 0, 4, 0, ENC(rs1, rs2, vd)) \
        _(vslidedown_vx, 0, 4, 0, ENC(rs1, rs2, vd))   \
        _(vslidedown_vi, 0, 4, 0, ENC(rs2, rd))        \
        _(vadc_vvm, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vadc_vxm, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vadc_vim, 0, 4, 0, ENC(rs2, rd))              \
        _(vmadc_vv, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vmadc_vx, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vmadc_vi, 0, 4, 0, ENC(rs2, rd))             \
        _(vsbc_vvm, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vsbc_vxm, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vmsbc_vv, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vmsbc_vx, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vmerge_vvm, 0, 4, 0, ENC(rs1, rs2, vd))       \
        _(vmerge_vxm, 0, 4, 0, ENC(rs1, rs2, vd))       \
        _(vmerge_vim, 0, 4, 0, ENC(rs2, rd))            \
        _(vmv_v_v, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vmv_v_x, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vmv_v_i, 0, 4, 0, ENC(rs2, rd))               \
        _(vmseq_vv, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vmseq_vx, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vmseq_vi, 0, 4, 0, ENC(rs2, rd))             \
        _(vmsne_vv, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vmsne_vx, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vmsne_vi, 0, 4, 0, ENC(rs2, rd))             \
        _(vmsltu_vv, 0, 4, 0, ENC(rs1, rs2, vd))       \
        _(vmsltu_vx, 0, 4, 0, ENC(rs1, rs2, vd))       \
        _(vmslt_vv, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vmslt_vx, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vmsleu_vv, 0, 4, 0, ENC(rs1, rs2, vd))       \
        _(vmsleu_vx, 0, 4, 0, ENC(rs1, rs2, vd))       \
        _(vmsleu_vi, 0, 4, 0, ENC(rs2, rd))            \
        _(vmsle_vv, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vmsle_vx, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vmsle_vi, 0, 4, 0, ENC(rs2, rd))             \
        _(vmsgtu_vx, 0, 4, 0, ENC(rs1, rs2, vd))       \
        _(vmsgtu_vi, 0, 4, 0, ENC(rs2, rd))            \
        _(vmsgt_vx, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vmsgt_vi, 0, 4, 0, ENC(rs2, rd))             \
        _(vsaddu_vv, 0, 4, 0, ENC(rs1, rs2, vd))       \
        _(vsaddu_vx, 0, 4, 0, ENC(rs1, rs2, vd))       \
        _(vsaddu_vi, 0, 4, 0, ENC(rs2, rd))            \
        _(vsadd_vv, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vsadd_vx, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vsadd_vi, 0, 4, 0, ENC(rs2, rd))             \
        _(vssubu_vv, 0, 4, 0, ENC(rs1, rs2, vd))       \
        _(vssubu_vx, 0, 4, 0, ENC(rs1, rs2, vd))       \
        _(vssub_vv, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vssub_vx, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vsll_vv, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vsll_vx, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vsll_vi, 0, 4, 0, ENC(rs2, rd))              \
        _(vsmul_vv, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vsmul_vx, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vsrl_vv, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vsrl_vx, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vsrl_vi, 0, 4, 0, ENC(rs2, rd))              \
        _(vsra_vv, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vsra_vx, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vsra_vi, 0, 4, 0, ENC(rs2, rd))              \
        _(vssrl_vv, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vssrl_vx, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vssrl_vi, 0, 4, 0, ENC(rs2, rd))             \
        _(vssra_vv, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vssra_vx, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vssra_vi, 0, 4, 0, ENC(rs2, rd))             \
        _(vnsrl_wv, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vnsrl_wx, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vnsrl_wi, 0, 4, 0, ENC(rs2, rd))             \
        _(vnsra_wv, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vnsra_wx, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vnsra_wi, 0, 4, 0, ENC(rs2, rd))             \
        _(vnclipu_wv, 0, 4, 0, ENC(rs1, rs2, vd))      \
        _(vnclipu_wx, 0, 4, 0, ENC(rs1, rs2, vd))      \
        _(vnclipu_wi, 0, 4, 0, ENC(rs2, rd))           \
        _(vnclip_wv, 0, 4, 0, ENC(rs1, rs2, vd))       \
        _(vnclip_wx, 0, 4, 0, ENC(rs1, rs2, vd))       \
        _(vnclip_wi, 0, 4, 0, ENC(rs2, rd))            \
        _(vwredsumu_vs, 0, 4, 0, ENC(rs1, rs2, vd))    \
        _(vwredsum_vs, 0, 4, 0, ENC(rs1, rs2, vd))     \
        /* OPM */                                         \
        _(vredsum_vs, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vredand_vs, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vredor_vs, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vredxor_vs, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vredminu_vs, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vredmin_vs, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vredmaxu_vs, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vredmax_vs, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vaaddu_vv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vaaddu_vx, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vaadd_vv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vaadd_vx, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vasubu_vv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vasubu_vx, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vasub_vv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vasub_vx, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vslide1up_vx, 0, 4, 0, ENC(rs1, rs2, vd))       \
        _(vslide1down_vx, 0, 4, 0, ENC(rs1, rs2, vd))     \
        _(vcompress_vm, 0, 4, 0, ENC(rs1, rs2, vd))       \
        _(vmandn_mm, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vmand_mm, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vmor_mm, 0, 4, 0, ENC(rs1, rs2, vd))            \
        _(vmxor_mm, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vmorn_mm, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vmnand_mm, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vmnor_mm, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vmxnor_mm, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vdivu_vv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vdivu_vx, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vdiv_vv, 0, 4, 0, ENC(rs1, rs2, vd))            \
        _(vdiv_vx, 0, 4, 0, ENC(rs1, rs2, vd))            \
        _(vremu_vv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vremu_vx, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vrem_vv, 0, 4, 0, ENC(rs1, rs2, vd))            \
        _(vrem_vx, 0, 4, 0, ENC(rs1, rs2, vd))            \
        _(vmulhu_vv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vmulhu_vx, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vmul_vv, 0, 4, 0, ENC(rs1, rs2, vd))            \
        _(vmul_vx, 0, 4, 0, ENC(rs1, rs2, vd))            \
        _(vmulhsu_vv, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vmulhsu_vx, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vmulh_vv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vmulh_vx, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vmadd_vv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vmadd_vx, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vnmsub_vv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vnmsub_vx, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vmacc_vv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vmacc_vx, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vnmsac_vv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vnmsac_vx, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vwaddu_vv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vwaddu_vx, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vwadd_vv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vwadd_vx, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vwsubu_vv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vwsubu_vx, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vwsub_vv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vwsub_vx, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vwaddu_wv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vwaddu_wx, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vwadd_wv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vwadd_wx, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vwsubu_wv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vwsubu_wx, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vwsub_wv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vwsub_wx, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vwmulu_vv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vwmulu_vx, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vwmulsu_vv, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vwmulsu_vx, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vwmul_vv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vwmul_vx, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vwmaccu_vv, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vwmaccu_vx, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vwmacc_vv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vwmacc_vx, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vwmaccus_vx, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vwmaccsu_vv, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vwmaccsu_vx, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vmv_s_x, 0, 4, 0, ENC(rs1, rs2, vd))            \
        _(vmv_x_s, 0, 4, 0, ENC(rs1, rs2, vd))            \
        _(vcpop_m, 0, 4, 0, ENC(rs1, rs2, vd))            \
        _(vfirst_m, 0, 4, 0, ENC(rs1, rs2, vd))            \
        _(vmsbf_m, 0, 4, 0, ENC(rs1, rs2, vd))            \
        _(vmsof_m, 0, 4, 0, ENC(rs1, rs2, vd))            \
        _(vmsif_m, 0, 4, 0, ENC(rs1, rs2, vd))            \
        _(viota_m, 0, 4, 0, ENC(rs1, rs2, vd))            \
        _(vid_v, 0, 4, 0, ENC(rs1, rs2, vd))            \
        /* OPF */                                         \
        _(vfadd_vv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vfadd_vf, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vfredusum_vs, 0, 4, 0, ENC(rs1, rs2, vd))       \
        _(vfsub_vv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vfsub_vf, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vfredosum_vs, 0, 4, 0, ENC(rs1, rs2, vd))       \
        _(vfmin_vv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vfmin_vf, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vfredmin_vs, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vfmax_vv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vfmax_vf, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vfredmax_vs, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vfsgnj_vv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfsgnj_vf, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfsgnjn_vv, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vfsgnjn_vf, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vfsgnjx_vv, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vfsgnjx_vf, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vfslide1up_vf, 0, 4, 0, ENC(rs1, rs2, vd))      \
        _(vfslide1down_vf, 0, 4, 0, ENC(rs1, rs2, vd))    \
        _(vfmerge_vfm, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vfmv_v_f, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vmfeq_vv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vmfeq_vf, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vmfle_vv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vmfle_vf, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vmflt_vv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vmflt_vf, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vmfne_vv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vmfne_vf, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vmfgt_vf, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vmfge_vf, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vfdiv_vv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vfdiv_vf, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vfrdiv_vf, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfmul_vv, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vfmul_vf, 0, 4, 0, ENC(rs1, rs2, vd))           \
        _(vfrsub_vf, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfmadd_vv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfmadd_vf, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfnmadd_vv, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vfnmadd_vf, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vfmsub_vv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfmsub_vf, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfnmsub_vv, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vfnmsub_vf, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vfmacc_vv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfmacc_vf, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfnmacc_vv, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vfnmacc_vf, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vfmsac_vv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfmsac_vf, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfnmsac_vv, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vfnmsac_vf, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vfwadd_vv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfwadd_vf, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfwredusum_vs, 0, 4, 0, ENC(rs1, rs2, vd))      \
        _(vfwsub_vv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfwsub_vf, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfwredosum_vs, 0, 4, 0, ENC(rs1, rs2, vd))      \
        _(vfwadd_wv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfwadd_wf, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfwsub_wv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfwsub_wf, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfwmul_vv, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfwmul_vf, 0, 4, 0, ENC(rs1, rs2, vd))          \
        _(vfwmacc_vv, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vfwmacc_vf, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vfwnmacc_vv, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vfwnmacc_vf, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vfwmsac_vv, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vfwmsac_vf, 0, 4, 0, ENC(rs1, rs2, vd))         \
        _(vfwnmsac_vv, 0, 4, 0, ENC(rs1, rs2, vd))        \
        _(vfwnmsac_vf, 0, 4, 0, ENC(rs1, rs2, vd))        \
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
    _(fuse5)

/* clang-format off */
/* IR (intermediate representation) is exclusively represented by RISC-V
 * instructions, yet it is executed at a lower cost.
 */
enum {
#define _(inst, can_branch, insn_len, translatable, reg_mask) rv_insn_##inst,
    RV_INSN_LIST
#undef _
    N_RV_INSNS,
#define _(inst) rv_insn_##inst,
    FUSE_INSN_LIST
#undef _
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
    FC_OPCODE    = 0b00000000000000000000000000000011, // compressed-instruction
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
    FV_ZIMM_30_20 = 0b01111111111100000000000000000000,
    FV_ZIMM_29_20 = 0b00111111111100000000000000000000,
    FV_VM        = 0b00000010000000000000000000000000,
    FV_MOP       = 0b00001100000000000000000000000000,
    FV_MEW       = 0b00010000000000000000000000000000,
    FV_NF        = 0b11100000000000000000000000000000,
    FV_14_12     = 0b00000000000000000111000000000000,
    FV_24_20     = 0b00000001111100000000000000000000,
    FV_FUNC6     = 0b11111100000000000000000000000000,
};
/* clang-format on */

typedef struct {
    int32_t imm;
    uint8_t rd, rs1, rs2;
    uint8_t opcode;
} opcode_fuse_t;

#define HISTORY_SIZE 16
typedef struct {
    uint32_t PC[HISTORY_SIZE];
#if !RV32_HAS(JIT)
    uint8_t idx;
    struct rv_insn *target[HISTORY_SIZE];
#else
    uint32_t times[HISTORY_SIZE];
#if RV32_HAS(SYSTEM)
    uint32_t satp[HISTORY_SIZE];
#endif
#endif
} branch_history_table_t;

typedef struct rv_insn {
    union {
        int32_t imm;
        uint8_t rs3;
    };
    uint8_t rd, rs1, rs2;
    /* store IR list */
    uint16_t opcode;

#if RV32_HAS(EXT_C)
    uint8_t shamt;
#endif

#if RV32_HAS(EXT_V)
    int32_t zimm;

    uint8_t vd, vs1, vs2, vs3, eew, vm;
#endif

#if RV32_HAS(EXT_F)
    /* Floating-point operations use either a static rounding mode encoded in
     * the instruction, or a dynamic rounding mode held in frm. A value of 111
     * in the instruction’s rm field selects the dynamic rounding mode held in
     * frm. If frm is set to an invalid value (101–111), any subsequent attempt
     * to execute a floating-point operation with a dynamic rounding mode will
     * cause an illegal instruction trap. Some instructions that have the rm
     * field are nevertheless unaffected by the rounding mode; they should have
     * their rm field set to RNE (000).
     */
    uint8_t rm;
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
     * The @next member indicates the next IR or is NULL if it is the final
     * instruction in a basic block. The @impl member facilitates the direct
     * invocation of the next instruction emulation without the need to compute
     * the jump address. By utilizing these two members, all instruction
     * emulations can be rewritten into a self-recursive version, enabling the
     * compiler to leverage TCO.
     */
    struct rv_insn *next;
    bool (*impl)(riscv_t *, const struct rv_insn *, uint64_t, uint32_t);

    /* Two pointers, 'branch_taken' and 'branch_untaken', are employed to
     * avoid the overhead associated with aggressive memory copying. Instead
     * of copying the entire IR array, these pointers indicate the first IR
     * of the first basic block in the path of the taken and untaken branches.
     * This allows for direct jumping to the specific IR array without the
     * need for additional copying.
     */
    struct rv_insn *branch_taken, *branch_untaken;
    branch_history_table_t *branch_table;
} rv_insn_t;

/* decode the RISC-V instruction */
bool rv_decode(rv_insn_t *ir, const uint32_t insn);
