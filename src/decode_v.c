/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* RISC-V "V" Vector Extension - decoders.
 *
 * Included from decode.c when RV32_HAS(EXT_V) is set. Defines V-extension
 * decode helpers, the V load/store dispatchers extracted from op_load_fp
 * and op_store_fp, and all V instruction decode functions + op_vcfg.
 * Not a standalone translation unit; expects RV_INSN_LIST enum, FV_*
 * field masks, and the surrounding decode_* helpers to be in scope.
 */

/* === Decode helpers === */

/* Sign-extened vector immediate */
static inline int32_t decode_v_imm(const uint32_t insn)
{
    return ((int32_t) ((insn << 12) & FR4_RS3)) >> 27;
}

/* decode vsetvli zimm[10:0] field
 * zimm = inst[30:20]
 */
static inline uint32_t decode_vsetvli_zimm(const uint32_t insn)
{
    return (insn & FV_ZIMM_30_20) >> 20;
}

/* decode vsetivli zimm[9:0] field
 * zimm = inst[29:20]
 */
static inline uint32_t decode_vsetivli_zimm(const uint32_t insn)
{
    return (insn & FV_ZIMM_29_20) >> 20;
}

/* Decode bit 30, used to distinguish vsetvl (0) from vsetivli (1) once
 * bit 31 has been checked in op_vcfg.
 */
static inline uint8_t decode_30(const uint32_t insn)
{
    return (insn & 0x40000000) >> 30;
}

/* decode vector mask field
 * vm = insn[25]
 */
static inline uint8_t decode_vm(const uint32_t insn)
{
    return (insn & FV_VM) >> 25;
}

/* decode mop field
 * mop = insn[27:26]
 */
static inline uint8_t decode_mop(const uint32_t insn)
{
    return (insn & FV_MOP) >> 26;
}

/* decode eew(width) field
 * eew(width) = insn[14:12]
 */
static inline uint8_t decode_eew(const uint32_t insn)
{
    switch ((insn & FV_14_12) >> 12) {
    case 0b000:
        return 0;
    case 0b101:
        return 1;
    case 0b110:
        return 2;
    case 0b111:
        return 3;
    default:
        __UNREACHABLE;
        break;
    }
}

/* decode nf field
 * nf = insn[31:29]
 */
static inline uint8_t decode_nf(const uint32_t insn)
{
    return (insn & FV_NF) >> 29;
}

/* decode lumop/sumop field
 * lumop/sumop = insn[24:20]
 */
static inline uint8_t decode_24_20(const uint32_t insn)
{
    return ((int32_t) (insn & FV_24_20)) >> 20;
}

/* decode VL* unit-stride
 *  31  29  28   27 26  25  24   20 19   15 14   12 11 7 6       0
 * |  nf  | mew | mop | vm | lumop |  rs1  | width | vd | 0000111 |
 */
static inline void decode_VL(rv_insn_t *ir, const uint32_t insn)
{
    ir->rs1 = decode_rs1(insn);
    ir->vd = decode_rd(insn);
    ir->vm = decode_vm(insn);
}

/* decode VLS* strided
 *  31  29  28   27 26  25  24   20 19   15 14   12 11 7 6       0
 * |  nf  | mew | mop | vm |  rs2  |  rs1  | width | vd | 0000111 |
 */
static inline void decode_VLS(rv_insn_t *ir, const uint32_t insn)
{
    ir->rs2 = decode_rs2(insn);
    ir->rs1 = decode_rs1(insn);
    ir->vd = decode_rd(insn);
    ir->vm = decode_vm(insn);
}

/* decode VLS* strided
 *  31  29  28   27 26  25  24   20 19   15 14   12 11 7 6       0
 * |  nf  | mew | mop | vm |  vs2  |  rs1  | width | vd | 0000111 |
 */
static inline void decode_VLX(rv_insn_t *ir, const uint32_t insn)
{
    ir->vs2 = decode_rs2(insn);
    ir->rs1 = decode_rs1(insn);
    ir->vd = decode_rd(insn);
    ir->vm = decode_vm(insn);
}

/* decode VS* unit-stride
 *  31  29  28   27 26  25  24   20 19   15 14   12 11  7 6       0
 * |  nf  | mew | mop | vm | sumop |  rs1  | width | vs3 | 0100111 |
 */
static inline void decode_VS(rv_insn_t *ir, const uint32_t insn)
{
    ir->rs1 = decode_rs1(insn);
    ir->vs3 = decode_rd(insn);
    ir->vm = decode_vm(insn);
}

/* decode VSS* strided
 *  31  29  28   27 26  25  24   20 19   15 14   12 11  7 6       0
 * |  nf  | mew | mop | vm |  rs2  |  rs1  | width | vs3 | 0100111 |
 */
static inline void decode_VSS(rv_insn_t *ir, const uint32_t insn)
{
    ir->rs2 = decode_rs2(insn);
    ir->rs1 = decode_rs1(insn);
    ir->vs3 = decode_rd(insn);
    ir->vm = decode_vm(insn);
}

/* decode VLS* strided
 *  31  29  28   27 26  25  24   20 19   15 14   12 11  7 6       0
 * |  nf  | mew | mop | vm |  vs2  |  rs1  | width | vs3 | 0100111 |
 */
static inline void decode_VSX(rv_insn_t *ir, const uint32_t insn)
{
    ir->vs2 = decode_rs2(insn);
    ir->rs1 = decode_rs1(insn);
    ir->vs3 = decode_rd(insn);
    ir->vm = decode_vm(insn);
}

/* === V load / store dispatch (was inline in op_load_fp/op_store_fp) === */

/* Decode a vector load. Returns true on success, false on illegal
 * encoding. Caller gates on funct3 != 0b010 before calling.
 */
static inline bool decode_v_load(rv_insn_t *ir, const uint32_t insn)
{
    uint8_t eew = decode_eew(insn);
    ir->eew = 8 << eew;
    uint8_t nf = decode_nf(insn);
    switch (decode_mop(insn)) {
    case 0:
        decode_VL(ir, insn);
        /* check lumop */
        switch (decode_24_20(insn)) {
        case 0b00000:
            if (!nf) {
                ir->opcode = rv_insn_vle8_v + eew;
            } else {
                ir->opcode = rv_insn_vlseg2e8_v + 7 * eew + nf - 1;
            }
            break;
        case 0b01000:
            ir->opcode = rv_insn_vl1re8_v + 4 * eew + ilog2(nf + 1);
            break;
        case 0b01011:
            ir->opcode = rv_insn_vlm_v;
            break;
        case 0b10000:
            if (!nf) {
                ir->opcode = rv_insn_vle8ff_v + eew;
            } else {
                ir->opcode = rv_insn_vlseg2e8ff_v + 7 * eew + nf - 1;
            }
            break;
        default:
            return false;
        }
        break;
    case 1:
        decode_VLX(ir, insn);
        if (!nf) {
            ir->opcode = rv_insn_vluxei8_v + eew;
        } else {
            ir->opcode = rv_insn_vluxseg2ei8_v + 7 * eew + nf - 1;
        }
        break;
    case 2:
        decode_VLS(ir, insn);
        if (!nf) {
            ir->opcode = rv_insn_vlse8_v + eew;
        } else {
            ir->opcode = rv_insn_vlsseg2e8_v + 7 * eew + nf - 1;
        }
        break;
    case 3:
        decode_VLX(ir, insn);
        if (!nf) {
            ir->opcode = rv_insn_vloxei8_v + eew;
        } else {
            ir->opcode = rv_insn_vloxseg2ei8_v + 7 * eew + nf - 1;
        }
        break;
    default:
        return false;
    }
    return true;
}

/* Decode a vector store. Same contract as decode_v_load. */
static inline bool decode_v_store(rv_insn_t *ir, const uint32_t insn)
{
    uint8_t eew = decode_eew(insn);
    ir->eew = 8 << eew;
    uint8_t nf = decode_nf(insn);
    switch (decode_mop(insn)) {
    case 0:
        decode_VS(ir, insn);
        /* check sumop */
        switch (decode_24_20(insn)) {
        case 0b00000:
            if (!nf) {
                ir->opcode = rv_insn_vse8_v + eew;
            } else {
                ir->opcode = rv_insn_vsseg2e8_v + 7 * eew + nf - 1;
            }
            break;
        case 0b01000:
            ir->opcode = rv_insn_vs1r_v + ilog2(nf + 1);
            break;
        case 0b01011:
            ir->opcode = rv_insn_vsm_v;
            break;
        default:
            return false;
        }
        break;
    case 1:
        decode_VSX(ir, insn);
        if (!nf) {
            ir->opcode = rv_insn_vsuxei8_v + eew;
        } else {
            ir->opcode = rv_insn_vsuxseg2ei8_v + 7 * eew + nf - 1;
        }
        break;
    case 2:
        decode_VSS(ir, insn);
        if (!nf) {
            ir->opcode = rv_insn_vsse8_v + eew;
        } else {
            ir->opcode = rv_insn_vssseg2e8_v + 7 * eew + nf - 1;
        }
        break;
    case 3:
        decode_VSX(ir, insn);
        if (!nf) {
            ir->opcode = rv_insn_vsoxei8_v + eew;
        } else {
            ir->opcode = rv_insn_vsoxseg2ei8_v + 7 * eew + nf - 1;
        }
        break;
    default:
        return false;
    }
    return true;
}

/* === V decode functions and op_vcfg === */

/* decode vsetvli
 *  31  30        20 19   15 14  12 11   7 6      0
 * | 0 | zimm[11:0] |  rs1  | 111  |  rd  | opcode |
 */
static inline void decode_vsetvli(rv_insn_t *ir, const uint32_t insn)
{
    ir->zimm = decode_vsetvli_zimm(insn);
    ir->rs1 = decode_rs1(insn);
    ir->rd = decode_rd(insn);
}

/* decode vsetivli
 *  31  30 29        20 19    15 14  12 11   7 6      0
 * | 1 |1 | zimm[11:0] |  rs1   | 111  |  rd  | opcode |
 */
static inline void decode_vsetivli(rv_insn_t *ir, const uint32_t insn)
{
    ir->zimm = decode_vsetivli_zimm(insn);
    ir->rs1 = decode_rs1(insn);
    ir->rd = decode_rd(insn);
}

/* decode vsetvl
 *  31  30    25 24  20 19   15 14  12 11   7 6      0
 * | 1 | 000000 | rs2  |  rs1  | 111  |  rd  | opcode |
 */
static inline void decode_vsetvl(rv_insn_t *ir, const uint32_t insn)
{
    ir->rs2 = decode_rs2(insn);
    ir->rs1 = decode_rs1(insn);
    ir->rd = decode_rd(insn);
}

/* decode vector-vector operation
 *  31    26  25  24   20 19   15 14    12 11 7 6       0
 * | funct6 | vm |  vs2  |  vs1  | funct3 | vd | 1010111 |
 */
static inline void decode_vvtype(rv_insn_t *ir, const uint32_t insn)
{
    ir->vs2 = decode_rs2(insn);
    ir->vs1 = decode_rs1(insn);
    ir->vd = decode_rd(insn);
    ir->vm = decode_vm(insn);
}

/* decode vector-immediate operation
 *  31    26  25  24   20 19   15 14    12 11 7 6       0
 * | funct6 | vm |  vs2  |  imm  | funct3 | vd | 1010111 |
 */
static inline void decode_vitype(rv_insn_t *ir, const uint32_t insn)
{
    ir->vs2 = decode_rs2(insn);
    ir->imm = decode_v_imm(insn);
    ir->vd = decode_rd(insn);
    ir->vm = decode_vm(insn);
}

/* decode vector-scalar operation
 *  31    26  25  24   20 19   15 14    12 11 7 6       0
 * | funct6 | vm |  vs2  |  rs1  | funct3 | vd | 1010111 |
 */
static inline void decode_vxtype(rv_insn_t *ir, const uint32_t insn)
{
    ir->vs2 = decode_rs2(insn);
    ir->rs1 = decode_rs1(insn);
    ir->vd = decode_rd(insn);
    ir->vm = decode_vm(insn);
}

/* decode vector mask instructions with single vector operand
 *  31    26  25  24   20 19   15 14    12 11 7 6       0
 * | funct6 | vm |  vs2  |  rs1  | funct3 | rd | 1010111 |
 */
static inline void decode_mtype(rv_insn_t *ir, const uint32_t insn)
{
    ir->vs2 = decode_rs2(insn);
    ir->rd = decode_rd(insn);
    ir->vm = decode_vm(insn);
}

static inline bool op_vcfg(rv_insn_t *ir, const uint32_t insn)
{
    switch ((insn >> 31) & 1) {
    case 0: /* vsetvli */
        decode_vsetvli(ir, insn);
        ir->opcode = rv_insn_vsetvli;
        break;
    case 1:
        switch (decode_30(insn)) {
        case 0: /* vsetvl */
            decode_vsetvl(ir, insn);
            ir->opcode = rv_insn_vsetvl;
            break;
        case 1: /* vsetivli */
            decode_vsetivli(ir, insn);
            ir->opcode = rv_insn_vsetivli;
            break;
        default: /* illegal instruction */
            return false;
        }
        break;

    default: /* illegal instruction */
        return false;
    }
    return true;
}

/*
 * Vector instructions under opcode 1010111 are decoded using funct6 (bits
 * 31-26). A new jump table rvv_jump_table is introduced, similar to
 * rv_jump_table, but indexed by funct6 to determine the specific vector
 * operation. The naming convention follows op_funct6, where funct6 is directly
 * appended after op_ (e.g., op_000000).
 */
static inline bool op_000000(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vadd_vv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfadd_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vredsum_vs;
        break;
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vadd_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vadd_vx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfadd_vf;
        break;
    case 6:  /* Reserved */
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_000001(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0: /* Reserved */
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfredusum_vs;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vredand_vs;
        break;
    case 3:  /* Reserved */
    case 4:  /* Reserved */
    case 5:  /* Reserved */
    case 6:  /* Reserved */
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_000010(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vsub_vv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfsub_vv;
        break;
    case 2: /* Reserved */
    case 3:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vredor_vs;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vsub_vx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfsub_vf;
        break;
    case 6:  /* Reserved */
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_000011(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0: /* Reserved */
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfredosum_vs;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vredxor_vs;
        break;
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vrsub_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vrsub_vx;
        break;
    case 5:  /* Reserved */
    case 6:  /* Reserved */
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_000100(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vminu_vv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfmin_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vredminu_vs;
        break;
    case 3:
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vminu_vx;
        break;
    case 5: /* Reserved */
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfmin_vf;
        break;
    case 6:  /* Reserved */
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_000101(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmin_vv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfredmin_vs;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vredmin_vs;
        break;
    case 3:
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmin_vx;
        break;
    case 5:  /* Reserved */
    case 6:  /* Reserved */
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_000110(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmaxu_vv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfmax_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vredmaxu_vs;
        break;
    case 3:
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmaxu_vx;
        break;
    case 5: /* Reserved */
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfmax_vf;
        break;
    case 6:  /* Reserved */
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_000111(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmax_vv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfredmax_vs;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vredmax_vs;
        break;
    case 3:
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmax_vx;
        break;
    case 5:  /* Reserved */
    case 6:  /* Reserved */
    default: /* illegal instruction */
        return false;
    }
    return true;
}


static inline bool op_001000(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfsgnj_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vaaddu_vv;
        break;
    case 3:
    case 4:
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfsgnj_vf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vaaddu_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_001001(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vand_vv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfsgnjn_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vaadd_vv;
        break;
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vand_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vand_vx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfsgnjn_vf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vaadd_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_001010(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vor_vv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfsgnjx_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vasubu_vv;
        break;
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vor_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vor_vx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfsgnjx_vf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vasubu_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_001011(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vxor_vv;
        break;
    case 1:
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vasub_vv;
        break;
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vxor_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vxor_vx;
        break;
    case 5:
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vasub_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_001100(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vrgather_vv;
        break;
    case 1:
    case 2:
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vrgather_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vrgather_vx;
        break;
    case 5:
    case 6:
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_001110(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vrgatherei16_vv;
        break;
    case 1:
    case 2:
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vslideup_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vslideup_vx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfslide1up_vf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vslide1up_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_001111(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
    case 1:
    case 2:
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vslidedown_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vslidedown_vx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfslide1down_vf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vslide1down_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_010000(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->vm = 0;
        ir->opcode = rv_insn_vadc_vvm;
        break;
    case 1:
        /* FIXME: Implement the decoding for VWFUNARY0. */
    case 2:
        /* VWXUNARY0 */
        switch (decode_rs1(insn)) {
        case 0:
            decode_mtype(ir, insn);
            ir->opcode = rv_insn_vmv_x_s;
            break;
        case 0b10000:
            decode_mtype(ir, insn);
            ir->opcode = rv_insn_vcpop_m;
            break;
        case 0b10001:
            decode_mtype(ir, insn);
            ir->opcode = rv_insn_vfirst_m;
            break;
        default:
            return false;
        }
        break;
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vadc_vim;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vadc_vxm;
        break;
    case 5:
        /* FIXME: Implement the decoding for VRFUNARY0. */
    case 6:
        /* VRXUNARY0 */
        ir->rd = decode_rd(insn);
        ir->vs2 = decode_rs2(insn);
        ir->vm = 1;
        ir->opcode = rv_insn_vmv_s_x;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_010001(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmadc_vv;
        break;
    case 1:
    case 2:
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vmadc_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmadc_vx;
        break;
    case 5:
    case 6:
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_010010(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vsbc_vvm;
        break;
    case 1:
        /* FIXME: Implement the decoding for VFUNARY0. */
        break;
    case 2:
        /* FIXME: Implement the decoding for VXUNARY0. */
    case 3:
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vsbc_vxm;
        break;
    case 5:
    case 6:
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_010011(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmsbc_vv;
        break;
    case 1:
        /* FIXME: Implement the decoding for VFUNARY1. */
    case 2:
    case 3:
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmsbc_vx;
        break;
    case 5:
    case 6:
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_010100(rv_insn_t *ir, const uint32_t insn)
{
    /* VMUNARY0 */
    switch (decode_rs1(insn)) {
    case 0b00001:
        decode_mtype(ir, insn);
        ir->opcode = rv_insn_vmsbf_m;
        break;
    case 0b00010:
        decode_mtype(ir, insn);
        ir->opcode = rv_insn_vmsof_m;
        break;
    case 0b00011:
        decode_mtype(ir, insn);
        ir->opcode = rv_insn_vmsif_m;
        break;
    case 0b10000:
        decode_mtype(ir, insn);
        ir->opcode = rv_insn_viota_m;
        break;
    case 0b10001:
        ir->vd = decode_rd(insn);
        ir->vm = 1;
        ir->opcode = rv_insn_vid_v;
        break;
    default:
        return false;
    }
    return true;
}

static inline bool op_010111(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        if (decode_vm(insn)) {
            ir->vm = 1;
            ir->opcode = rv_insn_vmv_v_v;
        } else {
            ir->vm = 0;
            ir->opcode = rv_insn_vmerge_vvm;
        }
        break;
    case 1:
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vcompress_vm;
        break;
    case 3:
        decode_vitype(ir, insn);
        if (decode_vm(insn)) {
            ir->vm = 1;
            ir->opcode = rv_insn_vmv_v_i;
        } else {
            ir->vm = 0;
            ir->opcode = rv_insn_vmerge_vim;
        }
        break;
    case 4:
        decode_vxtype(ir, insn);
        if (decode_vm(insn)) {
            ir->vm = 1;
            ir->opcode = rv_insn_vmv_v_x;
        } else {
            ir->vm = 0;
            ir->opcode = rv_insn_vmerge_vxm;
        }
        break;
    case 5:
        decode_vxtype(ir, insn);
        if (decode_vm(insn)) {
            ir->vm = 1;
            ir->opcode = rv_insn_vfmv_v_f;
        } else {
            ir->vm = 0;
            ir->opcode = rv_insn_vfmerge_vfm;
        }
        break;
    case 6:
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_011000(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmseq_vv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmfeq_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmandn_mm;
        break;
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vmseq_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmseq_vx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmfeq_vf;
        break;
    case 6:
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_011001(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmsne_vv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmfle_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmand_mm;
        break;
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vmsne_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmsne_vx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmfle_vf;
        break;
    case 6:
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_011010(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmsltu_vv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmflt_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmor_mm;
        break;
    case 3:
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmsltu_vx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmflt_vf;
        break;
    case 6:
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_011011(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmslt_vv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmflt_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmxor_mm;
        break;
    case 3:
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmslt_vx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmflt_vf;
        break;
    case 6:
    default: /* illegal instruction */
        return false;
    }
    return true;
}


static inline bool op_011100(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmsleu_vv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmfne_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmorn_mm;
        break;
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vmsleu_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmsleu_vx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmfne_vf;
        break;
    case 6:
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_011101(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmsle_vv;
        break;
    case 1:
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmnand_mm;
        break;
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vmsle_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmsle_vx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmfgt_vf;
        break;
    case 6:
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_011110(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
    case 1:
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmnor_mm;
        break;
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vmsgtu_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmsgtu_vx;
        break;
    case 5:
    case 6:
    default: /* illegal instruction */
        return false;
    }
    return true;
}


static inline bool op_011111(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
    case 1:
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmxnor_mm;
        break;
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vmsgt_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmsgt_vx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmfge_vf;
        break;
    case 6:
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_100000(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vsaddu_vv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfdiv_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vdivu_vv;
        break;
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vsaddu_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vsaddu_vx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfdiv_vf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vdivu_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_100001(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vsadd_vv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfrdiv_vf;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vdiv_vv;
        break;
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vsadd_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vsadd_vx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfrdiv_vf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vdiv_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_100010(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vssubu_vv;
        break;
    case 1:
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vremu_vv;
        break;
    case 3:
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vssubu_vx;
        break;
    case 5:
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vremu_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_100011(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vssub_vv;
        break;
    case 1:
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vrem_vv;
        break;
    case 3:
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vssub_vx;
        break;
    case 5:
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vrem_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_100100(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfmul_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmulhu_vv;
        break;
    case 3:
    case 4:
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfmul_vf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmulhu_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_100101(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vsll_vv;
        break;
    case 1:
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmul_vv;
        break;
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vsll_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vsll_vx;
        break;
    case 5:
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmul_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_100110(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
    case 1:
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmulhsu_vv;
        break;
    case 3:
    case 4:
    case 5:
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmulhsu_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_100111(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vsmul_vv;
        break;
    case 1:
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmulh_vv;
        break;
    case 3:
        /* FIXME: Implement the decoding for vmv<nr>r. */
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vsmul_vx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfrsub_vf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmulh_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_101000(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vsrl_vv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfmadd_vv;
        break;
    case 2:
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vsrl_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vsrl_vx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfmadd_vf;
        break;
    case 6:
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_101001(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vsra_vv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfnmadd_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmadd_vv;
        break;
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vsra_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vsra_vx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfnmadd_vf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmadd_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_101010(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vssrl_vv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfmsub_vv;
        break;
    case 2:
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vssrl_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vssrl_vx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfmsub_vf;
        break;
    case 6:
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_101011(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vssra_vv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfnmsub_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vnmsub_vv;
        break;
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vssra_vi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vssra_vx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfnmsub_vf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vnmsub_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_101100(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vnsrl_wv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfmacc_vv;
        break;
    case 2:
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vnsrl_wi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vnsrl_wx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfmacc_vf;
        break;
    case 6:
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_101101(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vnsra_wv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfnmacc_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vmacc_vv;
        break;
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vnsra_wi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vnsra_wx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfnmacc_vf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vmacc_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_101110(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vnclipu_wv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfmsac_vv;
        break;
    case 2:
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vnclipu_wi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vnclipu_wx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfmsac_vf;
        break;
    case 6:
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_101111(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vnclip_wv;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfnmsac_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vnmsac_vv;
        break;
    case 3:
        decode_vitype(ir, insn);
        ir->opcode = rv_insn_vnclip_wi;
        break;
    case 4:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vnclip_wx;
        break;
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfnmsac_vf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vnmsac_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_110000(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vwredsumu_vs;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfwadd_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vwaddu_vv;
        break;
    case 3:
    case 4:
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfwadd_vf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vwaddu_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_110001(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vwredsum_vs;
        break;
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfwredusum_vs;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vwadd_vv;
        break;
    case 3:
    case 4:
    case 5:
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vwadd_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_110010(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfwsub_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vwsubu_vv;
        break;
    case 3:
    case 4:
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfwsub_vf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vwsubu_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_110011(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfwredosum_vs;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vwsub_vv;
        break;
    case 3:
    case 4:
    case 5:
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vwsub_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_110100(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfwadd_wv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vwaddu_wv;
        break;
    case 3:
    case 4:
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfwadd_wf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vwaddu_wx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_110101(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
    case 1:
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vwadd_wv;
        break;
    case 3:
    case 4:
    case 5:
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vwadd_wx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_110110(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfwsub_wv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vwsubu_wv;
        break;
    case 3:
    case 4:
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfwsub_wf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vwsubu_wx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_110111(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
    case 1:
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vwsub_wv;
        break;
    case 3:
    case 4:
    case 5:
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vwsub_wx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_111000(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfwmul_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vwmulu_vv;
        break;
    case 3:
    case 4:
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfwmul_vf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vwmulu_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}


static inline bool op_111010(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
    case 1:
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vwmulsu_vv;
        break;
    case 3:
    case 4:
    case 5:
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vwmulsu_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_111011(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
    case 1:
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vwmul_vv;
        break;
    case 3:
    case 4:
    case 5:
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vwmul_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_111100(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfwmacc_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vwmaccu_vv;
        break;
    case 3:
    case 4:
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfwmacc_vf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vwmaccu_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_111101(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfwnmacc_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vwmacc_vv;
        break;
    case 3:
    case 4:
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfwnmacc_vf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vwmacc_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_111110(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfwmsac_vv;
        break;
    case 2:
    case 3:
    case 4:
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfwmsac_vf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vwmaccus_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}

static inline bool op_111111(rv_insn_t *ir, const uint32_t insn)
{
    switch (decode_funct3(insn)) {
    case 0:
    case 1:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vfwnmsac_vv;
        break;
    case 2:
        decode_vvtype(ir, insn);
        ir->opcode = rv_insn_vwmaccsu_vv;
        break;
    case 3:
    case 4:
    case 5:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vfwnmsac_vf;
        break;
    case 6:
        decode_vxtype(ir, insn);
        ir->opcode = rv_insn_vwmaccus_vx;
        break;
    default: /* illegal instruction */
        return false;
    }
    return true;
}
