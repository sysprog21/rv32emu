/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* RISC-V "V" Vector Extension - SEW=64 element operations.
 *
 * Included from rv32_v_template.c. The handlers in that file work on 32-bit
 * element values, which covers SEW of 8, 16, and 32. With SEW=64 the common
 * prologue calls rvv_exec_sew64() first; it executes the opcodes whose result
 * depends on the element width and leaves the rest (loads, stores, mask
 * logic, vset*) to their own handlers, which do not depend on SEW.
 *
 * Elements are 64 bits wide and the scalar operand is XLEN=32 bits, so a
 * scalar is sign-extended to SEW (V 1.0 5.4), vmv.x.s returns the low XLEN
 * bits (16.1), and the high halves of products and the 65-bit sums of the
 * averaging adds use __int128.
 *
 * Widening and narrowing forms, whose 2*SEW would be 128 bits, are rejected
 * by the shared legality check before they get here.
 */

typedef unsigned __int128 rvv_u128;
typedef __int128 rvv_s128;

typedef uint64_t (*rvv64_fn)(riscv_t *rv, uint64_t acc, uint64_t a, uint64_t b);

/* Where the second operand comes from, and how to extend an immediate. */
enum { RVV64_VV, RVV64_VX, RVV64_VI, RVV64_VUI, RVV64_VF };

static inline uint64_t rvv64_get(const riscv_t *rv, uint32_t reg, uint32_t elem)
{
    return rvv_get_elem_ext(rv, reg, elem, 64);
}

static inline void rvv64_set(riscv_t *rv,
                             uint32_t reg,
                             uint32_t elem,
                             uint64_t value)
{
    rvv_set_elem_ext(rv, reg, elem, 64, value);
}

static inline uint64_t rvv64_operand(const riscv_t *rv,
                                     const rv_insn_t *ir,
                                     int form,
                                     uint32_t elem)
{
    switch (form) {
    case RVV64_VV:
        return rvv64_get(rv, ir->vs1, elem);
    case RVV64_VX:
        return (uint64_t) (int64_t) (int32_t) rv->X[ir->rs1];
    case RVV64_VUI:
        return (uint64_t) ((uint32_t) ir->imm & 0x1fU);
#if RV32_HAS(EXT_D)
    case RVV64_VF:
        return f64_reg(rv, ir->rs1).v; /* a float register is 64 bits */
#endif
    default:
        return (uint64_t) (int64_t) ir->imm;
    }
}

/* Shift a wide value right by shift (< 64) and add the vxrm rounding carry,
 * which rvv_round_increment() derives from the discarded low bits.
 */
static inline rvv_u128 rvv64_roundoff_u(riscv_t *rv, rvv_u128 v, uint32_t shift)
{
    uint64_t low = (uint64_t) v;
    uint64_t carry;

    if (!shift)
        return v;
    carry = ((low & ((UINT64_C(1) << shift) - 1U)) +
             rvv_round_increment(rv, low, shift)) >>
            shift;
    return (v >> shift) + carry;
}

static inline rvv_s128 rvv64_roundoff_s(riscv_t *rv, rvv_s128 v, uint32_t shift)
{
    uint64_t low = (uint64_t) v;
    uint64_t carry;

    if (!shift)
        return v;
    carry = ((low & ((UINT64_C(1) << shift) - 1U)) +
             rvv_round_increment(rv, low, shift)) >>
            shift;
    return (v >> shift) + (rvv_s128) carry;
}

/* Element operations. acc is the old destination element, a is vs2, and b is
 * vs1, the scalar, or the immediate.
 */
#define RVV64_OP(name, expr)                                               \
    static uint64_t rvv64_op_##name(riscv_t *rv, uint64_t acc, uint64_t a, \
                                    uint64_t b)                            \
    {                                                                      \
        (void) rv;                                                         \
        (void) acc;                                                        \
        (void) a;                                                          \
        (void) b;                                                          \
        return (expr);                                                     \
    }

#define S64(x) ((int64_t) (x))

RVV64_OP(add, a + b)
RVV64_OP(sub, a - b)
RVV64_OP(rsub, b - a)
RVV64_OP(and, a &b)
RVV64_OP(or, a | b)
RVV64_OP(xor, a ^ b)
RVV64_OP(minu, a < b ? a : b)
RVV64_OP(maxu, a > b ? a : b)
RVV64_OP(min, S64(a) < S64(b) ? a : b)
RVV64_OP(max, S64(a) > S64(b) ? a : b)
RVV64_OP(sll, a << (b & 63))
RVV64_OP(srl, a >> (b & 63))
RVV64_OP(sra, (uint64_t) (S64(a) >> (b & 63)))
RVV64_OP(mul, a *b)
RVV64_OP(mulhu, (uint64_t) (((rvv_u128) a * b) >> 64))
RVV64_OP(mulh, (uint64_t) (((rvv_s128) S64(a) * S64(b)) >> 64))
RVV64_OP(mulhsu, (uint64_t) (((rvv_s128) S64(a) * (rvv_s128) b) >> 64))
RVV64_OP(divu, b ? a / b : UINT64_MAX)
RVV64_OP(remu, b ? a % b : a)
RVV64_OP(div,
         !b                                      ? UINT64_MAX
         : (S64(a) == INT64_MIN && S64(b) == -1) ? a
                                                 : (uint64_t) (S64(a) / S64(b)))
RVV64_OP(rem,
         !b                                      ? a
         : (S64(a) == INT64_MIN && S64(b) == -1) ? 0
                                                 : (uint64_t) (S64(a) % S64(b)))
/* Multiply-add forms: vmacc/vnmsac accumulate into vd, vmadd/vnmsub multiply
 * it. For the .vv forms b is vs1 and for .vx it is rs1.
 */
RVV64_OP(macc, acc + b * a)
RVV64_OP(nmsac, acc - b * a)
RVV64_OP(madd, b *acc + a)
RVV64_OP(nmsub, a - b * acc)

static uint64_t rvv64_op_saddu(riscv_t *rv,
                               uint64_t acc,
                               uint64_t a,
                               uint64_t b)
{
    uint64_t sum = a + b;

    (void) acc;
    if (sum < a) {
        rvv_set_vxsat(rv);
        return UINT64_MAX;
    }
    return sum;
}

static uint64_t rvv64_op_sadd(riscv_t *rv, uint64_t acc, uint64_t a, uint64_t b)
{
    int64_t sum;

    (void) acc;
    if (__builtin_add_overflow(S64(a), S64(b), &sum)) {
        rvv_set_vxsat(rv);
        return S64(a) < 0 ? (uint64_t) INT64_MIN : (uint64_t) INT64_MAX;
    }
    return (uint64_t) sum;
}

static uint64_t rvv64_op_ssubu(riscv_t *rv,
                               uint64_t acc,
                               uint64_t a,
                               uint64_t b)
{
    (void) acc;
    if (a < b) {
        rvv_set_vxsat(rv);
        return 0;
    }
    return a - b;
}

static uint64_t rvv64_op_ssub(riscv_t *rv, uint64_t acc, uint64_t a, uint64_t b)
{
    int64_t diff;

    (void) acc;
    if (__builtin_sub_overflow(S64(a), S64(b), &diff)) {
        rvv_set_vxsat(rv);
        return S64(a) < 0 ? (uint64_t) INT64_MIN : (uint64_t) INT64_MAX;
    }
    return (uint64_t) diff;
}

static uint64_t rvv64_op_aaddu(riscv_t *rv,
                               uint64_t acc,
                               uint64_t a,
                               uint64_t b)
{
    (void) acc;
    return (uint64_t) rvv64_roundoff_u(rv, (rvv_u128) a + b, 1);
}

static uint64_t rvv64_op_asubu(riscv_t *rv,
                               uint64_t acc,
                               uint64_t a,
                               uint64_t b)
{
    (void) acc;
    /* The difference needs 65 bits; its low 65 bits are what is shifted. */
    return (uint64_t) rvv64_roundoff_u(
        rv, ((rvv_u128) a - b) & (((rvv_u128) 1 << 65) - 1), 1);
}

static uint64_t rvv64_op_aadd(riscv_t *rv, uint64_t acc, uint64_t a, uint64_t b)
{
    (void) acc;
    return (uint64_t) rvv64_roundoff_s(rv, (rvv_s128) S64(a) + S64(b), 1);
}

static uint64_t rvv64_op_asub(riscv_t *rv, uint64_t acc, uint64_t a, uint64_t b)
{
    (void) acc;
    return (uint64_t) rvv64_roundoff_s(rv, (rvv_s128) S64(a) - S64(b), 1);
}

static uint64_t rvv64_op_ssrl(riscv_t *rv, uint64_t acc, uint64_t a, uint64_t b)
{
    (void) acc;
    return (uint64_t) rvv64_roundoff_u(rv, a, (uint32_t) (b & 63));
}

static uint64_t rvv64_op_ssra(riscv_t *rv, uint64_t acc, uint64_t a, uint64_t b)
{
    (void) acc;
    return (uint64_t) rvv64_roundoff_s(rv, S64(a), (uint32_t) (b & 63));
}

static uint64_t rvv64_op_smul(riscv_t *rv, uint64_t acc, uint64_t a, uint64_t b)
{
    rvv_s128 prod = (rvv_s128) S64(a) * S64(b);
    rvv_s128 rounded = rvv64_roundoff_s(rv, prod, 63);

    (void) acc;
    if (rounded > INT64_MAX) {
        rvv_set_vxsat(rv);
        return (uint64_t) INT64_MAX;
    }
    return (uint64_t) rounded;
}

/* Mask-producing comparisons. */
typedef bool (*rvv64_cmp_fn)(uint64_t a, uint64_t b);
#define RVV64_CMP(name, expr)                            \
    static bool rvv64_cmp_##name(uint64_t a, uint64_t b) \
    {                                                    \
        return (expr);                                   \
    }
RVV64_CMP(eq, a == b)
RVV64_CMP(ne, a != b)
RVV64_CMP(ltu, a < b)
RVV64_CMP(lt, S64(a) < S64(b))
RVV64_CMP(leu, a <= b)
RVV64_CMP(le, S64(a) <= S64(b))
RVV64_CMP(gtu, a > b)
RVV64_CMP(gt, S64(a) > S64(b))

/* Result of an element operation written to a data register under the usual
 * mask and tail policies.
 */
static void rvv64_exec_arith(riscv_t *rv,
                             const rv_insn_t *ir,
                             uint32_t dest,
                             int form,
                             rvv64_fn fn)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv64_set(rv, dest, elem, UINT64_MAX);
            continue;
        }
        rvv64_set(
            rv, dest, elem,
            fn(rv, rvv64_get(rv, dest, elem), rvv64_get(rv, ir->vs2, elem),
               rvv64_operand(rv, ir, form, elem)));
    }
    if (rvv_tail_fill(rv, vta)) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv64_set(rv, dest, elem, UINT64_MAX);
    }
    rv->csr_vstart = 0;
}

static void rvv64_exec_compare(riscv_t *rv,
                               const rv_insn_t *ir,
                               int form,
                               rvv64_cmp_fn cmp)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_mask_set_bit(rv, ir->vd, elem, true);
            continue;
        }
        rvv_mask_set_bit(rv, ir->vd, elem,
                         cmp(rvv64_get(rv, ir->vs2, elem),
                             rvv64_operand(rv, ir, form, elem)));
    }
    rvv_mask_fill_tail(rv, ir->vd, rv->csr_vl, vlmax);
    rv->csr_vstart = 0;
}

/* vadc/vsbc: the mask register is the carry or borrow, not a predicate. */
static void rvv64_exec_carry(riscv_t *rv,
                             const rv_insn_t *ir,
                             int form,
                             bool subtract)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint64_t a = rvv64_get(rv, ir->vs2, elem);
        uint64_t b = rvv64_operand(rv, ir, form, elem);
        uint64_t c = rvv_mask_get_bit(rv, 0, elem);

        rvv64_set(rv, ir->vd, elem, subtract ? a - b - c : a + b + c);
    }
    if (rvv_tail_fill(rv, vta)) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv64_set(rv, ir->vd, elem, UINT64_MAX);
    }
    rv->csr_vstart = 0;
}

/* vmadc/vmsbc: carry or borrow out as a mask; the mask register, when used,
 * is the carry in.
 */
static void rvv64_exec_carry_out(riscv_t *rv,
                                 const rv_insn_t *ir,
                                 int form,
                                 bool subtract)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        rvv_u128 a = rvv64_get(rv, ir->vs2, elem);
        rvv_u128 b = rvv64_operand(rv, ir, form, elem);
        rvv_u128 c = ir->vm ? 0 : rvv_mask_get_bit(rv, 0, elem);
        bool out = subtract ? (a < b + c) : ((a + b + c) >> 64) != 0;

        rvv_mask_set_bit(rv, ir->vd, elem, out);
    }
    rvv_mask_fill_tail(rv, ir->vd, rv->csr_vl, vlmax);
    rv->csr_vstart = 0;
}

/* vmerge and vmv.v: vd = (v0 ? b : vs2) and vd = b. */
static void rvv64_exec_merge(riscv_t *rv,
                             const rv_insn_t *ir,
                             int form,
                             bool move)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint64_t b = rvv64_operand(rv, ir, form, elem);

        rvv64_set(rv, ir->vd, elem,
                  (move || rvv_mask_get_bit(rv, 0, elem))
                      ? b
                      : rvv64_get(rv, ir->vs2, elem));
    }
    if (rvv_tail_fill(rv, vta)) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv64_set(rv, ir->vd, elem, UINT64_MAX);
    }
    rv->csr_vstart = 0;
}

static void rvv64_exec_reduction(riscv_t *rv, const rv_insn_t *ir, rvv64_fn fn)
{
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint64_t acc;

    if (rv->csr_vl == 0) {
        rv->csr_vstart = 0;
        return;
    }
    acc = rvv64_get(rv, ir->vs1, 0);
    for (uint32_t elem = 0; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem))
            continue;
        acc = fn(rv, 0, acc, rvv64_get(rv, ir->vs2, elem));
    }
    rvv64_set(rv, ir->vd, 0, acc);
    if (vta) {
        for (uint32_t elem = 1; elem < VLEN / 64; elem++)
            rvv64_set(rv, ir->vd, elem, UINT64_MAX);
    }
    rv->csr_vstart = 0;
}

static void rvv64_exec_slide(riscv_t *rv,
                             const rv_insn_t *ir,
                             bool up,
                             bool by_one,
                             uint32_t offset,
                             uint64_t scalar)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t first = rv->csr_vstart;

    if (by_one)
        offset = 1;
    /* Elements below the offset keep their old value; vslide1up writes from
     * the start because element 0 takes the scalar.
     */
    if (up && !by_one && offset > first)
        first = offset;
    for (uint32_t elem = first; elem < rv->csr_vl; elem++) {
        uint64_t value;

        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv64_set(rv, ir->vd, elem, UINT64_MAX);
            continue;
        }
        if (up) {
            value = (by_one && elem == 0)
                        ? scalar
                        : rvv64_get(rv, ir->vs2, elem - offset);
        } else if (by_one) {
            value = (elem + 1 == rv->csr_vl) ? scalar
                                             : rvv64_get(rv, ir->vs2, elem + 1);
        } else {
            uint64_t src = (uint64_t) elem + offset;

            value = src >= vlmax ? 0 : rvv64_get(rv, ir->vs2, (uint32_t) src);
        }
        rvv64_set(rv, ir->vd, elem, value);
    }
    if (rvv_tail_fill(rv, vta)) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv64_set(rv, ir->vd, elem, UINT64_MAX);
    }
    rv->csr_vstart = 0;
}

static void rvv64_exec_gather(riscv_t *rv,
                              const rv_insn_t *ir,
                              int form,
                              uint32_t index_eew)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint64_t index;

        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv64_set(rv, ir->vd, elem, UINT64_MAX);
            continue;
        }
        if (form == RVV64_VV)
            index = rvv_get_elem_ext(rv, ir->vs1, elem, index_eew);
        else if (form == RVV64_VX)
            index = rv->X[ir->rs1];
        else
            index = (uint32_t) ir->imm & 0x1fU;
        rvv64_set(
            rv, ir->vd, elem,
            index >= vlmax ? 0 : rvv64_get(rv, ir->vs2, (uint32_t) index));
    }
    if (rvv_tail_fill(rv, vta)) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv64_set(rv, ir->vd, elem, UINT64_MAX);
    }
    rv->csr_vstart = 0;
}

static void rvv64_exec_compress(riscv_t *rv, const rv_insn_t *ir)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t out = 0;

    for (uint32_t elem = 0; elem < rv->csr_vl; elem++) {
        if (rvv_mask_get_bit(rv, ir->vs1, elem))
            rvv64_set(rv, ir->vd, out++, rvv64_get(rv, ir->vs2, elem));
    }
    if (vta && rv->csr_vl) {
        for (uint32_t elem = out; elem < vlmax; elem++)
            rvv64_set(rv, ir->vd, elem, UINT64_MAX);
    }
    rv->csr_vstart = 0;
}

/* viota.m and vid.v. */
static void rvv64_exec_index(riscv_t *rv, const rv_insn_t *ir, bool iota)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t dest = iota ? ir->rd : ir->vd;
    uint32_t count = 0;

    /* Rebuild the running count over the prestart elements without writing. */
    if (iota) {
        for (uint32_t elem = 0; elem < rv->csr_vstart; elem++) {
            if (rvv_mask_enabled_for_elem(rv, ir, elem) &&
                rvv_mask_get_bit(rv, ir->vs2, elem))
                count++;
        }
    }
    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv64_set(rv, dest, elem, UINT64_MAX);
            continue;
        }
        rvv64_set(rv, dest, elem, iota ? count : elem);
        if (iota && rvv_mask_get_bit(rv, ir->vs2, elem))
            count++;
    }
    if (rvv_tail_fill(rv, vta)) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv64_set(rv, dest, elem, UINT64_MAX);
    }
    rv->csr_vstart = 0;
}

/* vzext.vfN and vsext.vfN: the source elements are SEW/N bits wide. */
static void rvv64_exec_extend(riscv_t *rv,
                              const rv_insn_t *ir,
                              uint32_t factor,
                              bool sign)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t eew = 64 / factor;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint64_t v;

        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv64_set(rv, ir->vd, elem, UINT64_MAX);
            continue;
        }
        v = rvv_get_elem_ext(rv, ir->vs2, elem, eew);
        if (sign)
            v = (uint64_t) rvv_sign_extend64(v, eew);
        rvv64_set(rv, ir->vd, elem, v);
    }
    if (rvv_tail_fill(rv, vta)) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv64_set(rv, ir->vd, elem, UINT64_MAX);
    }
    rv->csr_vstart = 0;
}

/* Register-group checks shared by the handlers below. */
static inline bool rvv64_group_ok(const riscv_t *rv, uint32_t reg)
{
    return rvv_validate_data_reg(rv->csr_vtype, reg);
}

static inline bool rvv64_overlap(const riscv_t *rv, uint32_t a, uint32_t b)
{
    uint32_t n = rvv_group_regs(rv->csr_vtype);

    return rvv_reg_spans_overlap(a, n, b, n);
}

/* Arithmetic over vd, vs2, and (for .vv) vs1, all single-width. */
static int rvv64_arith(riscv_t *rv, const rv_insn_t *ir, int form, rvv64_fn fn)
{
    if (!rvv64_group_ok(rv, ir->vd) || !rvv64_group_ok(rv, ir->vs2) ||
        (form == RVV64_VV && !rvv64_group_ok(rv, ir->vs1)))
        return RVV_EXEC_TRAP;
    rvv64_exec_arith(rv, ir, ir->vd, form, fn);
    return RVV_EXEC_DONE;
}

static int rvv64_compare(riscv_t *rv,
                         const rv_insn_t *ir,
                         int form,
                         rvv64_cmp_fn cmp)
{
    if (!rvv64_group_ok(rv, ir->vs2) ||
        (form == RVV64_VV && !rvv64_group_ok(rv, ir->vs1)))
        return RVV_EXEC_TRAP;
    rvv64_exec_compare(rv, ir, form, cmp);
    return RVV_EXEC_DONE;
}

static int rvv64_carry(riscv_t *rv,
                       const rv_insn_t *ir,
                       int form,
                       bool subtract,
                       bool carry_out)
{
    if (!rvv64_group_ok(rv, ir->vs2) ||
        (form == RVV64_VV && !rvv64_group_ok(rv, ir->vs1)) ||
        (!carry_out && !rvv64_group_ok(rv, ir->vd)))
        return RVV_EXEC_TRAP;
    if (carry_out)
        rvv64_exec_carry_out(rv, ir, form, subtract);
    else
        rvv64_exec_carry(rv, ir, form, subtract);
    return RVV_EXEC_DONE;
}

static int rvv64_reduction(riscv_t *rv, const rv_insn_t *ir, rvv64_fn fn)
{
    if (rv->csr_vstart || !rvv64_group_ok(rv, ir->vs2))
        return RVV_EXEC_TRAP;
    rvv64_exec_reduction(rv, ir, fn);
    return RVV_EXEC_DONE;
}

static int rvv64_slide(riscv_t *rv,
                       const rv_insn_t *ir,
                       bool up,
                       bool by_one,
                       uint32_t offset,
                       uint64_t scalar)
{
    if (!rvv64_group_ok(rv, ir->vd) || !rvv64_group_ok(rv, ir->vs2) ||
        (up && rvv64_overlap(rv, ir->vd, ir->vs2)))
        return RVV_EXEC_TRAP;
    rvv64_exec_slide(rv, ir, up, by_one, offset, scalar);
    return RVV_EXEC_DONE;
}

static int rvv64_gather(riscv_t *rv,
                        const rv_insn_t *ir,
                        int form,
                        uint32_t index_eew)
{
    uint32_t n = rvv_group_regs(rv->csr_vtype);
    uint32_t span = n;

    if (index_eew != 64 && !rvv_index_group_span(rv, index_eew, &span))
        return RVV_EXEC_TRAP;
    if (!rvv64_group_ok(rv, ir->vd) || !rvv64_group_ok(rv, ir->vs2) ||
        rvv64_overlap(rv, ir->vd, ir->vs2) ||
        (form == RVV64_VV &&
         (!rvv_validate_reg_span(ir->vs1, span) ||
          rvv_reg_spans_overlap(ir->vd, n, ir->vs1, span) ||
          /* The index is read at EEW, the data at SEW. */
          (index_eew != 64 &&
           rvv_reg_spans_overlap(ir->vs1, span, ir->vs2, n)))))
        return RVV_EXEC_TRAP;
    rvv64_exec_gather(rv, ir, form, index_eew);
    return RVV_EXEC_DONE;
}

static int rvv64_extend(riscv_t *rv,
                        const rv_insn_t *ir,
                        uint32_t factor,
                        bool sign)
{
    uint32_t num, den, src_span;
    bool at_least_one;

    rvv_lmul_ratio(rv->csr_vtype, &num, &den);
    /* Source EMUL = LMUL / factor must be at least 1/8. */
    if (den * factor > 8 * num)
        return RVV_EXEC_TRAP;
    at_least_one = num >= den * factor;
    src_span = at_least_one ? num / (den * factor) : 1;
    if (!rvv64_group_ok(rv, ir->vd) ||
        !rvv_validate_reg_span(ir->vs2, src_span) ||
        rvv_widen_overlap_illegal(ir->vd, rvv_group_regs(rv->csr_vtype),
                                  ir->vs2, src_span, at_least_one))
        return RVV_EXEC_TRAP;
    rvv64_exec_extend(rv, ir, factor, sign);
    return RVV_EXEC_DONE;
}

#if RV32_HAS(EXT_D)
/* Double-precision operations. The element is the raw binary64 pattern. */
static inline softfloat_float64_t rvv64_f(uint64_t bits)
{
    softfloat_float64_t value;

    value.v = bits;
    return value;
}

#define RVV64_FOP(name, expr)                                              \
    static uint64_t rvv64_op_##name(riscv_t *rv, uint64_t acc, uint64_t a, \
                                    uint64_t b)                            \
    {                                                                      \
        const softfloat_float64_t fa = rvv64_f(a);                         \
        const softfloat_float64_t fb = rvv64_f(b);                         \
        const softfloat_float64_t fc = rvv64_f(acc);                       \
        (void) rv;                                                         \
        (void) fa;                                                         \
        (void) fb;                                                         \
        (void) fc;                                                         \
        return (expr);                                                     \
    }

#define NEG(x) (rvv64_f((x).v ^ FMASK64_SIGN))

RVV64_FOP(fadd, f64_add(fa, fb).v)
RVV64_FOP(fsub, f64_sub(fa, fb).v)
RVV64_FOP(frsub, f64_sub(fb, fa).v)
RVV64_FOP(fmul, f64_mul(fa, fb).v)
RVV64_FOP(fdiv, f64_div(fa, fb).v)
RVV64_FOP(frdiv, f64_div(fb, fa).v)
RVV64_FOP(fsgnj, (a & ~FMASK64_SIGN) | (b & FMASK64_SIGN))
RVV64_FOP(fsgnjn, (a & ~FMASK64_SIGN) | (~b & FMASK64_SIGN))
RVV64_FOP(fsgnjx, a ^ (b & FMASK64_SIGN))
/* vd = +-(vs1 * vs2) +- vd, and vd = +-(vs1 * vd) +- vs2. */
RVV64_FOP(fmacc, f64_mulAdd(fb, fa, fc).v)
RVV64_FOP(fnmacc, f64_mulAdd(NEG(fb), fa, NEG(fc)).v)
RVV64_FOP(fmsac, f64_mulAdd(fb, fa, NEG(fc)).v)
RVV64_FOP(fnmsac, f64_mulAdd(NEG(fb), fa, fc).v)
RVV64_FOP(fmadd, f64_mulAdd(fb, fc, fa).v)
RVV64_FOP(fnmadd, f64_mulAdd(NEG(fb), fc, NEG(fa)).v)
RVV64_FOP(fmsub, f64_mulAdd(fb, fc, NEG(fa)).v)
RVV64_FOP(fnmsub, f64_mulAdd(NEG(fb), fc, fa).v)

/* fmin and fmax follow the scalar instructions: a NaN loses to a number, two
 * NaNs give the canonical NaN, and a signaling NaN raises invalid.
 */
static uint64_t rvv64_op_fmin(riscv_t *rv, uint64_t acc, uint64_t a, uint64_t b)
{
    (void) rv;
    (void) acc;
    if (f64_isSignalingNaN(rvv64_f(a)) || f64_isSignalingNaN(rvv64_f(b)))
        softfloat_exceptionFlags |= softfloat_flag_invalid;
    if (is_nan64(a) && is_nan64(b))
        return RV_NAN64;
    if (is_nan64(a))
        return b;
    if (is_nan64(b))
        return a;
    return (f64_lt_quiet(rvv64_f(a), rvv64_f(b)) ||
            (f64_eq(rvv64_f(a), rvv64_f(b)) && (a & FMASK64_SIGN)))
               ? a
               : b;
}

static uint64_t rvv64_op_fmax(riscv_t *rv, uint64_t acc, uint64_t a, uint64_t b)
{
    (void) rv;
    (void) acc;
    if (f64_isSignalingNaN(rvv64_f(a)) || f64_isSignalingNaN(rvv64_f(b)))
        softfloat_exceptionFlags |= softfloat_flag_invalid;
    if (is_nan64(a) && is_nan64(b))
        return RV_NAN64;
    if (is_nan64(a))
        return b;
    if (is_nan64(b))
        return a;
    return (f64_lt_quiet(rvv64_f(b), rvv64_f(a)) ||
            (f64_eq(rvv64_f(a), rvv64_f(b)) && (b & FMASK64_SIGN)))
               ? a
               : b;
}

/* Comparisons: vmfeq and vmfne are quiet, the others signaling. */
RVV64_CMP(feq, f64_eq(rvv64_f(a), rvv64_f(b)))
RVV64_CMP(fne, !f64_eq(rvv64_f(a), rvv64_f(b)))
RVV64_CMP(flt, f64_lt(rvv64_f(a), rvv64_f(b)))
RVV64_CMP(fle, f64_le(rvv64_f(a), rvv64_f(b)))
RVV64_CMP(fgt, f64_lt(rvv64_f(b), rvv64_f(a)))
RVV64_CMP(fge, f64_le(rvv64_f(b), rvv64_f(a)))

/* Unary operations take vs2 as a. */
typedef uint64_t (*rvv64_unary_fn)(riscv_t *rv, uint64_t a);

static uint64_t rvv64_u_sqrt(riscv_t *rv, uint64_t a)
{
    (void) rv;
    return f64_sqrt(rvv64_f(a)).v;
}

static uint64_t rvv64_u_class(riscv_t *rv, uint64_t a)
{
    (void) rv;
    return calc_fclass64(a);
}

static uint64_t rvv64_u_cvt_xu(riscv_t *rv, uint64_t a)
{
    (void) rv;
    return f64_to_ui64(rvv64_f(a), softfloat_roundingMode, true);
}

static uint64_t rvv64_u_cvt_x(riscv_t *rv, uint64_t a)
{
    (void) rv;
    return (uint64_t) f64_to_i64(rvv64_f(a), softfloat_roundingMode, true);
}

static uint64_t rvv64_u_cvt_rtz_xu(riscv_t *rv, uint64_t a)
{
    (void) rv;
    return f64_to_ui64_r_minMag(rvv64_f(a), true);
}

static uint64_t rvv64_u_cvt_rtz_x(riscv_t *rv, uint64_t a)
{
    (void) rv;
    return (uint64_t) f64_to_i64_r_minMag(rvv64_f(a), true);
}

static uint64_t rvv64_u_cvt_f_xu(riscv_t *rv, uint64_t a)
{
    (void) rv;
    return ui64_to_f64(a).v;
}

static uint64_t rvv64_u_cvt_f_x(riscv_t *rv, uint64_t a)
{
    (void) rv;
    return i64_to_f64((int64_t) a).v;
}

/* The 7-bit estimates (V 1.0 13.9, 13.10), on a 52-bit fraction. They are the
 * binary32 versions in rv32_v_template.c with the field widths of binary64.
 */
static inline void rvv64_normalize(uint64_t sig,
                                   uint32_t exp,
                                   int32_t *norm_exp,
                                   uint64_t *norm_sig)
{
    if (exp) {
        *norm_exp = (int32_t) exp;
        *norm_sig = sig;
        return;
    }
    {
        uint32_t lz = 0;
        uint64_t t = sig;

        while (!(t & (UINT64_C(1) << 51))) {
            t = (t << 1) & FMASK64_FRAC;
            lz++;
        }
        *norm_exp = -(int32_t) lz;
        *norm_sig = (sig << (1 - *norm_exp)) & FMASK64_FRAC;
    }
}

static uint64_t rvv64_u_rsqrt7(riscv_t *rv, uint64_t bits)
{
    uint64_t sign = bits & FMASK64_SIGN;
    uint32_t exp = (uint32_t) ((bits >> 52) & 0x7FF);
    uint64_t sig = bits & FMASK64_FRAC;
    int32_t norm_exp, out_exp;
    uint64_t norm_sig;
    uint32_t idx;

    (void) rv;
    if (exp == 0x7FF) {
        if (sig) {
            if (!(sig & FMASK64_QNAN))
                softfloat_exceptionFlags |= softfloat_flag_invalid;
            return RV_NAN64;
        }
        if (sign) {
            softfloat_exceptionFlags |= softfloat_flag_invalid;
            return RV_NAN64;
        }
        return 0;
    }
    if (!exp && !sig) {
        softfloat_exceptionFlags |= softfloat_flag_infinite;
        return sign | FMASK64_EXPN;
    }
    if (sign) {
        softfloat_exceptionFlags |= softfloat_flag_invalid;
        return RV_NAN64;
    }
    rvv64_normalize(sig, exp, &norm_exp, &norm_sig);
    idx =
        (uint32_t) ((norm_exp & 1) << 6) | (uint32_t) ((norm_sig >> 46) & 0x3F);
    out_exp = (3 * 1023 - 1 - norm_exp) / 2;
    return sign | ((uint64_t) ((uint32_t) out_exp & 0x7FF) << 52) |
           ((uint64_t) (rvv_frsqrt7_table[idx] & 0x7F) << 45);
}

static uint64_t rvv64_u_rec7(riscv_t *rv, uint64_t bits)
{
    uint64_t sign = bits & FMASK64_SIGN;
    uint32_t exp = (uint32_t) ((bits >> 52) & 0x7FF);
    uint64_t sig = bits & FMASK64_FRAC;
    int32_t norm_exp, out_exp;
    uint64_t norm_sig, sig_field;

    (void) rv;
    if (exp == 0x7FF) {
        if (sig) {
            if (!(sig & FMASK64_QNAN))
                softfloat_exceptionFlags |= softfloat_flag_invalid;
            return RV_NAN64;
        }
        return sign;
    }
    if (!exp && !sig) {
        softfloat_exceptionFlags |= softfloat_flag_infinite;
        return sign | FMASK64_EXPN;
    }
    rvv64_normalize(sig, exp, &norm_exp, &norm_sig);
    out_exp = 2 * 1023 - 1 - norm_exp;
    if (out_exp > 2 * 1023 || out_exp < -1) {
        softfloat_exceptionFlags |=
            softfloat_flag_overflow | softfloat_flag_inexact;
        if ((softfloat_roundingMode == softfloat_round_minMag) ||
            (sign ? (softfloat_roundingMode == softfloat_round_max)
                  : (softfloat_roundingMode == softfloat_round_min)))
            return sign | UINT64_C(0x7FEFFFFFFFFFFFFF);
        return sign | FMASK64_EXPN;
    }
    sig_field = (uint64_t) (rvv_frec7_table[(norm_sig >> 45) & 0x7F] & 0x7F)
                << 45;
    if (out_exp == 0 || out_exp == -1) {
        sig_field = ((UINT64_C(1) << 52) | sig_field) >> (1 - out_exp);
        out_exp = 0;
    }
    return sign | ((uint64_t) ((uint32_t) out_exp & 0x7FF) << 52) |
           (sig_field & FMASK64_FRAC);
}

/* A float result under the usual mask and tail policies, with the rounding
 * mode and exception flags handled around it.
 */
static int rvv64_fp_arith(riscv_t *rv,
                          const rv_insn_t *ir,
                          int form,
                          rvv64_fn fn)
{
    if (!rvv64_group_ok(rv, ir->vd) || !rvv64_group_ok(rv, ir->vs2) ||
        (form == RVV64_VV && !rvv64_group_ok(rv, ir->vs1)))
        return RVV_EXEC_TRAP;
    rvv_fp_begin_round(rv);
    rvv64_exec_arith(rv, ir, ir->vd, form, fn);
    set_fflag(rv);
    return RVV_EXEC_DONE;
}

static int rvv64_fp_compare(riscv_t *rv,
                            const rv_insn_t *ir,
                            int form,
                            rvv64_cmp_fn cmp)
{
    if (!rvv64_group_ok(rv, ir->vs2) ||
        (form == RVV64_VV && !rvv64_group_ok(rv, ir->vs1)))
        return RVV_EXEC_TRAP;
    rvv_fp_begin_flags();
    rvv64_exec_compare(rv, ir, form, cmp);
    set_fflag(rv);
    return RVV_EXEC_DONE;
}

/* A one-operand float operation: vs2 in, vd out. */
static int rvv64_fp_unary(riscv_t *rv,
                          const rv_insn_t *ir,
                          rvv64_unary_fn fn,
                          bool dynamic_round)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    if (!rvv64_group_ok(rv, ir->vd) || !rvv64_group_ok(rv, ir->vs2))
        return RVV_EXEC_TRAP;
    if (dynamic_round)
        rvv_fp_begin_round(rv);
    else
        rvv_fp_begin_flags();
    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv64_set(rv, ir->vd, elem, UINT64_MAX);
            continue;
        }
        rvv64_set(rv, ir->vd, elem, fn(rv, rvv64_get(rv, ir->vs2, elem)));
    }
    if (rvv_tail_fill(rv, vta)) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv64_set(rv, ir->vd, elem, UINT64_MAX);
    }
    rv->csr_vstart = 0;
    set_fflag(rv);
    return RVV_EXEC_DONE;
}

static int rvv64_fp_reduction(riscv_t *rv,
                              const rv_insn_t *ir,
                              rvv64_fn fn,
                              bool rounds)
{
    if (rv->csr_vstart || !rvv64_group_ok(rv, ir->vs2))
        return RVV_EXEC_TRAP;
    if (rounds)
        rvv_fp_begin_round(rv);
    else
        rvv_fp_begin_flags();
    rvv64_exec_reduction(rv, ir, fn);
    set_fflag(rv);
    return RVV_EXEC_DONE;
}

static int rvv64_fp_merge(riscv_t *rv, const rv_insn_t *ir, bool move)
{
    if (!rvv64_group_ok(rv, ir->vd) || (!move && !rvv64_group_ok(rv, ir->vs2)))
        return RVV_EXEC_TRAP;
    rvv64_exec_merge(rv, ir, RVV64_VF, move);
    return RVV_EXEC_DONE;
}

#define V64_FARITH(form, fn) \
    return rvv64_fp_arith(rv, ir, RVV64_##form, rvv64_op_##fn)
#define V64_FCMP(form, fn) \
    return rvv64_fp_compare(rv, ir, RVV64_##form, rvv64_cmp_##fn)

/* The float opcodes. Returns RVV_EXEC_CONT for any that are not. */
static int rvv_exec_sew64_fp(riscv_t *rv, const rv_insn_t *ir)
{
    switch (ir->opcode) {
    case rv_insn_vfadd_vv:
        V64_FARITH(VV, fadd);
    case rv_insn_vfadd_vf:
        V64_FARITH(VF, fadd);
    case rv_insn_vfsub_vv:
        V64_FARITH(VV, fsub);
    case rv_insn_vfsub_vf:
        V64_FARITH(VF, fsub);
    case rv_insn_vfrsub_vf:
        V64_FARITH(VF, frsub);
    case rv_insn_vfmul_vv:
        V64_FARITH(VV, fmul);
    case rv_insn_vfmul_vf:
        V64_FARITH(VF, fmul);
    case rv_insn_vfdiv_vv:
        V64_FARITH(VV, fdiv);
    case rv_insn_vfdiv_vf:
        V64_FARITH(VF, fdiv);
    case rv_insn_vfrdiv_vf:
        V64_FARITH(VF, frdiv);
    case rv_insn_vfmin_vv:
        V64_FARITH(VV, fmin);
    case rv_insn_vfmin_vf:
        V64_FARITH(VF, fmin);
    case rv_insn_vfmax_vv:
        V64_FARITH(VV, fmax);
    case rv_insn_vfmax_vf:
        V64_FARITH(VF, fmax);
    case rv_insn_vfsgnj_vv:
        V64_FARITH(VV, fsgnj);
    case rv_insn_vfsgnj_vf:
        V64_FARITH(VF, fsgnj);
    case rv_insn_vfsgnjn_vv:
        V64_FARITH(VV, fsgnjn);
    case rv_insn_vfsgnjn_vf:
        V64_FARITH(VF, fsgnjn);
    case rv_insn_vfsgnjx_vv:
        V64_FARITH(VV, fsgnjx);
    case rv_insn_vfsgnjx_vf:
        V64_FARITH(VF, fsgnjx);
    case rv_insn_vfmacc_vv:
        V64_FARITH(VV, fmacc);
    case rv_insn_vfmacc_vf:
        V64_FARITH(VF, fmacc);
    case rv_insn_vfnmacc_vv:
        V64_FARITH(VV, fnmacc);
    case rv_insn_vfnmacc_vf:
        V64_FARITH(VF, fnmacc);
    case rv_insn_vfmsac_vv:
        V64_FARITH(VV, fmsac);
    case rv_insn_vfmsac_vf:
        V64_FARITH(VF, fmsac);
    case rv_insn_vfnmsac_vv:
        V64_FARITH(VV, fnmsac);
    case rv_insn_vfnmsac_vf:
        V64_FARITH(VF, fnmsac);
    case rv_insn_vfmadd_vv:
        V64_FARITH(VV, fmadd);
    case rv_insn_vfmadd_vf:
        V64_FARITH(VF, fmadd);
    case rv_insn_vfnmadd_vv:
        V64_FARITH(VV, fnmadd);
    case rv_insn_vfnmadd_vf:
        V64_FARITH(VF, fnmadd);
    case rv_insn_vfmsub_vv:
        V64_FARITH(VV, fmsub);
    case rv_insn_vfmsub_vf:
        V64_FARITH(VF, fmsub);
    case rv_insn_vfnmsub_vv:
        V64_FARITH(VV, fnmsub);
    case rv_insn_vfnmsub_vf:
        V64_FARITH(VF, fnmsub);

    case rv_insn_vmfeq_vv:
        V64_FCMP(VV, feq);
    case rv_insn_vmfeq_vf:
        V64_FCMP(VF, feq);
    case rv_insn_vmfne_vv:
        V64_FCMP(VV, fne);
    case rv_insn_vmfne_vf:
        V64_FCMP(VF, fne);
    case rv_insn_vmflt_vv:
        V64_FCMP(VV, flt);
    case rv_insn_vmflt_vf:
        V64_FCMP(VF, flt);
    case rv_insn_vmfle_vv:
        V64_FCMP(VV, fle);
    case rv_insn_vmfle_vf:
        V64_FCMP(VF, fle);
    case rv_insn_vmfgt_vf:
        V64_FCMP(VF, fgt);
    case rv_insn_vmfge_vf:
        V64_FCMP(VF, fge);

    case rv_insn_vfsqrt_v:
        return rvv64_fp_unary(rv, ir, rvv64_u_sqrt, true);
    case rv_insn_vfrsqrt7_v:
        return rvv64_fp_unary(rv, ir, rvv64_u_rsqrt7, false);
    case rv_insn_vfrec7_v:
        return rvv64_fp_unary(rv, ir, rvv64_u_rec7, true);
    case rv_insn_vfclass_v:
        return rvv64_fp_unary(rv, ir, rvv64_u_class, false);
    case rv_insn_vfcvt_xu_f_v:
        return rvv64_fp_unary(rv, ir, rvv64_u_cvt_xu, true);
    case rv_insn_vfcvt_x_f_v:
        return rvv64_fp_unary(rv, ir, rvv64_u_cvt_x, true);
    case rv_insn_vfcvt_rtz_xu_f_v:
        return rvv64_fp_unary(rv, ir, rvv64_u_cvt_rtz_xu, false);
    case rv_insn_vfcvt_rtz_x_f_v:
        return rvv64_fp_unary(rv, ir, rvv64_u_cvt_rtz_x, false);
    case rv_insn_vfcvt_f_xu_v:
        return rvv64_fp_unary(rv, ir, rvv64_u_cvt_f_xu, true);
    case rv_insn_vfcvt_f_x_v:
        return rvv64_fp_unary(rv, ir, rvv64_u_cvt_f_x, true);

    case rv_insn_vfmerge_vfm:
        return rvv64_fp_merge(rv, ir, false);
    case rv_insn_vfmv_v_f:
        return rvv64_fp_merge(rv, ir, true);
    case rv_insn_vfmv_f_s:
        f64_set(rv, ir->rd, rvv64_f(rvv64_get(rv, ir->vs2, 0)));
        rv->csr_vstart = 0;
        return RVV_EXEC_DONE;
    case rv_insn_vfmv_s_f:
        if (rv->csr_vstart < rv->csr_vl)
            rvv64_set(rv, ir->vd, 0, f64_reg(rv, ir->rs1).v);
        rv->csr_vstart = 0;
        return RVV_EXEC_DONE;

    case rv_insn_vfslide1up_vf:
        return rvv64_slide(rv, ir, true, true, 1, f64_reg(rv, ir->rs1).v);
    case rv_insn_vfslide1down_vf:
        return rvv64_slide(rv, ir, false, true, 1, f64_reg(rv, ir->rs1).v);

    case rv_insn_vfredusum_vs:
    case rv_insn_vfredosum_vs:
        return rvv64_fp_reduction(rv, ir, rvv64_op_fadd, true);
    case rv_insn_vfredmin_vs:
        return rvv64_fp_reduction(rv, ir, rvv64_op_fmin, false);
    case rv_insn_vfredmax_vs:
        return rvv64_fp_reduction(rv, ir, rvv64_op_fmax, false);
    default:
        return RVV_EXEC_CONT;
    }
}
#endif

#define V64_ARITH(form, fn) \
    return rvv64_arith(rv, ir, RVV64_##form, rvv64_op_##fn)
#define V64_CMP(form, fn) \
    return rvv64_compare(rv, ir, RVV64_##form, rvv64_cmp_##fn)

/* Run the SEW=64 form of ir, if it has one. */
static int rvv_exec_sew64(riscv_t *rv, const rv_insn_t *ir)
{
    switch (ir->opcode) {
    case rv_insn_vadd_vv:
        V64_ARITH(VV, add);
    case rv_insn_vadd_vx:
        V64_ARITH(VX, add);
    case rv_insn_vadd_vi:
        V64_ARITH(VI, add);
    case rv_insn_vsub_vv:
        V64_ARITH(VV, sub);
    case rv_insn_vsub_vx:
        V64_ARITH(VX, sub);
    case rv_insn_vrsub_vx:
        V64_ARITH(VX, rsub);
    case rv_insn_vrsub_vi:
        V64_ARITH(VI, rsub);
    case rv_insn_vand_vv:
        V64_ARITH(VV, and);
    case rv_insn_vand_vx:
        V64_ARITH(VX, and);
    case rv_insn_vand_vi:
        V64_ARITH(VI, and);
    case rv_insn_vor_vv:
        V64_ARITH(VV, or);
    case rv_insn_vor_vx:
        V64_ARITH(VX, or);
    case rv_insn_vor_vi:
        V64_ARITH(VI, or);
    case rv_insn_vxor_vv:
        V64_ARITH(VV, xor);
    case rv_insn_vxor_vx:
        V64_ARITH(VX, xor);
    case rv_insn_vxor_vi:
        V64_ARITH(VI, xor);
    case rv_insn_vminu_vv:
        V64_ARITH(VV, minu);
    case rv_insn_vminu_vx:
        V64_ARITH(VX, minu);
    case rv_insn_vmin_vv:
        V64_ARITH(VV, min);
    case rv_insn_vmin_vx:
        V64_ARITH(VX, min);
    case rv_insn_vmaxu_vv:
        V64_ARITH(VV, maxu);
    case rv_insn_vmaxu_vx:
        V64_ARITH(VX, maxu);
    case rv_insn_vmax_vv:
        V64_ARITH(VV, max);
    case rv_insn_vmax_vx:
        V64_ARITH(VX, max);
    case rv_insn_vsll_vv:
        V64_ARITH(VV, sll);
    case rv_insn_vsll_vx:
        V64_ARITH(VX, sll);
    case rv_insn_vsll_vi:
        V64_ARITH(VUI, sll);
    case rv_insn_vsrl_vv:
        V64_ARITH(VV, srl);
    case rv_insn_vsrl_vx:
        V64_ARITH(VX, srl);
    case rv_insn_vsrl_vi:
        V64_ARITH(VUI, srl);
    case rv_insn_vsra_vv:
        V64_ARITH(VV, sra);
    case rv_insn_vsra_vx:
        V64_ARITH(VX, sra);
    case rv_insn_vsra_vi:
        V64_ARITH(VUI, sra);
    case rv_insn_vmul_vv:
        V64_ARITH(VV, mul);
    case rv_insn_vmul_vx:
        V64_ARITH(VX, mul);
    case rv_insn_vmulh_vv:
        V64_ARITH(VV, mulh);
    case rv_insn_vmulh_vx:
        V64_ARITH(VX, mulh);
    case rv_insn_vmulhu_vv:
        V64_ARITH(VV, mulhu);
    case rv_insn_vmulhu_vx:
        V64_ARITH(VX, mulhu);
    case rv_insn_vmulhsu_vv:
        V64_ARITH(VV, mulhsu);
    case rv_insn_vmulhsu_vx:
        V64_ARITH(VX, mulhsu);
    case rv_insn_vdivu_vv:
        V64_ARITH(VV, divu);
    case rv_insn_vdivu_vx:
        V64_ARITH(VX, divu);
    case rv_insn_vdiv_vv:
        V64_ARITH(VV, div);
    case rv_insn_vdiv_vx:
        V64_ARITH(VX, div);
    case rv_insn_vremu_vv:
        V64_ARITH(VV, remu);
    case rv_insn_vremu_vx:
        V64_ARITH(VX, remu);
    case rv_insn_vrem_vv:
        V64_ARITH(VV, rem);
    case rv_insn_vrem_vx:
        V64_ARITH(VX, rem);
    case rv_insn_vmacc_vv:
        V64_ARITH(VV, macc);
    case rv_insn_vmacc_vx:
        V64_ARITH(VX, macc);
    case rv_insn_vnmsac_vv:
        V64_ARITH(VV, nmsac);
    case rv_insn_vnmsac_vx:
        V64_ARITH(VX, nmsac);
    case rv_insn_vmadd_vv:
        V64_ARITH(VV, madd);
    case rv_insn_vmadd_vx:
        V64_ARITH(VX, madd);
    case rv_insn_vnmsub_vv:
        V64_ARITH(VV, nmsub);
    case rv_insn_vnmsub_vx:
        V64_ARITH(VX, nmsub);
    case rv_insn_vsaddu_vv:
        V64_ARITH(VV, saddu);
    case rv_insn_vsaddu_vx:
        V64_ARITH(VX, saddu);
    case rv_insn_vsaddu_vi:
        V64_ARITH(VI, saddu);
    case rv_insn_vsadd_vv:
        V64_ARITH(VV, sadd);
    case rv_insn_vsadd_vx:
        V64_ARITH(VX, sadd);
    case rv_insn_vsadd_vi:
        V64_ARITH(VI, sadd);
    case rv_insn_vssubu_vv:
        V64_ARITH(VV, ssubu);
    case rv_insn_vssubu_vx:
        V64_ARITH(VX, ssubu);
    case rv_insn_vssub_vv:
        V64_ARITH(VV, ssub);
    case rv_insn_vssub_vx:
        V64_ARITH(VX, ssub);
    case rv_insn_vaaddu_vv:
        V64_ARITH(VV, aaddu);
    case rv_insn_vaaddu_vx:
        V64_ARITH(VX, aaddu);
    case rv_insn_vaadd_vv:
        V64_ARITH(VV, aadd);
    case rv_insn_vaadd_vx:
        V64_ARITH(VX, aadd);
    case rv_insn_vasubu_vv:
        V64_ARITH(VV, asubu);
    case rv_insn_vasubu_vx:
        V64_ARITH(VX, asubu);
    case rv_insn_vasub_vv:
        V64_ARITH(VV, asub);
    case rv_insn_vasub_vx:
        V64_ARITH(VX, asub);
    case rv_insn_vssrl_vv:
        V64_ARITH(VV, ssrl);
    case rv_insn_vssrl_vx:
        V64_ARITH(VX, ssrl);
    case rv_insn_vssrl_vi:
        V64_ARITH(VUI, ssrl);
    case rv_insn_vssra_vv:
        V64_ARITH(VV, ssra);
    case rv_insn_vssra_vx:
        V64_ARITH(VX, ssra);
    case rv_insn_vssra_vi:
        V64_ARITH(VUI, ssra);
    case rv_insn_vsmul_vv:
        V64_ARITH(VV, smul);
    case rv_insn_vsmul_vx:
        V64_ARITH(VX, smul);

    case rv_insn_vmseq_vv:
        V64_CMP(VV, eq);
    case rv_insn_vmseq_vx:
        V64_CMP(VX, eq);
    case rv_insn_vmseq_vi:
        V64_CMP(VI, eq);
    case rv_insn_vmsne_vv:
        V64_CMP(VV, ne);
    case rv_insn_vmsne_vx:
        V64_CMP(VX, ne);
    case rv_insn_vmsne_vi:
        V64_CMP(VI, ne);
    case rv_insn_vmsltu_vv:
        V64_CMP(VV, ltu);
    case rv_insn_vmsltu_vx:
        V64_CMP(VX, ltu);
    case rv_insn_vmslt_vv:
        V64_CMP(VV, lt);
    case rv_insn_vmslt_vx:
        V64_CMP(VX, lt);
    case rv_insn_vmsleu_vv:
        V64_CMP(VV, leu);
    case rv_insn_vmsleu_vx:
        V64_CMP(VX, leu);
    case rv_insn_vmsleu_vi:
        V64_CMP(VI, leu);
    case rv_insn_vmsle_vv:
        V64_CMP(VV, le);
    case rv_insn_vmsle_vx:
        V64_CMP(VX, le);
    case rv_insn_vmsle_vi:
        V64_CMP(VI, le);
    case rv_insn_vmsgtu_vx:
        V64_CMP(VX, gtu);
    case rv_insn_vmsgtu_vi:
        V64_CMP(VI, gtu);
    case rv_insn_vmsgt_vx:
        V64_CMP(VX, gt);
    case rv_insn_vmsgt_vi:
        V64_CMP(VI, gt);

    case rv_insn_vadc_vvm:
        return rvv64_carry(rv, ir, RVV64_VV, false, false);
    case rv_insn_vadc_vxm:
        return rvv64_carry(rv, ir, RVV64_VX, false, false);
    case rv_insn_vadc_vim:
        return rvv64_carry(rv, ir, RVV64_VI, false, false);
    case rv_insn_vsbc_vvm:
        return rvv64_carry(rv, ir, RVV64_VV, true, false);
    case rv_insn_vsbc_vxm:
        return rvv64_carry(rv, ir, RVV64_VX, true, false);
    case rv_insn_vmadc_vv:
        return rvv64_carry(rv, ir, RVV64_VV, false, true);
    case rv_insn_vmadc_vx:
        return rvv64_carry(rv, ir, RVV64_VX, false, true);
    case rv_insn_vmadc_vi:
        return rvv64_carry(rv, ir, RVV64_VI, false, true);
    case rv_insn_vmsbc_vv:
        return rvv64_carry(rv, ir, RVV64_VV, true, true);
    case rv_insn_vmsbc_vx:
        return rvv64_carry(rv, ir, RVV64_VX, true, true);

    case rv_insn_vmerge_vvm:
    case rv_insn_vmerge_vxm:
    case rv_insn_vmerge_vim: {
        int form =
            ir->opcode == rv_insn_vmerge_vvm
                ? RVV64_VV
                : (ir->opcode == rv_insn_vmerge_vxm ? RVV64_VX : RVV64_VI);
        if (!rvv64_group_ok(rv, ir->vd) || !rvv64_group_ok(rv, ir->vs2) ||
            (form == RVV64_VV && !rvv64_group_ok(rv, ir->vs1)))
            return RVV_EXEC_TRAP;
        rvv64_exec_merge(rv, ir, form, false);
        return RVV_EXEC_DONE;
    }
    case rv_insn_vmv_v_v:
    case rv_insn_vmv_v_x:
    case rv_insn_vmv_v_i: {
        int form = ir->opcode == rv_insn_vmv_v_v
                       ? RVV64_VV
                       : (ir->opcode == rv_insn_vmv_v_x ? RVV64_VX : RVV64_VI);
        if (!rvv64_group_ok(rv, ir->vd) ||
            (form == RVV64_VV && !rvv64_group_ok(rv, ir->vs1)))
            return RVV_EXEC_TRAP;
        rvv64_exec_merge(rv, ir, form, true);
        return RVV_EXEC_DONE;
    }

    case rv_insn_vredsum_vs:
        return rvv64_reduction(rv, ir, rvv64_op_add);
    case rv_insn_vredand_vs:
        return rvv64_reduction(rv, ir, rvv64_op_and);
    case rv_insn_vredor_vs:
        return rvv64_reduction(rv, ir, rvv64_op_or);
    case rv_insn_vredxor_vs:
        return rvv64_reduction(rv, ir, rvv64_op_xor);
    case rv_insn_vredminu_vs:
        return rvv64_reduction(rv, ir, rvv64_op_minu);
    case rv_insn_vredmin_vs:
        return rvv64_reduction(rv, ir, rvv64_op_min);
    case rv_insn_vredmaxu_vs:
        return rvv64_reduction(rv, ir, rvv64_op_maxu);
    case rv_insn_vredmax_vs:
        return rvv64_reduction(rv, ir, rvv64_op_max);

    case rv_insn_vslideup_vx:
        return rvv64_slide(rv, ir, true, false, rv->X[ir->rs1], 0);
    case rv_insn_vslideup_vi:
        return rvv64_slide(rv, ir, true, false, (uint32_t) ir->imm & 0x1f, 0);
    case rv_insn_vslidedown_vx:
        return rvv64_slide(rv, ir, false, false, rv->X[ir->rs1], 0);
    case rv_insn_vslidedown_vi:
        return rvv64_slide(rv, ir, false, false, (uint32_t) ir->imm & 0x1f, 0);
    case rv_insn_vslide1up_vx:
        return rvv64_slide(rv, ir, true, true, 1,
                           (uint64_t) (int64_t) (int32_t) rv->X[ir->rs1]);
    case rv_insn_vslide1down_vx:
        return rvv64_slide(rv, ir, false, true, 1,
                           (uint64_t) (int64_t) (int32_t) rv->X[ir->rs1]);

    case rv_insn_vrgather_vv:
        return rvv64_gather(rv, ir, RVV64_VV, 64);
    case rv_insn_vrgather_vx:
        return rvv64_gather(rv, ir, RVV64_VX, 64);
    case rv_insn_vrgather_vi:
        return rvv64_gather(rv, ir, RVV64_VUI, 64);
    case rv_insn_vrgatherei16_vv:
        return rvv64_gather(rv, ir, RVV64_VV, 16);
    case rv_insn_vcompress_vm:
        if (rv->csr_vstart || !rvv64_group_ok(rv, ir->vd) ||
            !rvv64_group_ok(rv, ir->vs2) ||
            rvv64_overlap(rv, ir->vd, ir->vs2) ||
            rvv_reg_spans_overlap(ir->vd, rvv_group_regs(rv->csr_vtype),
                                  ir->vs1, 1))
            return RVV_EXEC_TRAP;
        rvv64_exec_compress(rv, ir);
        return RVV_EXEC_DONE;
    case rv_insn_viota_m:
        if (!rvv64_group_ok(rv, ir->rd))
            return RVV_EXEC_TRAP;
        rvv64_exec_index(rv, ir, true);
        return RVV_EXEC_DONE;
    case rv_insn_vid_v:
        if (!rvv64_group_ok(rv, ir->vd))
            return RVV_EXEC_TRAP;
        rvv64_exec_index(rv, ir, false);
        return RVV_EXEC_DONE;

    case rv_insn_vzext_vf2:
        return rvv64_extend(rv, ir, 2, false);
    case rv_insn_vsext_vf2:
        return rvv64_extend(rv, ir, 2, true);
    case rv_insn_vzext_vf4:
        return rvv64_extend(rv, ir, 4, false);
    case rv_insn_vsext_vf4:
        return rvv64_extend(rv, ir, 4, true);
    case rv_insn_vzext_vf8:
        return rvv64_extend(rv, ir, 8, false);
    case rv_insn_vsext_vf8:
        return rvv64_extend(rv, ir, 8, true);

    case rv_insn_vmv_x_s:
        /* SEW > XLEN: the least-significant XLEN bits are transferred. */
        if (ir->rd)
            rv->X[ir->rd] = (uint32_t) rvv64_get(rv, ir->vs2, 0);
        rv->csr_vstart = 0;
        return RVV_EXEC_DONE;
    case rv_insn_vmv_s_x:
        if (rv->csr_vstart < rv->csr_vl)
            rvv64_set(rv, ir->vd, 0,
                      (uint64_t) (int64_t) (int32_t) rv->X[ir->rs1]);
        rv->csr_vstart = 0;
        return RVV_EXEC_DONE;
    default:
#if RV32_HAS(EXT_D)
        return rvv_exec_sew64_fp(rv, ir);
#else
        return RVV_EXEC_CONT;
#endif
    }
}
