/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* RISC-V "V" Vector Extension - interpreter handlers.
 *
 * Included from rv32_template.c when RV32_HAS(EXT_V) is set. Defines all
 * V-extension RVOP handlers, the SEW dispatch macros, and the per-lane
 * VI/VV/VX loop macros. Not a standalone translation unit; expects RVOP,
 * VLEN, and the surrounding interpreter context to be in scope.
 */

/* Placeholder body for V-extension RVOPs whose semantics are not yet
 * implemented. Does nothing.
 */
#define V_NOP \
    do {      \
    } while (0)

#define VREG_U32_COUNT ((VLEN) >> (5))
/*
 * Vector Configuration-Setting Instructions
 *
 * These instructions set the vector CSRs, specifically csr_vl and csr_vtype.
 * The CSRs can only be updated using vset{i}vl{i} instructions. The current
 * implementation does not support vma and vta.
 *
 * The value VLMAX = (LMUL * VLEN) / SEW represents the maximum number of
 * elements that can be processed by a single vector instruction given the
 * current SEW and LMUL.
 *
 * Constraints on Setting vl:
 *  - vl = AVL if AVL ≤ VLMAX
 *  - ceil(AVL / 2) ≤ vl ≤ VLMAX if AVL < 2 * VLMAX
 *  - vl = VLMAX if AVL ≥ 2 * VLMAX
 *
 * +------------+------+--------------+
 * | vlmul[2:0] | LMUL |    VLMAX     |
 * +------------+------+--------------+
 * |    1 0 0   |  -   |       -      |
 * |    1 0 1   | 1/8  |  VLEN/SEW/8  |
 * |    1 1 0   | 1/4  |  VLEN/SEW/4  |
 * |    1 1 1   | 1/2  |  VLEN/SEW/2  |
 * |    0 0 0   |  1   |  VLEN/SEW    |
 * |    0 0 1   |  2   |  2*VLEN/SEW  |
 * |    0 1 0   |  4   |  4*VLEN/SEW  |
 * |    0 1 1   |  8   |  8*VLEN/SEW  |
 * +------------+------+--------------+
 *
 * LMUL determines how vector registers are grouped. Since VL controls the
 * number of processed elements (based on SEW) and is derived from VLMAX,
 * LMUL's primary role is setting VLMAX. This implementation computes VLMAX
 * directly, avoiding fractional LMUL values (e.g., 1/2, 1/4, 1/8).
 *
 * Mapping of rd, rs1, and AVL value effects on vl:
 * +-----+-----+------------------+----------------------------------+
 * | rd  | rs1 |    AVL value     |         Effect on vl             |
 * +-----+-----+------------------+----------------------------------+
 * |  -  | !x0 | Value in x[rs1]  | Normal stripmining               |
 * | !x0 |  x0 | ~0               | Set vl to VLMAX                  |
 * |  x0 |  x0 | Value in vl reg  | Keep existing vl                 |
 * +-----+-----+------------------+----------------------------------+
 *
 * +------------+----------+
 * | vsew[2:0]  |   SEW    |
 * +------------+----------+
 * |    0 0 0   |     8    |
 * |    0 0 1   |    16    |
 * |    0 1 0   |    32    |
 * |    0 1 1   |    64    |
 * |    1 X X   | Reserved |
 * +------------+----------+
 */

#define vl_setting(vlmax_, rs1, vl)    \
    if ((rs1) <= vlmax_) {             \
        (vl) = (rs1);                  \
    } else if ((rs1) < (2 * vlmax_)) { \
        (vl) = vlmax_;                 \
    } else {                           \
        (vl) = vlmax_;                 \
    }

RVOP(
    vsetvli,
    {
        uint8_t v_lmul = ir->zimm & 0b111;
        uint8_t v_sew = (ir->zimm >> 3) & 0b111;

        if (v_lmul == 4 || v_sew >= 4) {
            /* Illegal setting */
            rv->csr_vl = 0;
            rv->csr_vtype = 0x80000000;
            return true;
        }
        uint16_t vlmax = (v_lmul < 4)
                             ? ((1 << v_lmul) * VLEN) >> (3 + v_sew)
                             : (VLEN >> (3 + v_sew) >> (3 - (v_lmul - 5)));
        if (ir->rs1) {
            vl_setting(vlmax, rv->X[ir->rs1], rv->csr_vl);
            rv->csr_vtype = ir->zimm;
        } else {
            if (!ir->rd) {
                rv->csr_vtype = ir->zimm;
            } else {
                rv->csr_vl = vlmax;
                rv->csr_vtype = ir->zimm;
            }
        }
        rv->X[ir->rd] = rv->csr_vl;
    })

RVOP(
    vsetivli,
    {
        uint8_t v_lmul = ir->zimm & 0b111;
        uint8_t v_sew = (ir->zimm >> 3) & 0b111;

        if (v_lmul == 4 || v_sew >= 4) {
            /* Illegal setting */
            rv->csr_vl = 0;
            rv->csr_vtype = 0x80000000;
            return true;
        }
        uint16_t vlmax = (v_lmul < 4)
                             ? ((1 << v_lmul) * VLEN) >> (3 + v_sew)
                             : (VLEN >> (3 + v_sew) >> (3 - (v_lmul - 5)));
        if (ir->rs1) {
            vl_setting(vlmax, ir->rs1, rv->csr_vl);
            rv->csr_vtype = ir->zimm;
        } else {
            if (!ir->rd) {
                rv->csr_vtype = ir->zimm;
            } else {
                rv->csr_vl = vlmax;
                rv->csr_vtype = ir->zimm;
            }
        }
        rv->X[ir->rd] = rv->csr_vl;
    })

RVOP(
    vsetvl,
    {
        uint8_t v_lmul = rv->X[ir->rs2] & 0b111;
        uint8_t v_sew = (rv->X[ir->rs2] >> 3) & 0b111;

        if (v_lmul == 4 || v_sew >= 4) {
            /* Illegal setting */
            rv->csr_vl = 0;
            rv->csr_vtype = 0x80000000;
            return true;
        }
        uint16_t vlmax = (v_lmul < 4)
                             ? ((1 << v_lmul) * VLEN) >> (3 + v_sew)
                             : (VLEN >> (3 + v_sew) >> (3 - (v_lmul - 5)));
        if (rv->X[ir->rs1]) {
            vl_setting(vlmax, rv->X[ir->rs1], rv->csr_vl);
            rv->csr_vtype = rv->X[ir->rs2];
        } else {
            if (!ir->rd) {
                rv->csr_vtype = rv->X[ir->rs2];
            } else {
                rv->csr_vl = vlmax;
                rv->csr_vtype = rv->X[ir->rs2];
            }
        }
        rv->X[ir->rd] = rv->csr_vl;
    })
#undef vl_setting

/*
 * In RVV, vector register v0 serves as the mask register. Each bit in v0
 * indicates whether the corresponding element in other vector registers should
 * be updated or left unmodified. When ir->vm == 1, masking is disabled. When
 * ir->vm == 0, masking is enabled, and for each element, the bit in v0
 * determines whether to use the newly computed result (bit = 1) or keep the
 * original value in the destination register (bit = 0).
 *
 * The macro VECTOR_DISPATCH(des, op1, op2, op, op_type) selects the
 * corresponding handler based on the sew from csr_vtype. It then calls one of
 * the sew_*b_handler functions for 8-bit, 16-bit, or 32-bit operations. Each
 * handler checks csr_vl to determine how many elements need to be processed and
 * uses one of the three macros VV_LOOP, VX_LOOP, VI_LOOP depending on whether
 * the second operand is a vector, a scalar, or an immediate. These LOOP macros
 * handle one 32-bit word at a time and pass remainder to their respective
 * V*_LOOP_LEFT macro if the csr_vl is not evenly divisible.
 *
 * Inside each loop macro, SHIFT and MASK determine how to isolate and position
 * sub-elements within each 32-bit word. SHIFT specifies how many bits to shift
 * for each sub-element, and MASK filters out the bits that belong to a single
 * sub-element, such as 0xFF for 8-bit or 0xFFFF for 16-bit. Index __i tracks
 * which 32-bit word within the vector register is processed; when it reaches
 * the end of the current VLEN vector register, __j increments to move on to the
 * next vector register, and __i resets.
 *
 * The current approach supports only SEW values of 8, 16, and 32 bits.
 */

#define VECTOR_DISPATCH(des, op1, op2, op, op_type)      \
    {                                                    \
        switch (8 << ((rv->csr_vtype >> 3) & 0b111)) {   \
        case 8:                                          \
            sew_8b_handler(des, op1, op2, op, op_type);  \
            break;                                       \
        case 16:                                         \
            sew_16b_handler(des, op1, op2, op, op_type); \
            break;                                       \
        case 32:                                         \
            sew_32b_handler(des, op1, op2, op, op_type); \
            break;                                       \
        default:                                         \
            break;                                       \
        }                                                \
    }
/* Extract vta and vma bits from csr_vtype */
#define GET_VTA(vtype) (((vtype) >> 6) & 0x1)
#define GET_VMA(vtype) (((vtype) >> 7) & 0x1)
#define GET_VSEW(vtype) (((vtype) >> 3) & 0x7)
#define GET_VLMUL(vtype) ((vtype) & 0x7)

/* Agnostic fill values based on SEW */
#define AGNOSTIC_FILL_8b 0xFF
#define AGNOSTIC_FILL_16b 0xFFFF
#define AGNOSTIC_FILL_32b 0xFFFFFFFF

/* Get agnostic fill value for current SEW */
#define GET_FILL_VALUE(sew_bits)                            \
    ((sew_bits) == 8 ? AGNOSTIC_FILL_8b                     \
                     : (sew_bits) == 16 ? AGNOSTIC_FILL_16b \
                                        : AGNOSTIC_FILL_32b)

/* Per-lane value selection for the V*_LOOP / V*_LOOP_LEFT family.
 * Returns the SEW-wide bit-field for lane k, shifted into its slot:
 *   - active (mask disabled OR mask bit set): VALUE_EXPR;
 *   - vma=1, masked-off: agnostic fill;
 *   - vma=0, masked-off: keep destination's prior bits (undisturbed).
 *
 * VALUE_EXPR is only evaluated on the active path, so it may call
 * op_##op() with operands undefined for inactive lanes.
 * Captures from enclosing scope: ir, vm, vma, fill, tmp_d.
 */
#define V_LANE_VALUE(VALUE_EXPR, k, SHIFT, MASK)                    \
    ((ir->vm || ((vm) & (1U << (k))))                               \
         ? (((uint32_t) (VALUE_EXPR) & (MASK)) << ((k) << (SHIFT))) \
         : (vma) ? (((fill) & (MASK)) << ((k) << (SHIFT)))          \
                 : (tmp_d & ((uint32_t) (MASK) << ((k) << (SHIFT)))))

#define VI_LOOP(des, op1, op2, op, SHIFT, MASK, i, j, itr, vm)         \
    uint32_t tmp_1 = rv->V[(op1) + (j)][i];                            \
    uint32_t tmp_d = rv->V[(des) + (j)][i];                            \
    uint32_t out = 0;                                                  \
    for (uint8_t k = 0; k < (itr); k++) {                              \
        out |= V_LANE_VALUE(op_##op((tmp_1 >> (k << (SHIFT))), (op2)), \
                            k, SHIFT, MASK);                           \
    }                                                                  \
    rv->V[(des) + (j)][i] = out;

/* Partial-tail word: 0 < body_count < itr body lanes followed by tail
 * lanes within the same word. Caller (sew_*b_handler) guarantees this
 * word is valid (j < num_regs). When body_count == 0 the macro is a
 * no-op; the caller then fills the entirely-tail word.
 */
#define VI_LOOP_LEFT(des, op1, op2, op, SHIFT, MASK, i, j, itr, vm)        \
    uint32_t tmp_1 = rv->V[(op1) + (j)][i];                                \
    uint32_t tmp_d = rv->V[(des) + (j)][i];                                \
    uint8_t body_count = rv->csr_vl % (itr);                               \
    if (body_count > 0 && vta) {                                           \
        uint32_t body_mask = (1U << (body_count << (SHIFT))) - 1;          \
        rv->V[(des) + (j)][i] =                                            \
            (rv->V[(des) + (j)][i] & body_mask) | ~body_mask;              \
    }                                                                      \
    for (uint8_t k = 0; k < body_count; k++) {                             \
        assert(((des) + (j)) < 32);                                        \
        uint32_t lane =                                                    \
            V_LANE_VALUE(op_##op((tmp_1 >> (k << (SHIFT))), (op2)),        \
                         k, SHIFT, MASK);                                  \
        rv->V[(des) + (j)][i] =                                            \
            (rv->V[(des) + (j)][i] & ~((MASK) << (k << (SHIFT)))) | lane;  \
    }

#define VV_LOOP(des, op1, op2, op, SHIFT, MASK, i, j, itr, vm) \
    uint32_t tmp_1 = rv->V[(op1) + (j)][i];                    \
    uint32_t tmp_2 = rv->V[(op2) + (j)][i];                    \
    uint32_t tmp_d = rv->V[(des) + (j)][i];                    \
    uint32_t out = 0;                                          \
    for (uint8_t k = 0; k < (itr); k++) {                      \
        out |= V_LANE_VALUE(op_##op((tmp_1 >> (k << (SHIFT))), \
                                    (tmp_2 >> (k << (SHIFT)))), \
                            k, SHIFT, MASK);                   \
    }                                                          \
    rv->V[(des) + (j)][i] = out;

#define VV_LOOP_LEFT(des, op1, op2, op, SHIFT, MASK, i, j, itr, vm)        \
    uint32_t tmp_1 = rv->V[(op1) + (j)][i];                                \
    uint32_t tmp_2 = rv->V[(op2) + (j)][i];                                \
    uint32_t tmp_d = rv->V[(des) + (j)][i];                                \
    uint8_t body_count = rv->csr_vl % (itr);                               \
    if (body_count > 0 && vta) {                                           \
        uint32_t body_mask = (1U << (body_count << (SHIFT))) - 1;          \
        rv->V[(des) + (j)][i] =                                            \
            (rv->V[(des) + (j)][i] & body_mask) | ~body_mask;              \
    }                                                                      \
    for (uint8_t k = 0; k < body_count; k++) {                             \
        assert(((des) + (j)) < 32);                                        \
        uint32_t lane = V_LANE_VALUE(op_##op((tmp_1 >> (k << (SHIFT))),    \
                                             (tmp_2 >> (k << (SHIFT)))),   \
                                     k, SHIFT, MASK);                      \
        rv->V[(des) + (j)][i] =                                            \
            (rv->V[(des) + (j)][i] & ~((MASK) << (k << (SHIFT)))) | lane;  \
    }

#define VX_LOOP(des, op1, op2, op, SHIFT, MASK, i, j, itr, vm)         \
    uint32_t tmp_1 = rv->V[(op1) + (j)][i];                            \
    uint32_t tmp_2 = rv->X[op2];                                       \
    uint32_t tmp_d = rv->V[(des) + (j)][i];                            \
    uint32_t out = 0;                                                  \
    for (uint8_t k = 0; k < (itr); k++) {                              \
        out |= V_LANE_VALUE(op_##op((tmp_1 >> (k << (SHIFT))), tmp_2), \
                            k, SHIFT, MASK);                           \
    }                                                                  \
    rv->V[(des) + (j)][i] = out;

#define VX_LOOP_LEFT(des, op1, op2, op, SHIFT, MASK, i, j, itr, vm)        \
    uint32_t tmp_1 = rv->V[(op1) + (j)][i];                                \
    uint32_t tmp_2 = rv->X[op2];                                           \
    uint32_t tmp_d = rv->V[(des) + (j)][i];                                \
    uint8_t body_count = rv->csr_vl % (itr);                               \
    if (body_count > 0 && vta) {                                           \
        uint32_t body_mask = (1U << (body_count << (SHIFT))) - 1;          \
        rv->V[(des) + (j)][i] =                                            \
            (rv->V[(des) + (j)][i] & body_mask) | ~body_mask;              \
    }                                                                      \
    for (uint8_t k = 0; k < body_count; k++) {                             \
        assert(((des) + (j)) < 32);                                        \
        uint32_t lane =                                                    \
            V_LANE_VALUE(op_##op((tmp_1 >> (k << (SHIFT))), tmp_2),        \
                         k, SHIFT, MASK);                                  \
        rv->V[(des) + (j)][i] =                                            \
            (rv->V[(des) + (j)][i] & ~((MASK) << (k << (SHIFT)))) | lane;  \
    }

/* Fill every word from (__j, __i) inclusive to the end of register __j,
 * then every word of registers __j+1 .. num_regs-1, with the agnostic
 * fill pattern 0xFFFFFFFF. Used by sew_*b_handler after body and partial-
 * tail processing when vta=1. Captures rv, __i, __j (mutated in place).
 */
#define V_FILL_LMUL_TAIL(des, num_regs)                  \
    do {                                                 \
        while (__i < VREG_U32_COUNT) {                   \
            assert(((des) + __j) < 32);                  \
            rv->V[(des) + __j][__i] = 0xFFFFFFFF;        \
            __i++;                                       \
        }                                                \
        __j++;                                           \
        while (__j < (num_regs)) {                       \
            for (uint8_t w = 0; w < VREG_U32_COUNT; w++) \
                rv->V[(des) + __j][w] = 0xFFFFFFFF;      \
            __j++;                                       \
        }                                                \
    } while (0)

#define sew_8b_handler(des, op1, op2, op, op_type)                            \
    {                                                                         \
        uint8_t vma = GET_VMA(rv->csr_vtype);                                 \
        uint8_t vta = GET_VTA(rv->csr_vtype);                                 \
        uint8_t v_lmul = GET_VLMUL(rv->csr_vtype);                            \
        uint8_t num_regs = (v_lmul < 4) ? (1 << v_lmul) : 1;                  \
        uint32_t fill = AGNOSTIC_FILL_8b;                                     \
        uint8_t __i = 0;                                                      \
        uint8_t __j = 0;                                                      \
        uint8_t __m = 0;                                                      \
        uint32_t vm = rv->V[0][__m];                                          \
        for (uint32_t __k = 0; (rv->csr_vl - __k) >= 4;) {                    \
            __i %= VREG_U32_COUNT;                                            \
            assert((des + __j) < 32);                                         \
            op_type##_LOOP(des, op1, op2, op, 3, 0xFF, __i, __j, 4, vm);      \
            __k += 4;                                                         \
            __i++;                                                            \
            if (!(__k % (VREG_U32_COUNT << 2))) {                             \
                __j++;                                                        \
                __i = 0;                                                      \
            }                                                                 \
            vm >>= 4;                                                         \
            if (!(__k % 32)) {                                                \
                __m++;                                                        \
                vm = rv->V[0][__m];                                           \
            }                                                                 \
        }                                                                     \
        if (__j < num_regs) {                                                 \
            op_type##_LOOP_LEFT(des, op1, op2, op, 3, 0xFF, __i, __j, 4, vm); \
            if (vta) {                                                        \
                if (rv->csr_vl % 4)                                           \
                    __i++;                                                    \
                V_FILL_LMUL_TAIL(des, num_regs);                              \
            }                                                                 \
        }                                                                     \
    }

#define sew_16b_handler(des, op1, op2, op, op_type)                         \
    {                                                                       \
        uint8_t vma = GET_VMA(rv->csr_vtype);                               \
        uint8_t vta = GET_VTA(rv->csr_vtype);                               \
        uint8_t v_lmul = GET_VLMUL(rv->csr_vtype);                          \
        uint8_t num_regs = (v_lmul < 4) ? (1 << v_lmul) : 1;                \
        uint32_t fill = AGNOSTIC_FILL_16b;                                  \
        uint8_t __i = 0;                                                    \
        uint8_t __j = 0;                                                    \
        uint8_t __m = 0;                                                    \
        uint32_t vm = rv->V[0][__m];                                        \
        for (uint32_t __k = 0; (rv->csr_vl - __k) >= 2;) {                  \
            __i %= VREG_U32_COUNT;                                          \
            assert((des + __j) < 32);                                       \
            op_type##_LOOP(des, op1, op2, op, 4, 0xFFFF, __i, __j, 2, vm);  \
            __k += 2;                                                       \
            __i++;                                                          \
            if (!(__k % (VREG_U32_COUNT << 1))) {                           \
                __j++;                                                      \
                __i = 0;                                                    \
            }                                                               \
            vm >>= 2;                                                       \
            if (!(__k % 32)) {                                              \
                __m++;                                                      \
                vm = rv->V[0][__m];                                         \
            }                                                               \
        }                                                                   \
        if (__j < num_regs) {                                               \
            op_type##_LOOP_LEFT(des, op1, op2, op, 4, 0xFFFF, __i, __j, 2,  \
                                vm);                                        \
            if (vta) {                                                      \
                if (rv->csr_vl % 2)                                         \
                    __i++;                                                  \
                V_FILL_LMUL_TAIL(des, num_regs);                            \
            }                                                               \
        }                                                                   \
    }

#define sew_32b_handler(des, op1, op2, op, op_type)                            \
    {                                                                          \
        uint8_t vma = GET_VMA(rv->csr_vtype);                                  \
        uint8_t vta = GET_VTA(rv->csr_vtype);                                  \
        uint8_t v_lmul = GET_VLMUL(rv->csr_vtype);                             \
        uint8_t num_regs = (v_lmul < 4) ? (1 << v_lmul) : 1;                   \
        uint32_t fill = AGNOSTIC_FILL_32b;                                     \
        uint8_t __i = 0;                                                       \
        uint8_t __j = 0;                                                       \
        uint8_t __m = 0;                                                       \
        uint32_t vm = rv->V[0][__m];                                           \
        for (uint32_t __k = 0; rv->csr_vl > __k;) {                            \
            __i %= VREG_U32_COUNT;                                             \
            assert((des + __j) < 32);                                          \
            op_type##_LOOP(des, op1, op2, op, 0, 0xFFFFFFFF, __i, __j, 1, vm); \
            __k += 1;                                                          \
            __i++;                                                             \
            if (!(__k % VREG_U32_COUNT)) {                                     \
                __j++;                                                         \
                __i = 0;                                                       \
            }                                                                  \
            vm >>= 1;                                                          \
            if (!(__k % 32)) {                                                 \
                __m++;                                                         \
                vm = rv->V[0][__m];                                            \
            }                                                                  \
        }                                                                      \
        if (vta && __j < num_regs)                                             \
            V_FILL_LMUL_TAIL(des, num_regs);                                   \
    }

RVOP(
    vle8_v,
    {
        uint8_t sew = 8 << ((rv->csr_vtype >> 3) & 0b111);
        uint32_t addr = rv->X[ir->rs1];

        if (ir->eew > sew) {
            /* Illegal */
            rv->csr_vtype = 0x80000000;
            rv->csr_vl = 0;
            return true;
        } else {
            uint8_t i = 0;
            uint8_t j = 0;
            for (uint32_t cnt = 0; rv->csr_vl - cnt >= 4;) {
                i %= VREG_U32_COUNT;
                /* Set illegal when trying to access vector register that is
                 * larger then 31.
                 */
                assert(ir->vd + j < 32);
                /* Process full 32-bit words */
                rv->V[ir->vd + j][i] = 0;
                rv->V[ir->vd + j][i] |= rv->io.mem_read_b(rv, addr);
                rv->V[ir->vd + j][i] |= rv->io.mem_read_b(rv, addr + 1) << 8;
                rv->V[ir->vd + j][i] |= rv->io.mem_read_b(rv, addr + 2) << 16;
                rv->V[ir->vd + j][i] |= rv->io.mem_read_b(rv, addr + 3) << 24;
                cnt += 4;
                i++;

                /* Move to next vector register after filling VLEN */
                if (!(cnt % (VREG_U32_COUNT << 2))) {
                    j++;
                    i = 0;
                }
                addr += 4;
            }
            /* Clear the low bytes about to be loaded; leave the upper
             * bytes (tail) untouched. Full vta agnostic-fill handling
             * for loads is not yet implemented.
             */
            if (rv->csr_vl % 4) {
                rv->V[ir->vd + j][i] &= 0xFFFFFFFF << ((rv->csr_vl % 4) << 3);
            }
            /* Handle eews that is narrower then a word */
            for (uint32_t cnt = 0; cnt < (rv->csr_vl % 4); cnt++) {
                assert(ir->vd + j < 32); /* Illegal */
                rv->V[ir->vd + j][i] |= rv->io.mem_read_b(rv, addr + cnt)
                                        << (cnt << 3);
            }
        }
    })

RVOP(
    vle16_v,
    {
        uint8_t sew = 8 << ((rv->csr_vtype >> 3) & 0b111);
        uint32_t addr = rv->X[ir->rs1];

        if (ir->eew > sew) {
            /* Illegal */
            rv->csr_vtype = 0x80000000;
            rv->csr_vl = 0;
            return true;
        } else {
            uint8_t i = 0;
            uint8_t j = 0;
            for (uint32_t cnt = 0; rv->csr_vl - cnt >= 2;) {
                i %= VREG_U32_COUNT;
                assert(ir->vd + j < 32);
                /* Process full 32-bit words */
                rv->V[ir->vd + j][i] = 0;
                rv->V[ir->vd + j][i] |= rv->io.mem_read_s(rv, addr);
                rv->V[ir->vd + j][i] |= rv->io.mem_read_s(rv, addr + 2) << 16;
                cnt += 2;
                i++;

                /* Move to next vector register after filling VLEN */
                if (!(cnt % (VREG_U32_COUNT << 1))) {
                    j++;
                    i = 0;
                }
                addr += 4;
            }
            if (rv->csr_vl % 2) {
                assert(ir->vd + j < 32); /* Illegal */
                rv->V[ir->vd + j][i] |= rv->io.mem_read_s(rv, addr);
            }
        }
    })

RVOP(
    vle32_v,
    {
        uint8_t sew = 8 << ((rv->csr_vtype >> 3) & 0b111);
        uint32_t addr = rv->X[ir->rs1];

        if (ir->eew > sew) {
            /* Illegal */
            rv->csr_vtype = 0x80000000;
            rv->csr_vl = 0;
            return true;
        } else {
            uint8_t i = 0;
            uint8_t j = 0;
            for (uint32_t cnt = 0; rv->csr_vl > cnt;) {
                i %= VREG_U32_COUNT;
                assert(ir->vd + j < 32);
                rv->V[ir->vd + j][i] = rv->io.mem_read_w(rv, addr);
                cnt += 1;
                i++;

                if (!(cnt % VREG_U32_COUNT)) {
                    j++;
                    i = 0;
                }
                addr += 4;
            }
        }
    })

RVOP(
    vle64_v,
    { V_NOP; })

RVOP(
    vlseg2e8_v,
    { V_NOP; })

RVOP(
    vlseg3e8_v,
    { V_NOP; })

RVOP(
    vlseg4e8_v,
    { V_NOP; })

RVOP(
    vlseg5e8_v,
    { V_NOP; })

RVOP(
    vlseg6e8_v,
    { V_NOP; })

RVOP(
    vlseg7e8_v,
    { V_NOP; })

RVOP(
    vlseg8e8_v,
    { V_NOP; })

RVOP(
    vlseg2e16_v,
    { V_NOP; })

RVOP(
    vlseg3e16_v,
    { V_NOP; })

RVOP(
    vlseg4e16_v,
    { V_NOP; })

RVOP(
    vlseg5e16_v,
    { V_NOP; })

RVOP(
    vlseg6e16_v,
    { V_NOP; })

RVOP(
    vlseg7e16_v,
    { V_NOP; })

RVOP(
    vlseg8e16_v,
    { V_NOP; })

RVOP(
    vlseg2e32_v,
    { V_NOP; })

RVOP(
    vlseg3e32_v,
    { V_NOP; })

RVOP(
    vlseg4e32_v,
    { V_NOP; })

RVOP(
    vlseg5e32_v,
    { V_NOP; })

RVOP(
    vlseg6e32_v,
    { V_NOP; })

RVOP(
    vlseg7e32_v,
    { V_NOP; })

RVOP(
    vlseg8e32_v,
    { V_NOP; })

RVOP(
    vlseg2e64_v,
    { V_NOP; })

RVOP(
    vlseg3e64_v,
    { V_NOP; })

RVOP(
    vlseg4e64_v,
    { V_NOP; })

RVOP(
    vlseg5e64_v,
    { V_NOP; })

RVOP(
    vlseg6e64_v,
    { V_NOP; })

RVOP(
    vlseg7e64_v,
    { V_NOP; })

RVOP(
    vlseg8e64_v,
    { V_NOP; })

RVOP(
    vl1re8_v,
    { V_NOP; })

RVOP(
    vl1re16_v,
    { V_NOP; })

RVOP(
    vl1re32_v,
    { V_NOP; })

RVOP(
    vl1re64_v,
    { V_NOP; })

RVOP(
    vl2re8_v,
    { V_NOP; })

RVOP(
    vl2re16_v,
    { V_NOP; })
RVOP(
    vl2re32_v,
    { V_NOP; })

RVOP(
    vl2re64_v,
    { V_NOP; })

RVOP(
    vl4re8_v,
    { V_NOP; })

RVOP(
    vl4re16_v,
    { V_NOP; })

RVOP(
    vl4re32_v,
    { V_NOP; })

RVOP(
    vl4re64_v,
    { V_NOP; })

RVOP(
    vl8re8_v,
    { V_NOP; })

RVOP(
    vl8re16_v,
    { V_NOP; })

RVOP(
    vl8re32_v,
    { V_NOP; })

RVOP(
    vl8re64_v,
    { V_NOP; })

RVOP(
    vlm_v,
    { V_NOP; })

RVOP(
    vle8ff_v,
    { V_NOP; })

RVOP(
    vle16ff_v,
    { V_NOP; })

RVOP(
    vle32ff_v,
    { V_NOP; })

RVOP(
    vle64ff_v,
    { V_NOP; })

RVOP(
    vlseg2e8ff_v,
    { V_NOP; })

RVOP(
    vlseg3e8ff_v,
    { V_NOP; })

RVOP(
    vlseg4e8ff_v,
    { V_NOP; })

RVOP(
    vlseg5e8ff_v,
    { V_NOP; })

RVOP(
    vlseg6e8ff_v,
    { V_NOP; })

RVOP(
    vlseg7e8ff_v,
    { V_NOP; })

RVOP(
    vlseg8e8ff_v,
    { V_NOP; })

RVOP(
    vlseg2e16ff_v,
    { V_NOP; })

RVOP(
    vlseg3e16ff_v,
    { V_NOP; })

RVOP(
    vlseg4e16ff_v,
    { V_NOP; })

RVOP(
    vlseg5e16ff_v,
    { V_NOP; })

RVOP(
    vlseg6e16ff_v,
    { V_NOP; })

RVOP(
    vlseg7e16ff_v,
    { V_NOP; })

RVOP(
    vlseg8e16ff_v,
    { V_NOP; })

RVOP(
    vlseg2e32ff_v,
    { V_NOP; })

RVOP(
    vlseg3e32ff_v,
    { V_NOP; })

RVOP(
    vlseg4e32ff_v,
    { V_NOP; })

RVOP(
    vlseg5e32ff_v,
    { V_NOP; })

RVOP(
    vlseg6e32ff_v,
    { V_NOP; })

RVOP(
    vlseg7e32ff_v,
    { V_NOP; })

RVOP(
    vlseg8e32ff_v,
    { V_NOP; })

RVOP(
    vlseg2e64ff_v,
    { V_NOP; })

RVOP(
    vlseg3e64ff_v,
    { V_NOP; })

RVOP(
    vlseg4e64ff_v,
    { V_NOP; })

RVOP(
    vlseg5e64ff_v,
    { V_NOP; })

RVOP(
    vlseg6e64ff_v,
    { V_NOP; })

RVOP(
    vlseg7e64ff_v,
    { V_NOP; })

RVOP(
    vlseg8e64ff_v,
    { V_NOP; })

RVOP(
    vluxei8_v,
    { V_NOP; })

RVOP(
    vluxei16_v,
    { V_NOP; })

RVOP(
    vluxei32_v,
    { V_NOP; })

RVOP(
    vluxei64_v,
    { V_NOP; })

RVOP(
    vluxseg2ei8_v,
    { V_NOP; })

RVOP(
    vluxseg3ei8_v,
    { V_NOP; })

RVOP(
    vluxseg4ei8_v,
    { V_NOP; })

RVOP(
    vluxseg5ei8_v,
    { V_NOP; })

RVOP(
    vluxseg6ei8_v,
    { V_NOP; })

RVOP(
    vluxseg7ei8_v,
    { V_NOP; })

RVOP(
    vluxseg8ei8_v,
    { V_NOP; })

RVOP(
    vluxseg2ei16_v,
    { V_NOP; })

RVOP(
    vluxseg3ei16_v,
    { V_NOP; })

RVOP(
    vluxseg4ei16_v,
    { V_NOP; })

RVOP(
    vluxseg5ei16_v,
    { V_NOP; })

RVOP(
    vluxseg6ei16_v,
    { V_NOP; })

RVOP(
    vluxseg7ei16_v,
    { V_NOP; })

RVOP(
    vluxseg8ei16_v,
    { V_NOP; })

RVOP(
    vluxseg2ei32_v,
    { V_NOP; })

RVOP(
    vluxseg3ei32_v,
    { V_NOP; })

RVOP(
    vluxseg4ei32_v,
    { V_NOP; })

RVOP(
    vluxseg5ei32_v,
    { V_NOP; })

RVOP(
    vluxseg6ei32_v,
    { V_NOP; })

RVOP(
    vluxseg7ei32_v,
    { V_NOP; })

RVOP(
    vluxseg8ei32_v,
    { V_NOP; })

RVOP(
    vluxseg2ei64_v,
    { V_NOP; })

RVOP(
    vluxseg3ei64_v,
    { V_NOP; })

RVOP(
    vluxseg4ei64_v,
    { V_NOP; })

RVOP(
    vluxseg5ei64_v,
    { V_NOP; })

RVOP(
    vluxseg6ei64_v,
    { V_NOP; })

RVOP(
    vluxseg7ei64_v,
    { V_NOP; })

RVOP(
    vluxseg8ei64_v,
    { V_NOP; })

RVOP(
    vlse8_v,
    { V_NOP; })

RVOP(
    vlse16_v,
    { V_NOP; })

RVOP(
    vlse32_v,
    { V_NOP; })

RVOP(
    vlse64_v,
    { V_NOP; })

RVOP(
    vlsseg2e8_v,
    { V_NOP; })

RVOP(
    vlsseg3e8_v,
    { V_NOP; })

RVOP(
    vlsseg4e8_v,
    { V_NOP; })

RVOP(
    vlsseg5e8_v,
    { V_NOP; })

RVOP(
    vlsseg6e8_v,
    { V_NOP; })

RVOP(
    vlsseg7e8_v,
    { V_NOP; })

RVOP(
    vlsseg8e8_v,
    { V_NOP; })

RVOP(
    vlsseg2e16_v,
    { V_NOP; })

RVOP(
    vlsseg3e16_v,
    { V_NOP; })

RVOP(
    vlsseg4e16_v,
    { V_NOP; })

RVOP(
    vlsseg5e16_v,
    { V_NOP; })

RVOP(
    vlsseg6e16_v,
    { V_NOP; })

RVOP(
    vlsseg7e16_v,
    { V_NOP; })

RVOP(
    vlsseg8e16_v,
    { V_NOP; })

RVOP(
    vlsseg2e32_v,
    { V_NOP; })

RVOP(
    vlsseg3e32_v,
    { V_NOP; })

RVOP(
    vlsseg4e32_v,
    { V_NOP; })

RVOP(
    vlsseg5e32_v,
    { V_NOP; })

RVOP(
    vlsseg6e32_v,
    { V_NOP; })

RVOP(
    vlsseg7e32_v,
    { V_NOP; })

RVOP(
    vlsseg8e32_v,
    { V_NOP; })

RVOP(
    vlsseg2e64_v,
    { V_NOP; })

RVOP(
    vlsseg3e64_v,
    { V_NOP; })

RVOP(
    vlsseg4e64_v,
    { V_NOP; })

RVOP(
    vlsseg5e64_v,
    { V_NOP; })

RVOP(
    vlsseg6e64_v,
    { V_NOP; })

RVOP(
    vlsseg7e64_v,
    { V_NOP; })

RVOP(
    vlsseg8e64_v,
    { V_NOP; })

RVOP(
    vloxei8_v,
    { V_NOP; })

RVOP(
    vloxei16_v,
    { V_NOP; })

RVOP(
    vloxei32_v,
    { V_NOP; })

RVOP(
    vloxei64_v,
    { V_NOP; })

RVOP(
    vloxseg2ei8_v,
    { V_NOP; })

RVOP(
    vloxseg3ei8_v,
    { V_NOP; })

RVOP(
    vloxseg4ei8_v,
    { V_NOP; })

RVOP(
    vloxseg5ei8_v,
    { V_NOP; })

RVOP(
    vloxseg6ei8_v,
    { V_NOP; })

RVOP(
    vloxseg7ei8_v,
    { V_NOP; })

RVOP(
    vloxseg8ei8_v,
    { V_NOP; })

RVOP(
    vloxseg2ei16_v,
    { V_NOP; })

RVOP(
    vloxseg3ei16_v,
    { V_NOP; })

RVOP(
    vloxseg4ei16_v,
    { V_NOP; })

RVOP(
    vloxseg5ei16_v,
    { V_NOP; })

RVOP(
    vloxseg6ei16_v,
    { V_NOP; })

RVOP(
    vloxseg7ei16_v,
    { V_NOP; })

RVOP(
    vloxseg8ei16_v,
    { V_NOP; })

RVOP(
    vloxseg2ei32_v,
    { V_NOP; })

RVOP(
    vloxseg3ei32_v,
    { V_NOP; })

RVOP(
    vloxseg4ei32_v,
    { V_NOP; })

RVOP(
    vloxseg5ei32_v,
    { V_NOP; })

RVOP(
    vloxseg6ei32_v,
    { V_NOP; })

RVOP(
    vloxseg7ei32_v,
    { V_NOP; })

RVOP(
    vloxseg8ei32_v,
    { V_NOP; })

RVOP(
    vloxseg2ei64_v,
    { V_NOP; })

RVOP(
    vloxseg3ei64_v,
    { V_NOP; })

RVOP(
    vloxseg4ei64_v,
    { V_NOP; })

RVOP(
    vloxseg5ei64_v,
    { V_NOP; })

RVOP(
    vloxseg6ei64_v,
    { V_NOP; })

RVOP(
    vloxseg7ei64_v,
    { V_NOP; })

RVOP(
    vloxseg8ei64_v,
    { V_NOP; })

RVOP(
    vse8_v,
    {
        uint8_t sew = 8 << ((rv->csr_vtype >> 3) & 0b111);
        uint32_t addr = rv->X[ir->rs1];

        if (ir->eew > sew) {
            /* Illegal */
            rv->csr_vtype = 0x80000000;
            rv->csr_vl = 0;
            return true;
        } else {
            uint8_t i = 0;
            uint8_t j = 0;
            for (uint32_t cnt = 0; rv->csr_vl - cnt >= 4;) {
                i %= VREG_U32_COUNT;
                /* Set illegal when trying to access vector register that is
                 * larger then 31.
                 */
                assert(ir->vs3 + j < 32);
                uint32_t tmp = rv->V[ir->vs3 + j][i];
                /* Process full 32-bit words */
                rv->io.mem_write_b(rv, addr, (tmp) & 0xff);
                rv->io.mem_write_b(rv, addr + 1, (tmp >> 8) & 0xff);
                rv->io.mem_write_b(rv, addr + 2, (tmp >> 16) & 0xff);
                rv->io.mem_write_b(rv, addr + 3, (tmp >> 24) & 0xff);
                cnt += 4;
                i++;

                /* Move to next vector register after filling VLEN */
                if (!(cnt % (VREG_U32_COUNT << 2))) {
                    j++;
                    i = 0;
                }
                addr += 4;
            }
            /* Handle eews that is narrower then a word */
            for (uint32_t cnt = 0; cnt < (rv->csr_vl % 4); cnt++) {
                assert(ir->vs3 + j < 32); /* Illegal */
                uint8_t tmp = (rv->V[ir->vs3 + j][i] >> (cnt << 3)) & 0xff;
                rv->io.mem_write_b(rv, addr + cnt, tmp);
            }
        }
    })

RVOP(
    vse16_v,
    {
        uint8_t sew = 8 << ((rv->csr_vtype >> 3) & 0b111);
        uint32_t addr = rv->X[ir->rs1];

        if (ir->eew > sew) {
            /* Illegal */
            rv->csr_vtype = 0x80000000;
            rv->csr_vl = 0;
            return true;
        } else {
            uint8_t i = 0;
            uint8_t j = 0;
            for (uint32_t cnt = 0; rv->csr_vl - cnt >= 2;) {
                i %= VREG_U32_COUNT;
                assert(ir->vs3 + j < 32);
                uint32_t tmp = rv->V[ir->vs3 + j][i];
                /* Process full 32-bit words */
                rv->io.mem_write_s(rv, addr, (tmp) & 0xffff);
                rv->io.mem_write_s(rv, addr + 2, (tmp >> 16) & 0xffff);
                cnt += 2;
                i++;

                if (!(cnt % (VREG_U32_COUNT << 1))) {
                    j++;
                    i = 0;
                }
                addr += 4;
            }
            if (rv->csr_vl % 2) {
                rv->io.mem_write_s(rv, addr, rv->V[ir->vs3 + j][i] & 0xffff);
            }
        }
    })

RVOP(
    vse32_v,
    {
        uint8_t sew = 8 << ((rv->csr_vtype >> 3) & 0b111);
        uint32_t addr = rv->X[ir->rs1];

        if (ir->eew > sew) {
            /* Illegal */
            rv->csr_vtype = 0x80000000;
            rv->csr_vl = 0;
            return true;
        } else {
            uint8_t i = 0;
            uint8_t j = 0;
            for (uint32_t cnt = 0; rv->csr_vl > cnt;) {
                i %= VREG_U32_COUNT;
                assert(ir->vs3 + j < 32);
                rv->io.mem_write_w(rv, addr, rv->V[ir->vs3 + j][i]);
                cnt += 1;
                i++;

                if (!(cnt % (VREG_U32_COUNT))) {
                    j++;
                    i = 0;
                }
                addr += 4;
            }
        }
    })

RVOP(
    vse64_v,
    { V_NOP; })

RVOP(
    vsseg2e8_v,
    { V_NOP; })

RVOP(
    vsseg3e8_v,
    { V_NOP; })

RVOP(
    vsseg4e8_v,
    { V_NOP; })

RVOP(
    vsseg5e8_v,
    { V_NOP; })

RVOP(
    vsseg6e8_v,
    { V_NOP; })

RVOP(
    vsseg7e8_v,
    { V_NOP; })

RVOP(
    vsseg8e8_v,
    { V_NOP; })

RVOP(
    vsseg2e16_v,
    { V_NOP; })

RVOP(
    vsseg3e16_v,
    { V_NOP; })

RVOP(
    vsseg4e16_v,
    { V_NOP; })

RVOP(
    vsseg5e16_v,
    { V_NOP; })

RVOP(
    vsseg6e16_v,
    { V_NOP; })

RVOP(
    vsseg7e16_v,
    { V_NOP; })

RVOP(
    vsseg8e16_v,
    { V_NOP; })

RVOP(
    vsseg2e32_v,
    { V_NOP; })

RVOP(
    vsseg3e32_v,
    { V_NOP; })

RVOP(
    vsseg4e32_v,
    { V_NOP; })

RVOP(
    vsseg5e32_v,
    { V_NOP; })

RVOP(
    vsseg6e32_v,
    { V_NOP; })

RVOP(
    vsseg7e32_v,
    { V_NOP; })

RVOP(
    vsseg8e32_v,
    { V_NOP; })

RVOP(
    vsseg2e64_v,
    { V_NOP; })

RVOP(
    vsseg3e64_v,
    { V_NOP; })

RVOP(
    vsseg4e64_v,
    { V_NOP; })

RVOP(
    vsseg5e64_v,
    { V_NOP; })

RVOP(
    vsseg6e64_v,
    { V_NOP; })

RVOP(
    vsseg7e64_v,
    { V_NOP; })

RVOP(
    vsseg8e64_v,
    { V_NOP; })

RVOP(
    vs1r_v,
    { V_NOP; })

RVOP(
    vs2r_v,
    { V_NOP; })

RVOP(
    vs4r_v,
    { V_NOP; })

RVOP(
    vs8r_v,
    { V_NOP; })

RVOP(
    vsm_v,
    { V_NOP; })

RVOP(
    vsuxei8_v,
    { V_NOP; })

RVOP(
    vsuxei16_v,
    { V_NOP; })

RVOP(
    vsuxei32_v,
    { V_NOP; })

RVOP(
    vsuxei64_v,
    { V_NOP; })

RVOP(
    vsuxseg2ei8_v,
    { V_NOP; })

RVOP(
    vsuxseg3ei8_v,
    { V_NOP; })

RVOP(
    vsuxseg4ei8_v,
    { V_NOP; })

RVOP(
    vsuxseg5ei8_v,
    { V_NOP; })

RVOP(
    vsuxseg6ei8_v,
    { V_NOP; })

RVOP(
    vsuxseg7ei8_v,
    { V_NOP; })

RVOP(
    vsuxseg8ei8_v,
    { V_NOP; })

RVOP(
    vsuxseg2ei16_v,
    { V_NOP; })

RVOP(
    vsuxseg3ei16_v,
    { V_NOP; })

RVOP(
    vsuxseg4ei16_v,
    { V_NOP; })

RVOP(
    vsuxseg5ei16_v,
    { V_NOP; })

RVOP(
    vsuxseg6ei16_v,
    { V_NOP; })

RVOP(
    vsuxseg7ei16_v,
    { V_NOP; })

RVOP(
    vsuxseg8ei16_v,
    { V_NOP; })

RVOP(
    vsuxseg2ei32_v,
    { V_NOP; })

RVOP(
    vsuxseg3ei32_v,
    { V_NOP; })

RVOP(
    vsuxseg4ei32_v,
    { V_NOP; })

RVOP(
    vsuxseg5ei32_v,
    { V_NOP; })

RVOP(
    vsuxseg6ei32_v,
    { V_NOP; })

RVOP(
    vsuxseg7ei32_v,
    { V_NOP; })

RVOP(
    vsuxseg8ei32_v,
    { V_NOP; })

RVOP(
    vsuxseg2ei64_v,
    { V_NOP; })

RVOP(
    vsuxseg3ei64_v,
    { V_NOP; })

RVOP(
    vsuxseg4ei64_v,
    { V_NOP; })

RVOP(
    vsuxseg5ei64_v,
    { V_NOP; })

RVOP(
    vsuxseg6ei64_v,
    { V_NOP; })

RVOP(
    vsuxseg7ei64_v,
    { V_NOP; })

RVOP(
    vsuxseg8ei64_v,
    { V_NOP; })

RVOP(
    vsse8_v,
    { V_NOP; })

RVOP(
    vsse16_v,
    { V_NOP; })

RVOP(
    vsse32_v,
    { V_NOP; })

RVOP(
    vsse64_v,
    { V_NOP; })

RVOP(
    vssseg2e8_v,
    { V_NOP; })

RVOP(
    vssseg3e8_v,
    { V_NOP; })

RVOP(
    vssseg4e8_v,
    { V_NOP; })

RVOP(
    vssseg5e8_v,
    { V_NOP; })

RVOP(
    vssseg6e8_v,
    { V_NOP; })

RVOP(
    vssseg7e8_v,
    { V_NOP; })

RVOP(
    vssseg8e8_v,
    { V_NOP; })

RVOP(
    vssseg2e16_v,
    { V_NOP; })

RVOP(
    vssseg3e16_v,
    { V_NOP; })

RVOP(
    vssseg4e16_v,
    { V_NOP; })

RVOP(
    vssseg5e16_v,
    { V_NOP; })

RVOP(
    vssseg6e16_v,
    { V_NOP; })

RVOP(
    vssseg7e16_v,
    { V_NOP; })

RVOP(
    vssseg8e16_v,
    { V_NOP; })

RVOP(
    vssseg2e32_v,
    { V_NOP; })

RVOP(
    vssseg3e32_v,
    { V_NOP; })

RVOP(
    vssseg4e32_v,
    { V_NOP; })

RVOP(
    vssseg5e32_v,
    { V_NOP; })

RVOP(
    vssseg6e32_v,
    { V_NOP; })

RVOP(
    vssseg7e32_v,
    { V_NOP; })

RVOP(
    vssseg8e32_v,
    { V_NOP; })

RVOP(
    vssseg2e64_v,
    { V_NOP; })

RVOP(
    vssseg3e64_v,
    { V_NOP; })

RVOP(
    vssseg4e64_v,
    { V_NOP; })

RVOP(
    vssseg5e64_v,
    { V_NOP; })

RVOP(
    vssseg6e64_v,
    { V_NOP; })

RVOP(
    vssseg7e64_v,
    { V_NOP; })

RVOP(
    vssseg8e64_v,
    { V_NOP; })

RVOP(
    vsoxei8_v,
    { V_NOP; })

RVOP(
    vsoxei16_v,
    { V_NOP; })

RVOP(
    vsoxei32_v,
    { V_NOP; })

RVOP(
    vsoxei64_v,
    { V_NOP; })

RVOP(
    vsoxseg2ei8_v,
    { V_NOP; })

RVOP(
    vsoxseg3ei8_v,
    { V_NOP; })

RVOP(
    vsoxseg4ei8_v,
    { V_NOP; })

RVOP(
    vsoxseg5ei8_v,
    { V_NOP; })

RVOP(
    vsoxseg6ei8_v,
    { V_NOP; })

RVOP(
    vsoxseg7ei8_v,
    { V_NOP; })

RVOP(
    vsoxseg8ei8_v,
    { V_NOP; })

RVOP(
    vsoxseg2ei16_v,
    { V_NOP; })

RVOP(
    vsoxseg3ei16_v,
    { V_NOP; })

RVOP(
    vsoxseg4ei16_v,
    { V_NOP; })

RVOP(
    vsoxseg5ei16_v,
    { V_NOP; })

RVOP(
    vsoxseg6ei16_v,
    { V_NOP; })

RVOP(
    vsoxseg7ei16_v,
    { V_NOP; })

RVOP(
    vsoxseg8ei16_v,
    { V_NOP; })

RVOP(
    vsoxseg2ei32_v,
    { V_NOP; })

RVOP(
    vsoxseg3ei32_v,
    { V_NOP; })

RVOP(
    vsoxseg4ei32_v,
    { V_NOP; })

RVOP(
    vsoxseg5ei32_v,
    { V_NOP; })

RVOP(
    vsoxseg6ei32_v,
    { V_NOP; })

RVOP(
    vsoxseg7ei32_v,
    { V_NOP; })

RVOP(
    vsoxseg8ei32_v,
    { V_NOP; })

RVOP(
    vsoxseg2ei64_v,
    { V_NOP; })

RVOP(
    vsoxseg3ei64_v,
    { V_NOP; })

RVOP(
    vsoxseg4ei64_v,
    { V_NOP; })

RVOP(
    vsoxseg5ei64_v,
    { V_NOP; })

RVOP(
    vsoxseg6ei64_v,
    { V_NOP; })

RVOP(
    vsoxseg7ei64_v,
    { V_NOP; })

RVOP(
    vsoxseg8ei64_v,
    { V_NOP; })

#define op_add(a, b) ((a) + (b))
RVOP(vadd_vv, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->vs1, add, VV)})

RVOP(vadd_vx, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->rs1, add, VX)})

RVOP(vadd_vi, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->imm, add, VI)})
#undef op_add

#define op_sub(a, b) ((a) - (b))
RVOP(vsub_vv, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->vs1, sub, VV)})

RVOP(vsub_vx, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->rs1, sub, VX)})
#undef op_sub

#define op_rsub(a, b) ((b) - (a))
RVOP(vrsub_vx, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->rs1, rsub, VX)})

RVOP(vrsub_vi, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->imm, rsub, VI)})
#undef op_rsub

RVOP(
    vminu_vv,
    { V_NOP; })

RVOP(
    vminu_vx,
    { V_NOP; })

RVOP(
    vmin_vv,
    { V_NOP; })

RVOP(
    vmin_vx,
    { V_NOP; })

RVOP(
    vmaxu_vv,
    { V_NOP; })

RVOP(
    vmaxu_vx,
    { V_NOP; })

RVOP(
    vmax_vv,
    { V_NOP; })

RVOP(
    vmax_vx,
    { V_NOP; })

#define op_and(a, b) ((a) & (b))
RVOP(vand_vv, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->vs1, and, VV)})

RVOP(vand_vx, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->rs1, and, VX)})

RVOP(vand_vi, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->imm, and, VI)})
#undef op_and

#define op_or(a, b) ((a) | (b))
RVOP(vor_vv, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->vs1, or, VV)})

RVOP(vor_vx, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->rs1, or, VX)})

RVOP(vor_vi, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->imm, or, VI)})
#undef op_or

#define op_xor(a, b) ((a) ^ (b))
RVOP(vxor_vv, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->vs1, xor, VV)})

RVOP(vxor_vx, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->rs1, xor, VX)})

RVOP(vxor_vi, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->imm, xor, VI)})
#undef op_xor

RVOP(
    vrgather_vv,
    { V_NOP; })

RVOP(
    vrgather_vx,
    { V_NOP; })

RVOP(
    vrgather_vi,
    { V_NOP; })

RVOP(
    vslideup_vx,
    { V_NOP; })

RVOP(
    vslideup_vi,
    { V_NOP; })

RVOP(
    vrgatherei16_vv,
    { V_NOP; })

RVOP(
    vslidedown_vx,
    { V_NOP; })

RVOP(
    vslidedown_vi,
    { V_NOP; })

RVOP(
    vadc_vvm,
    { V_NOP; })

RVOP(
    vadc_vxm,
    { V_NOP; })

RVOP(
    vadc_vim,
    { V_NOP; })

RVOP(
    vmadc_vv,
    { V_NOP; })

RVOP(
    vmadc_vx,
    { V_NOP; })

RVOP(
    vmadc_vi,
    { V_NOP; })

RVOP(
    vsbc_vvm,
    { V_NOP; })

RVOP(
    vsbc_vxm,
    { V_NOP; })

RVOP(
    vmsbc_vv,
    { V_NOP; })

RVOP(
    vmsbc_vx,
    { V_NOP; })

RVOP(
    vmerge_vvm,
    { V_NOP; })

RVOP(
    vmerge_vxm,
    { V_NOP; })

RVOP(
    vmerge_vim,
    { V_NOP; })

#define op_vmv(a, b) (((a) & 0) + (b))
RVOP(vmv_v_v, {VECTOR_DISPATCH(ir->vd, 0, ir->vs1, vmv, VV)})

RVOP(vmv_v_x, {VECTOR_DISPATCH(ir->vd, 0, ir->rs1, vmv, VX)})

RVOP(vmv_v_i, {VECTOR_DISPATCH(ir->vd, 0, ir->imm, vmv, VI)})
#undef op_vmv

RVOP(
    vmseq_vv,
    { V_NOP; })

RVOP(
    vmseq_vx,
    { V_NOP; })

RVOP(
    vmseq_vi,
    { V_NOP; })

RVOP(
    vmsne_vv,
    { V_NOP; })

RVOP(
    vmsne_vx,
    { V_NOP; })

RVOP(
    vmsne_vi,
    { V_NOP; })

RVOP(
    vmsltu_vv,
    { V_NOP; })

RVOP(
    vmsltu_vx,
    { V_NOP; })

RVOP(
    vmslt_vv,
    { V_NOP; })

RVOP(
    vmslt_vx,
    { V_NOP; })

RVOP(
    vmsleu_vv,
    { V_NOP; })

RVOP(
    vmsleu_vx,
    { V_NOP; })

RVOP(
    vmsleu_vi,
    { V_NOP; })

RVOP(
    vmsle_vv,
    { V_NOP; })

RVOP(
    vmsle_vx,
    { V_NOP; })

RVOP(
    vmsle_vi,
    { V_NOP; })

RVOP(
    vmsgtu_vx,
    { V_NOP; })

RVOP(
    vmsgtu_vi,
    { V_NOP; })

RVOP(
    vmsgt_vx,
    { V_NOP; })

RVOP(
    vmsgt_vi,
    { V_NOP; })

RVOP(
    vsaddu_vv,
    { V_NOP; })

RVOP(
    vsaddu_vx,
    { V_NOP; })

RVOP(
    vsaddu_vi,
    { V_NOP; })

RVOP(
    vsadd_vv,
    { V_NOP; })

RVOP(
    vsadd_vx,
    { V_NOP; })

RVOP(
    vsadd_vi,
    { V_NOP; })

RVOP(
    vssubu_vv,
    { V_NOP; })

RVOP(
    vssubu_vx,
    { V_NOP; })

RVOP(
    vssub_vv,
    { V_NOP; })

RVOP(
    vssub_vx,
    { V_NOP; })

#define op_sll(a, b) \
    ((a) << ((b) & ((8 << ((rv->csr_vtype >> 3) & 0b111)) - 1)))
RVOP(vsll_vv, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->vs1, sll, VV)})

RVOP(vsll_vx, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->rs1, sll, VX)})

RVOP(vsll_vi, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->imm, sll, VI)})
#undef op_sll

RVOP(
    vsmul_vv,
    { V_NOP; })

RVOP(
    vsmul_vx,
    { V_NOP; })

RVOP(
    vsrl_vv,
    { V_NOP; })

RVOP(
    vsrl_vx,
    { V_NOP; })

RVOP(
    vsrl_vi,
    { V_NOP; })

RVOP(
    vsra_vv,
    { V_NOP; })

RVOP(
    vsra_vx,
    { V_NOP; })

RVOP(
    vsra_vi,
    { V_NOP; })

RVOP(
    vssrl_vv,
    { V_NOP; })

RVOP(
    vssrl_vx,
    { V_NOP; })

RVOP(
    vssrl_vi,
    { V_NOP; })

RVOP(
    vssra_vv,
    { V_NOP; })

RVOP(
    vssra_vx,
    { V_NOP; })

RVOP(
    vssra_vi,
    { V_NOP; })

RVOP(
    vnsrl_wv,
    { V_NOP; })

RVOP(
    vnsrl_wx,
    { V_NOP; })

RVOP(
    vnsrl_wi,
    { V_NOP; })

RVOP(
    vnsra_wv,
    { V_NOP; })

RVOP(
    vnsra_wx,
    { V_NOP; })

RVOP(
    vnsra_wi,
    { V_NOP; })

RVOP(
    vnclipu_wv,
    { V_NOP; })

RVOP(
    vnclipu_wx,
    { V_NOP; })

RVOP(
    vnclipu_wi,
    { V_NOP; })

RVOP(
    vnclip_wv,
    { V_NOP; })

RVOP(
    vnclip_wx,
    { V_NOP; })

RVOP(
    vnclip_wi,
    { V_NOP; })

RVOP(
    vwredsumu_vs,
    { V_NOP; })

RVOP(
    vwredsum_vs,
    { V_NOP; })



RVOP(
    vredsum_vs,
    { V_NOP; })

RVOP(
    vredand_vs,
    { V_NOP; })

RVOP(
    vredor_vs,
    { V_NOP; })

RVOP(
    vredxor_vs,
    { V_NOP; })

RVOP(
    vredminu_vs,
    { V_NOP; })

RVOP(
    vredmin_vs,
    { V_NOP; })

RVOP(
    vredmaxu_vs,
    { V_NOP; })

RVOP(
    vredmax_vs,
    { V_NOP; })

RVOP(
    vaaddu_vv,
    { V_NOP; })

RVOP(
    vaaddu_vx,
    { V_NOP; })

RVOP(
    vaadd_vv,
    { V_NOP; })

RVOP(
    vaadd_vx,
    { V_NOP; })

RVOP(
    vasubu_vv,
    { V_NOP; })

RVOP(
    vasubu_vx,
    { V_NOP; })

RVOP(
    vasub_vv,
    { V_NOP; })

RVOP(
    vasub_vx,
    { V_NOP; })

RVOP(
    vslide1up_vx,
    { V_NOP; })

RVOP(
    vslide1down_vx,
    { V_NOP; })

RVOP(
    vcompress_vm,
    { V_NOP; })

RVOP(
    vmandn_mm,
    { V_NOP; })

RVOP(
    vmand_mm,
    { V_NOP; })

RVOP(
    vmor_mm,
    { V_NOP; })

RVOP(
    vmxor_mm,
    { V_NOP; })

RVOP(
    vmorn_mm,
    { V_NOP; })

RVOP(
    vmnand_mm,
    { V_NOP; })

RVOP(
    vmnor_mm,
    { V_NOP; })

RVOP(
    vmxnor_mm,
    { V_NOP; })

RVOP(
    vdivu_vv,
    { V_NOP; })

RVOP(
    vdivu_vx,
    { V_NOP; })

RVOP(
    vdiv_vv,
    { V_NOP; })

RVOP(
    vdiv_vx,
    { V_NOP; })

RVOP(
    vremu_vv,
    { V_NOP; })

RVOP(
    vremu_vx,
    { V_NOP; })

RVOP(
    vrem_vv,
    { V_NOP; })

RVOP(
    vrem_vx,
    { V_NOP; })

RVOP(
    vmulhu_vv,
    { V_NOP; })

RVOP(
    vmulhu_vx,
    { V_NOP; })

#define op_mul(a, b) ((a) * (b))
RVOP(vmul_vv, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->vs1, mul, VV)})

RVOP(vmul_vx, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->rs1, mul, VX)})
#undef op_mul

RVOP(
    vmulhsu_vv,
    { V_NOP; })

RVOP(
    vmulhsu_vx,
    { V_NOP; })

RVOP(
    vmulh_vv,
    { V_NOP; })

RVOP(
    vmulh_vx,
    { V_NOP; })

RVOP(
    vmadd_vv,
    { V_NOP; })

RVOP(
    vmadd_vx,
    { V_NOP; })

RVOP(
    vnmsub_vv,
    { V_NOP; })

RVOP(
    vnmsub_vx,
    { V_NOP; })

RVOP(
    vmacc_vv,
    { V_NOP; })

RVOP(
    vmacc_vx,
    { V_NOP; })

RVOP(
    vnmsac_vv,
    { V_NOP; })

RVOP(
    vnmsac_vx,
    { V_NOP; })

RVOP(
    vwaddu_vv,
    { V_NOP; })

RVOP(
    vwaddu_vx,
    { V_NOP; })

RVOP(
    vwadd_vv,
    { V_NOP; })

RVOP(
    vwadd_vx,
    { V_NOP; })

RVOP(
    vwsubu_vv,
    { V_NOP; })

RVOP(
    vwsubu_vx,
    { V_NOP; })

RVOP(
    vwsub_vv,
    { V_NOP; })

RVOP(
    vwsub_vx,
    { V_NOP; })

RVOP(
    vwaddu_wv,
    { V_NOP; })

RVOP(
    vwaddu_wx,
    { V_NOP; })

RVOP(
    vwadd_wv,
    { V_NOP; })

RVOP(
    vwadd_wx,
    { V_NOP; })

RVOP(
    vwsubu_wv,
    { V_NOP; })

RVOP(
    vwsubu_wx,
    { V_NOP; })

RVOP(
    vwsub_wv,
    { V_NOP; })

RVOP(
    vwsub_wx,
    { V_NOP; })

RVOP(
    vwmulu_vv,
    { V_NOP; })

RVOP(
    vwmulu_vx,
    { V_NOP; })

RVOP(
    vwmulsu_vv,
    { V_NOP; })

RVOP(
    vwmulsu_vx,
    { V_NOP; })

RVOP(
    vwmul_vv,
    { V_NOP; })

RVOP(
    vwmul_vx,
    { V_NOP; })

RVOP(
    vwmaccu_vv,
    { V_NOP; })

RVOP(
    vwmaccu_vx,
    { V_NOP; })

RVOP(
    vwmacc_vv,
    { V_NOP; })

RVOP(
    vwmacc_vx,
    { V_NOP; })

RVOP(
    vwmaccus_vx,
    { V_NOP; })

RVOP(
    vwmaccsu_vv,
    { V_NOP; })

RVOP(
    vwmaccsu_vx,
    { V_NOP; })

RVOP(
    vmv_s_x,
    { V_NOP; })

RVOP(
    vmv_x_s,
    { V_NOP; })

RVOP(
    vcpop_m,
    { V_NOP; })

RVOP(
    vfirst_m,
    { V_NOP; })

RVOP(
    vmsbf_m,
    { V_NOP; })

RVOP(
    vmsof_m,
    { V_NOP; })

RVOP(
    vmsif_m,
    { V_NOP; })

RVOP(
    viota_m,
    { V_NOP; })

RVOP(
    vid_v,
    { V_NOP; })


RVOP(
    vfadd_vv,
    { V_NOP; })

RVOP(
    vfadd_vf,
    { V_NOP; })

RVOP(
    vfredusum_vs,
    { V_NOP; })

RVOP(
    vfsub_vv,
    { V_NOP; })

RVOP(
    vfsub_vf,
    { V_NOP; })

RVOP(
    vfredosum_vs,
    { V_NOP; })

RVOP(
    vfmin_vv,
    { V_NOP; })

RVOP(
    vfmin_vf,
    { V_NOP; })

RVOP(
    vfredmin_vs,
    { V_NOP; })

RVOP(
    vfmax_vv,
    { V_NOP; })

RVOP(
    vfmax_vf,
    { V_NOP; })

RVOP(
    vfredmax_vs,
    { V_NOP; })

RVOP(
    vfsgnj_vv,
    { V_NOP; })

RVOP(
    vfsgnj_vf,
    { V_NOP; })

RVOP(
    vfsgnjn_vv,
    { V_NOP; })

RVOP(
    vfsgnjn_vf,
    { V_NOP; })

RVOP(
    vfsgnjx_vv,
    { V_NOP; })

RVOP(
    vfsgnjx_vf,
    { V_NOP; })

RVOP(
    vfslide1up_vf,
    { V_NOP; })

RVOP(
    vfslide1down_vf,
    { V_NOP; })

RVOP(
    vfmerge_vfm,
    { V_NOP; })

RVOP(
    vfmv_v_f,
    { V_NOP; })

RVOP(
    vmfeq_vv,
    { V_NOP; })

RVOP(
    vmfeq_vf,
    { V_NOP; })

RVOP(
    vmfle_vv,
    { V_NOP; })

RVOP(
    vmfle_vf,
    { V_NOP; })

RVOP(
    vmflt_vv,
    { V_NOP; })

RVOP(
    vmflt_vf,
    { V_NOP; })

RVOP(
    vmfne_vv,
    { V_NOP; })

RVOP(
    vmfne_vf,
    { V_NOP; })

RVOP(
    vmfgt_vf,
    { V_NOP; })

RVOP(
    vmfge_vf,
    { V_NOP; })

RVOP(
    vfdiv_vv,
    { V_NOP; })

RVOP(
    vfdiv_vf,
    { V_NOP; })

RVOP(
    vfrdiv_vf,
    { V_NOP; })

RVOP(
    vfmul_vv,
    { V_NOP; })

RVOP(
    vfmul_vf,
    { V_NOP; })

RVOP(
    vfrsub_vf,
    { V_NOP; })

RVOP(
    vfmadd_vv,
    { V_NOP; })

RVOP(
    vfmadd_vf,
    { V_NOP; })

RVOP(
    vfnmadd_vv,
    { V_NOP; })

RVOP(
    vfnmadd_vf,
    { V_NOP; })

RVOP(
    vfmsub_vv,
    { V_NOP; })

RVOP(
    vfmsub_vf,
    { V_NOP; })

RVOP(
    vfnmsub_vv,
    { V_NOP; })

RVOP(
    vfnmsub_vf,
    { V_NOP; })

RVOP(
    vfmacc_vv,
    { V_NOP; })

RVOP(
    vfmacc_vf,
    { V_NOP; })

RVOP(
    vfnmacc_vv,
    { V_NOP; })

RVOP(
    vfnmacc_vf,
    { V_NOP; })

RVOP(
    vfmsac_vv,
    { V_NOP; })

RVOP(
    vfmsac_vf,
    { V_NOP; })

RVOP(
    vfnmsac_vv,
    { V_NOP; })

RVOP(
    vfnmsac_vf,
    { V_NOP; })

RVOP(
    vfwadd_vv,
    { V_NOP; })

RVOP(
    vfwadd_vf,
    { V_NOP; })

RVOP(
    vfwredusum_vs,
    { V_NOP; })

RVOP(
    vfwsub_vv,
    { V_NOP; })

RVOP(
    vfwsub_vf,
    { V_NOP; })

RVOP(
    vfwredosum_vs,
    { V_NOP; })

RVOP(
    vfwadd_wv,
    { V_NOP; })

RVOP(
    vfwadd_wf,
    { V_NOP; })

RVOP(
    vfwsub_wv,
    { V_NOP; })

RVOP(
    vfwsub_wf,
    { V_NOP; })

RVOP(
    vfwmul_vv,
    { V_NOP; })

RVOP(
    vfwmul_vf,
    { V_NOP; })

RVOP(
    vfwmacc_vv,
    { V_NOP; })

RVOP(
    vfwmacc_vf,
    { V_NOP; })

RVOP(
    vfwnmacc_vv,
    { V_NOP; })

RVOP(
    vfwnmacc_vf,
    { V_NOP; })

RVOP(
    vfwmsac_vv,
    { V_NOP; })

RVOP(
    vfwmsac_vf,
    { V_NOP; })

RVOP(
    vfwnmsac_vv,
    { V_NOP; })

RVOP(
    vfwnmsac_vf,
    { V_NOP; })

