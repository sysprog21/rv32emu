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

#define RVV_VTYPE_VILL (1U << 31)
#define RVV_VTYPE_LOW_MASK 0xFFU

static inline void rvv_set_vill(riscv_t *rv)
{
    rv->csr_vtype = RVV_VTYPE_VILL;
    rv->csr_vl = 0;
}

static inline void rvv_lmul_ratio(uint32_t vtype, uint32_t *num, uint32_t *den)
{
    uint32_t v_lmul = vtype & 0x7;

    if (v_lmul <= 3) {
        *num = 1U << v_lmul;
        *den = 1;
        return;
    }

    *num = 1;
    *den = 1U << (8 - v_lmul);
}

static inline uint32_t rvv_sew_bits(uint32_t vtype)
{
    return 8U << ((vtype >> 3) & 0x7);
}

static inline uint32_t rvv_vlmax(uint32_t vtype)
{
    uint32_t num, den;
    rvv_lmul_ratio(vtype, &num, &den);
    return ((uint32_t) VLEN * num) / (rvv_sew_bits(vtype) * den);
}

static inline bool rvv_vtype_is_supported(uint32_t vtype)
{
    /* Reserved bits beyond vma/vta/vsew/vlmul must be zero. */
    if (vtype & ~RVV_VTYPE_LOW_MASK)
        return false;

    uint32_t v_lmul = vtype & 0x7;
    uint32_t v_sew = (vtype >> 3) & 0x7;

    /* vlmul=4 is reserved; rv32 supports SEW up to 32 (encoding 2). */
    if (v_lmul == 4 || v_sew > 2)
        return false;

    /* Spec §6.3: any vtype combination that produces VLMAX < 1 (e.g.
     * fractional LMUL paired with a large SEW relative to VLEN) is
     * unsupported and must set vill.
     */
    return rvv_vlmax(vtype) != 0;
}

static inline uint32_t rvv_group_regs(uint32_t vtype)
{
    uint32_t v_lmul = vtype & 0x7;
    return (v_lmul < 4) ? (1U << v_lmul) : 1U;
}

static inline uint32_t rvv_compute_vl(uint32_t avl, uint32_t vlmax)
{
    if (avl <= vlmax)
        return avl;
    if (avl < (2 * vlmax))
        return vlmax;
    return vlmax;
}

static inline bool rvv_require_valid_state(riscv_t *rv)
{
    return !(rv->csr_vtype & RVV_VTYPE_VILL) &&
           rvv_vtype_is_supported(rv->csr_vtype);
}

/* Raise an illegal-instruction exception with the supplied tval. Returns
 * false so callers can write return rvv_trap_illegal_state(rv, tval);
 * directly and have the RVOP impl propagate the trap to the dispatcher
 * (which interprets false as "trap raised, stop block dispatch").
 *
 * The companion rvv_require_operable / rvv_check_data_reg helpers
 * below preserve their original "true = trap raised, false = OK" caller
 * convention by EXPLICITLY returning true after invoking this helper, so
 * the long-standing if (rvv_require_operable(rv)) return false; and
 * if (rvv_check_data_reg(rv, reg)) return false; patterns continue to
 * mean what they look like.
 */
static inline bool rvv_trap_illegal_state(riscv_t *rv, uint32_t tval)
{
    SET_CAUSE_AND_TVAL_THEN_TRAP(rv, ILLEGAL_INSN, tval);
    return false;
}

/* Validate the vector unit is in a runnable state. Returns true (with the
 * trap raised) when vill is set or vtype is otherwise unsupported; returns
 * false when the caller may proceed.
 *
 * Per V 1.0 §3.5 every standard vector instruction is resumable from
 * vstart; helpers iterate from rv->csr_vstart and clear vstart=0 on
 * successful completion. This routine intentionally does NOT trap on
 * non-zero vstart so that a trap-resume sequence can re-execute the
 * faulted instruction from where it left off.
 */
static inline bool rvv_require_operable(riscv_t *rv)
{
    if (!rvv_require_valid_state(rv)) {
        rvv_trap_illegal_state(rv, 0);
        return true;
    }
    return false;
}

static inline uint32_t rvv_get_elem(const riscv_t *rv,
                                    uint32_t reg,
                                    uint32_t elem,
                                    uint32_t sew_bits)
{
    uint32_t elems_per_word = 32 / sew_bits;
    uint32_t linear_word = elem / elems_per_word;
    uint32_t reg_idx = reg + linear_word / VREG_U32_COUNT;
    uint32_t word_idx = linear_word % VREG_U32_COUNT;
    uint32_t slot = elem % elems_per_word;
    uint32_t shift = slot * sew_bits;
    uint32_t mask = (sew_bits == 32) ? 0xFFFFFFFFU : ((1U << sew_bits) - 1U);

    assert(reg_idx < 32);
    return (rv->V[reg_idx][word_idx] >> shift) & mask;
}

static inline void rvv_set_elem(riscv_t *rv,
                                uint32_t reg,
                                uint32_t elem,
                                uint32_t sew_bits,
                                uint32_t value)
{
    uint32_t elems_per_word = 32 / sew_bits;
    uint32_t linear_word = elem / elems_per_word;
    uint32_t reg_idx = reg + linear_word / VREG_U32_COUNT;
    uint32_t word_idx = linear_word % VREG_U32_COUNT;
    uint32_t slot = elem % elems_per_word;
    uint32_t shift = slot * sew_bits;
    uint32_t mask = (sew_bits == 32) ? 0xFFFFFFFFU : ((1U << sew_bits) - 1U);
    uint32_t word_mask = mask << shift;

    assert(reg_idx < 32);
    rv->V[reg_idx][word_idx] =
        (rv->V[reg_idx][word_idx] & ~word_mask) | ((value & mask) << shift);
}

static inline bool rvv_mask_enabled_for_elem(const riscv_t *rv,
                                             const rv_insn_t *ir,
                                             uint32_t elem)
{
    if (ir->vm)
        return true;
    return (rv->V[0][elem >> 5] >> (elem & 31)) & 0x1;
}

static inline uint32_t rvv_data_fill(uint32_t sew_bits)
{
    switch (sew_bits) {
    case 8:
        return 0xFF;
    case 16:
        return 0xFFFF;
    default:
        return 0xFFFFFFFFU;
    }
}

static inline uint64_t rvv_data_fill_ext(uint32_t sew_bits)
{
    return (sew_bits >= 64) ? UINT64_MAX : rvv_data_fill(sew_bits);
}

static inline uint32_t rvv_read_mem_elem(riscv_t *rv,
                                         uint32_t addr,
                                         uint32_t eew)
{
    switch (eew) {
    case 8:
        return rv->io.mem_read_b(rv, addr);
    case 16:
        return rv->io.mem_read_s(rv, addr);
    default:
        return rv->io.mem_read_w(rv, addr);
    }
}

static inline void rvv_write_mem_elem(riscv_t *rv,
                                      uint32_t addr,
                                      uint32_t eew,
                                      uint32_t value)
{
    switch (eew) {
    case 8:
        rv->io.mem_write_b(rv, addr, value & 0xFF);
        break;
    case 16:
        rv->io.mem_write_s(rv, addr, value & 0xFFFF);
        break;
    default:
        rv->io.mem_write_w(rv, addr, value);
        break;
    }
}

/* Extended RVV memory accessors preserve the architected access width for
 * EEW=8/16/32, which keeps MMIO callbacks consistent with scalar loads and
 * stores. EEW=64 uses two 32-bit accesses on RV32 because there is no
 * native 64-bit memory callback in the core I/O interface.
 *
 * These helpers return false immediately on a trap so callers can preserve
 * precise vector trap semantics: do not commit the faulting load result,
 * do not issue later accesses, and leave vstart at the faulting element.
 */
static inline bool rvv_read_mem_elem_ext(riscv_t *rv,
                                         uint32_t addr,
                                         uint32_t eew,
                                         uint64_t *value_out)
{
    uint64_t value;

    switch (eew) {
    case 8:
        value = rv->io.mem_read_b(rv, addr);
        break;
    case 16:
        value = rv->io.mem_read_s(rv, addr);
        break;
    case 32:
        value = rv->io.mem_read_w(rv, addr);
        break;
    default: {
        uint32_t lo = rv->io.mem_read_w(rv, addr);
#if RV32_HAS(SYSTEM)
        if (rv->is_trapped)
            return false;
#endif
        value = lo;
        value |= ((uint64_t) rv->io.mem_read_w(rv, addr + 4)) << 32;
        break;
    }
    }
#if RV32_HAS(SYSTEM)
    if (rv->is_trapped)
        return false;
#endif
    *value_out = value;
    return true;
}

static inline bool rvv_write_mem_elem_ext(riscv_t *rv,
                                          uint32_t addr,
                                          uint32_t eew,
                                          uint64_t value)
{
    switch (eew) {
    case 8:
        rv->io.mem_write_b(rv, addr, value & 0xFF);
        break;
    case 16:
        rv->io.mem_write_s(rv, addr, value & 0xFFFF);
        break;
    case 32:
        rv->io.mem_write_w(rv, addr, value);
        break;
    default:
        rv->io.mem_write_w(rv, addr, (uint32_t) value);
#if RV32_HAS(SYSTEM)
        if (rv->is_trapped)
            return false;
#endif
        rv->io.mem_write_w(rv, addr + 4, (uint32_t) (value >> 32));
        break;
    }
#if RV32_HAS(SYSTEM)
    if (rv->is_trapped)
        return false;
#endif
    return true;
}

static inline uint64_t rvv_get_elem_ext(const riscv_t *rv,
                                        uint32_t reg,
                                        uint32_t elem,
                                        uint32_t sew_bits);
static inline void rvv_set_elem_ext(riscv_t *rv,
                                    uint32_t reg,
                                    uint32_t elem,
                                    uint32_t sew_bits,
                                    uint64_t value);

static inline void rvv_unit_stride_load(riscv_t *rv,
                                        const rv_insn_t *ir,
                                        uint32_t dest,
                                        uint32_t addr)
{
    /* Per V 1.0 §7.4: unit-stride loads use EEW (encoded in the opcode) for
     * both memory and register layout, ignoring SEW. EMUL = (EEW/SEW)*LMUL
     * determines the destination register group; vlmax for the EEW layout
     * is VLEN*EMUL/EEW = VLEN*LMUL/SEW (same as the SEW-based vlmax).
     */
    uint32_t eew = ir->eew;
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint64_t fill = rvv_data_fill_ext(eew);
    uint32_t stride = eew / 8;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint32_t elem_addr = addr + (elem * stride);
        uint64_t value;
        if (!rvv_mask_enabled_for_elem(rv, ir, elem))
            continue;
        if (!rvv_read_mem_elem_ext(rv, elem_addr, eew, &value)) {
            rv->csr_vstart = elem;
            return;
        }
        rvv_set_elem_ext(rv, dest, elem, eew, value);
    }

    if ((rv->csr_vtype >> 6) & 0x1) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem_ext(rv, dest, elem, eew, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_unit_stride_store(riscv_t *rv,
                                         const rv_insn_t *ir,
                                         uint32_t src,
                                         uint32_t addr)
{
    uint32_t eew = ir->eew;
    uint32_t stride = eew / 8;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint32_t elem_addr = addr + (elem * stride);
        if (!rvv_mask_enabled_for_elem(rv, ir, elem))
            continue;
        if (!rvv_write_mem_elem_ext(rv, elem_addr, eew,
                                    rvv_get_elem_ext(rv, src, elem, eew))) {
            rv->csr_vstart = elem;
            return;
        }
    }

    rv->csr_vstart = 0;
}

static inline void rvv_mask_set_bit(riscv_t *rv,
                                    uint32_t reg,
                                    uint32_t elem,
                                    bool bit)
{
    uint32_t *word = &rv->V[reg][elem >> 5];
    uint32_t mask = 1U << (elem & 31);

    if (bit)
        *word |= mask;
    else
        *word &= ~mask;
}

static inline bool rvv_mask_get_bit(const riscv_t *rv,
                                    uint32_t reg,
                                    uint32_t elem)
{
    return (rv->V[reg][elem >> 5] >> (elem & 31)) & 0x1;
}

/* Per V 1.0 §4.5: for mask destinations, elements in [vl, VLMAX) are tail
 * (always agnostic, filled with 1s here) and elements [VLMAX, VLEN) are
 * inactive - they must remain unmodified. Bound the fill loop by vlmax,
 * not the physical register width.
 */
static inline void rvv_mask_fill_tail(riscv_t *rv,
                                      uint32_t reg,
                                      uint32_t start,
                                      uint32_t vlmax)
{
    for (uint32_t elem = start; elem < vlmax; elem++)
        rvv_mask_set_bit(rv, reg, elem, true);
}

static inline uint32_t rvv_trunc_elem(uint32_t value, uint32_t sew_bits)
{
    if (sew_bits == 32)
        return value;
    return value & ((1U << sew_bits) - 1U);
}

static inline uint64_t rvv_trunc_elem64(uint64_t value, uint32_t sew_bits)
{
    if (sew_bits >= 64)
        return value;
    return value & ((UINT64_C(1) << sew_bits) - 1U);
}

static inline int32_t rvv_signed_elem(uint32_t value, uint32_t sew_bits)
{
    return ((int32_t) (rvv_trunc_elem(value, sew_bits) << (32 - sew_bits))) >>
           (32 - sew_bits);
}

static inline bool rvv_validate_data_reg(uint32_t vtype, uint32_t reg)
{
    uint32_t span = rvv_group_regs(vtype);

    if ((reg + span) > 32)
        return false;
    return (span == 1) || ((reg % span) == 0);
}

/* Trap-raising wrapper for SEW-based register-group validation. Returns
 * true when the trap was raised (caller must propagate return false to
 * the dispatcher), false when the register is valid and execution may
 * continue - mirroring rvv_require_operable's caller contract.
 */
static inline bool rvv_check_data_reg(riscv_t *rv, uint32_t reg)
{
    if (rvv_validate_data_reg(rv->csr_vtype, reg))
        return false;
    rvv_trap_illegal_state(rv, 0);
    return true;
}

static inline bool rvv_validate_reg_span(uint32_t reg, uint32_t span)
{
    if (!span || (reg + span) > 32)
        return false;
    return (span == 1) || ((reg % span) == 0);
}

static inline bool rvv_reg_spans_overlap(uint32_t reg_a,
                                         uint32_t span_a,
                                         uint32_t reg_b,
                                         uint32_t span_b)
{
    return (reg_a < (reg_b + span_b)) && (reg_b < (reg_a + span_a));
}

/* Cross-EEW overlap rule per V 1.0 §11.2 (widening) and §11.3 (narrowing):
 * a wider register group may not overlap a narrower one EXCEPT when the
 * narrower group occupies the lower portion of the wider group - i.e.,
 * both groups share the same base register. Returns true when the overlap
 * is illegal (caller should raise illegal-instruction).
 *
 * Both widening and narrowing share this predicate because the spec rule
 * is symmetric: overlap is allowed only when the lower-numbered half of
 * the wider group is the narrower group itself (reg_a == reg_b).
 */
static inline bool rvv_cross_eew_overlap_illegal(uint32_t reg_a,
                                                 uint32_t span_a,
                                                 uint32_t reg_b,
                                                 uint32_t span_b)
{
    if (!rvv_reg_spans_overlap(reg_a, span_a, reg_b, span_b))
        return false;
    return reg_a != reg_b;
}

/* Compute the register-group span for an instruction whose effective
 * element width is eew while the SEW remains as configured in vtype.
 * EMUL = (EEW / SEW) * LMUL per V 1.0 §7.6.1; the register span is
 * ceil(EMUL), clamped to 1 for fractional EMUL.
 *
 * Returns false if EMUL would exceed 8 (architecturally illegal).
 */
static inline bool rvv_eew_reg_span(riscv_t *rv,
                                    uint32_t eew,
                                    uint32_t *span_out)
{
    uint32_t lmul_num, lmul_den;
    uint64_t emul_num, emul_den;

    rvv_lmul_ratio(rv->csr_vtype, &lmul_num, &lmul_den);
    emul_num = (uint64_t) lmul_num * eew;
    emul_den = (uint64_t) lmul_den * rvv_sew_bits(rv->csr_vtype);

    if (emul_num > (emul_den * 8U))
        return false;

    *span_out = (emul_num <= emul_den) ? 1U : (uint32_t) (emul_num / emul_den);
    return true;
}

/* Indexed memory ops use index_eew for the index register and SEW for the
 * data register. The index group span uses the same EMUL formula as the
 * EEW-based register span, so route through the shared helper.
 */
static inline bool rvv_index_group_span(riscv_t *rv,
                                        uint32_t index_eew,
                                        uint32_t *span_out)
{
    return rvv_eew_reg_span(rv, index_eew, span_out);
}

/* Validate a data register for unit-stride / strided / FoF loads/stores
 * whose destination is laid out at EEW (not SEW). This is the spec-correct
 * replacement for the LMUL-based rvv_validate_data_reg in EEW-mode ops.
 */
static inline bool rvv_validate_eew_reg(riscv_t *rv, uint32_t eew, uint32_t reg)
{
    uint32_t span;

    if (!rvv_eew_reg_span(rv, eew, &span))
        return false;
    return rvv_validate_reg_span(reg, span);
}

static inline bool rvv_validate_wide_reg(riscv_t *rv, uint32_t reg)
{
    return rvv_validate_eew_reg(rv, rvv_sew_bits(rv->csr_vtype) << 1, reg);
}

static inline bool rvv_wide_group_span(riscv_t *rv, uint32_t *span_out)
{
    return rvv_eew_reg_span(rv, rvv_sew_bits(rv->csr_vtype) << 1, span_out);
}

static inline const uint8_t *rvv_reg_byte_ptr_c(const riscv_t *rv, uint32_t reg)
{
    return (const uint8_t *) &rv->V[reg][0];
}

static inline uint8_t *rvv_reg_byte_ptr(riscv_t *rv, uint32_t reg)
{
    return (uint8_t *) &rv->V[reg][0];
}

static inline uint64_t rvv_get_elem_ext(const riscv_t *rv,
                                        uint32_t reg,
                                        uint32_t elem,
                                        uint32_t sew_bits)
{
    uint32_t elem_bytes = sew_bits >> 3;
    uint32_t byte_offset = elem * elem_bytes;
    uint32_t reg_idx = reg + (byte_offset / rv->csr_vlenb);
    uint32_t reg_off = byte_offset % rv->csr_vlenb;
    uint64_t value = 0;

    assert((sew_bits % 8) == 0);
    assert(elem_bytes <= 8);

    for (uint32_t i = 0; i < elem_bytes; i++) {
        uint32_t cur_reg = reg_idx + ((reg_off + i) / rv->csr_vlenb);
        uint32_t cur_off = (reg_off + i) % rv->csr_vlenb;
        value |= ((uint64_t) rvv_reg_byte_ptr_c(rv, cur_reg)[cur_off])
                 << (i * 8);
    }
    return value;
}

static inline void rvv_set_elem_ext(riscv_t *rv,
                                    uint32_t reg,
                                    uint32_t elem,
                                    uint32_t sew_bits,
                                    uint64_t value)
{
    uint32_t elem_bytes = sew_bits >> 3;
    uint32_t byte_offset = elem * elem_bytes;
    uint32_t reg_idx = reg + (byte_offset / rv->csr_vlenb);
    uint32_t reg_off = byte_offset % rv->csr_vlenb;

    assert((sew_bits % 8) == 0);
    assert(elem_bytes <= 8);

    value = rvv_trunc_elem64(value, sew_bits);
    for (uint32_t i = 0; i < elem_bytes; i++) {
        uint32_t cur_reg = reg_idx + ((reg_off + i) / rv->csr_vlenb);
        uint32_t cur_off = (reg_off + i) % rv->csr_vlenb;
        rvv_reg_byte_ptr(rv, cur_reg)[cur_off] = (uint8_t) (value >> (i * 8));
    }
}

static inline uint32_t rvv_whole_reg_bytes(const riscv_t *rv, uint32_t nregs)
{
    return nregs * rv->csr_vlenb;
}

static inline void rvv_zero_reg_bytes(riscv_t *rv, uint32_t reg, uint32_t nregs)
{
    memset(&rv->V[reg][0], 0, rvv_whole_reg_bytes(rv, nregs));
}

/* Whole-register vl<n>r.v / vs<n>r.v transfers. Per V 1.0 §7.9 these
 * instructions ignore vl AND vstart entirely, AND must NOT modify vstart
 * on completion. Caller is responsible for vd alignment validation; this
 * helper just moves nregs*VLENB bytes through the io layer.
 */
static inline void rvv_whole_reg_load(riscv_t *rv,
                                      uint32_t dest,
                                      uint32_t nregs,
                                      uint32_t addr)
{
    uint32_t bytes = rvv_whole_reg_bytes(rv, nregs);
    uint8_t *dst = (uint8_t *) &rv->V[dest][0];

    for (uint32_t i = 0; i < bytes; i++)
        dst[i] = rv->io.mem_read_b(rv, addr + i);
}

static inline void rvv_whole_reg_store(riscv_t *rv,
                                       uint32_t src,
                                       uint32_t nregs,
                                       uint32_t addr)
{
    uint32_t bytes = rvv_whole_reg_bytes(rv, nregs);
    uint8_t *srcp = (uint8_t *) &rv->V[src][0];

    for (uint32_t i = 0; i < bytes; i++)
        rv->io.mem_write_b(rv, addr + i, srcp[i]);
}

static inline uint32_t rvv_mask_mem_bytes(uint32_t vl)
{
    return (vl + 7U) >> 3;
}

static inline void rvv_mask_load(riscv_t *rv, uint32_t dest, uint32_t addr)
{
    uint32_t nbytes = rvv_mask_mem_bytes(rv->csr_vl);
    uint8_t *dst = (uint8_t *) &rv->V[dest][0];

    rvv_zero_reg_bytes(rv, dest, 1);
    for (uint32_t i = 0; i < nbytes; i++)
        dst[i] = rv->io.mem_read_b(rv, addr + i);
    /* vlm.v's tail extends to the full VLEN of the mask register (§7.4). */
    rvv_mask_fill_tail(rv, dest, rv->csr_vl, VLEN);
    rv->csr_vstart = 0;
}

static inline void rvv_mask_store(riscv_t *rv, uint32_t src, uint32_t addr)
{
    uint32_t nbytes = rvv_mask_mem_bytes(rv->csr_vl);
    uint8_t *srcp = (uint8_t *) &rv->V[src][0];

    for (uint32_t i = 0; i < nbytes; i++)
        rv->io.mem_write_b(rv, addr + i, srcp[i]);
    rv->csr_vstart = 0;
}

/* Fault-only-first unit-stride load (vle{8,16,32}ff.v).
 *
 * Per V 1.0 §7.6: a fault on the first active element raises the trap as a
 * normal vle.v would; a fault on any later element silently truncates vl
 * to the index of the faulting element and completes without trap. The
 * "first active element" is the first element with the mask bit set when
 * masking is enabled, or element vstart otherwise - NOT absolute element 0.
 *
 * Returns true if execution completed (caller should continue), false if a
 * trap was raised on the first active element (caller must return false to
 * the dispatcher so the trap handler can run).
 */
static inline bool rvv_unit_stride_load_ff(riscv_t *rv,
                                           const rv_insn_t *ir,
                                           uint32_t dest,
                                           uint32_t addr)
{
    uint32_t eew = ir->eew;
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint64_t fill = rvv_data_fill_ext(eew);
    uint32_t stride = eew / 8;
    uint32_t loaded_vl = rv->csr_vl;
#if RV32_HAS(SYSTEM)
    bool seen_active = false;
#endif

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint32_t elem_addr = addr + (elem * stride);
        uint64_t value;

        if (!rvv_mask_enabled_for_elem(rv, ir, elem))
            continue;

        if (!rvv_read_mem_elem_ext(rv, elem_addr, eew, &value)) {
#if RV32_HAS(SYSTEM)
            if (!seen_active)
                return false;
            rv->is_trapped = false;
            loaded_vl = elem;
            rv->csr_vl = loaded_vl;
            break;
#endif
        }
        rvv_set_elem_ext(rv, dest, elem, eew, value);
#if RV32_HAS(SYSTEM)
        seen_active = true;
#endif
    }

    if ((rv->csr_vtype >> 6) & 0x1) {
        for (uint32_t elem = loaded_vl; elem < vlmax; elem++)
            rvv_set_elem_ext(rv, dest, elem, eew, fill);
    }

    rv->csr_vstart = 0;
    return true;
}

/* Read an index element from reg at position elem. The helper uses the
 * extended (64-bit-capable) accessor so EEW=64 indices don't divide-by-zero
 * in the 32-bit rvv_get_elem packing math. RV32 addresses are 32 bits, so
 * the high 32 bits of any 64-bit index are simply truncated when added to
 * the base - which matches the spec's "zero-extend, then add" semantics on
 * an XLEN=32 host since the carry above bit 31 is unobservable in memory.
 */
static inline uint32_t rvv_index_offset(const riscv_t *rv,
                                        uint32_t reg,
                                        uint32_t elem,
                                        uint32_t index_eew)
{
    return (uint32_t) rvv_get_elem_ext(rv, reg, elem, index_eew);
}

static inline void rvv_indexed_load(riscv_t *rv,
                                    const rv_insn_t *ir,
                                    uint32_t dest,
                                    uint32_t base_addr)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint64_t fill = rvv_data_fill_ext(sew_bits);
    uint32_t index_eew = ir->eew;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint32_t elem_addr;
        uint64_t value;

        if (!rvv_mask_enabled_for_elem(rv, ir, elem))
            continue;
        elem_addr = base_addr + rvv_index_offset(rv, ir->vs2, elem, index_eew);
        if (!rvv_read_mem_elem_ext(rv, elem_addr, sew_bits, &value)) {
            rv->csr_vstart = elem;
            return;
        }
        rvv_set_elem_ext(rv, dest, elem, sew_bits, value);
    }

    if ((rv->csr_vtype >> 6) & 0x1) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem_ext(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_indexed_store(riscv_t *rv,
                                     const rv_insn_t *ir,
                                     uint32_t src,
                                     uint32_t base_addr)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t index_eew = ir->eew;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint32_t elem_addr;

        if (!rvv_mask_enabled_for_elem(rv, ir, elem))
            continue;
        elem_addr = base_addr + rvv_index_offset(rv, ir->vs2, elem, index_eew);
        if (!rvv_write_mem_elem_ext(
                rv, elem_addr, sew_bits,
                rvv_get_elem_ext(rv, src, elem, sew_bits))) {
            rv->csr_vstart = elem;
            return;
        }
    }

    rv->csr_vstart = 0;
}

static inline bool rvv_validate_segment_reg_group(uint32_t reg,
                                                  uint32_t field_span,
                                                  uint32_t nf)
{
    return rvv_validate_reg_span(reg, field_span * nf);
}

/* Segmented memory helpers - V 1.0 §7.7.
 *
 * Trap discipline (applies to ALL six helpers): every memory access can
 * fault. On fault the helper must:
 *   1. Stop iterating immediately (do not write the remaining fields).
 *   2. Leave rv->csr_vstart at the segment that faulted so the trap
 *      handler can resume.
 *   3. Return false so the RVOP wrapper propagates return false to the
 *      block dispatcher (which honors rv->is_trapped via on_trap).
 *
 * The fault-only-first variant of unit-stride load is the only path that
 * may suppress the trap and truncate vl - and only when the fault hits
 * after the first successfully completed active element.
 *
 * Returning false on fault means we MUST NOT clear csr_vstart on that
 * path, only on successful completion (or on FoF post-progress truncation
 * which is itself a successful completion of a smaller vl).
 */
static inline bool rvv_segment_unit_stride_load(riscv_t *rv,
                                                const rv_insn_t *ir,
                                                uint32_t dest,
                                                uint32_t nf,
                                                bool fault_only_first)
{
    uint32_t eew = ir->eew;
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint64_t fill = rvv_data_fill_ext(eew);
    uint32_t field_span;
    uint32_t loaded_vl = rv->csr_vl;
    uint32_t seg_stride = nf * (eew >> 3);
    /* nf is encoded in 3 bits → max 8 fields per segment. */
    uint64_t staged[8];
    (void) fault_only_first;
#if RV32_HAS(SYSTEM)
    bool seen_active = false;
#endif

    if (!rvv_eew_reg_span(rv, eew, &field_span))
        return rvv_trap_illegal_state(rv, 0);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint32_t elem_addr;

        if (!rvv_mask_enabled_for_elem(rv, ir, elem))
            continue;
        elem_addr = rv->X[ir->rs1] + (elem * seg_stride);
        /* Stage all nf field reads in a local buffer before committing to
         * vd. Per V 1.0 §7.6 (FoF), if a fault hits a later element the
         * destination must be UNDISTURBED for that element (vta=0) - that
         * means no partial segment writes. The staging buffer also makes
         * the non-FoF trap path well-defined: vd[elem] for any field is
         * never observably modified when this segment's read faults.
         */
        for (uint32_t field = 0; field < nf; field++) {
            uint32_t field_addr = elem_addr + (field * (eew >> 3));
            if (!rvv_read_mem_elem_ext(rv, field_addr, eew, &staged[field])) {
#if RV32_HAS(SYSTEM)
                if (fault_only_first && seen_active) {
                    rv->is_trapped = false;
                    loaded_vl = elem;
                    rv->csr_vl = loaded_vl;
                    goto done;
                }
                /* Non-FoF fault, or FoF fault on the first active element:
                 * leave vstart at the faulting segment and propagate
                 * without committing any of the staged fields. */
                rv->csr_vstart = elem;
                return false;
#endif
            }
        }
        /* All nf fields read successfully - commit atomically. */
        for (uint32_t field = 0; field < nf; field++)
            rvv_set_elem_ext(rv, dest + (field * field_span), elem, eew,
                             staged[field]);
#if RV32_HAS(SYSTEM)
        seen_active = true;
#endif
    }

done:
    if ((rv->csr_vtype >> 6) & 0x1) {
        for (uint32_t field = 0; field < nf; field++) {
            for (uint32_t elem = loaded_vl; elem < vlmax; elem++) {
                rvv_set_elem_ext(rv, dest + (field * field_span), elem, eew,
                                 fill);
            }
        }
    }

    rv->csr_vstart = 0;
    return true;
}

static inline bool rvv_segment_unit_stride_store(riscv_t *rv,
                                                 const rv_insn_t *ir,
                                                 uint32_t src,
                                                 uint32_t nf)
{
    uint32_t eew = ir->eew;
    uint32_t field_span;
    uint32_t seg_stride = nf * (eew >> 3);

    if (!rvv_eew_reg_span(rv, eew, &field_span)) {
        rvv_trap_illegal_state(rv, 0);
        return false;
    }

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint32_t elem_addr;

        if (!rvv_mask_enabled_for_elem(rv, ir, elem))
            continue;
        elem_addr = rv->X[ir->rs1] + (elem * seg_stride);
        for (uint32_t field = 0; field < nf; field++) {
            uint32_t field_addr = elem_addr + (field * (eew >> 3));
            if (!rvv_write_mem_elem_ext(
                    rv, field_addr, eew,
                    rvv_get_elem_ext(rv, src + (field * field_span), elem,
                                     eew))) {
                rv->csr_vstart = elem;
                return false;
            }
        }
    }

    rv->csr_vstart = 0;
    return true;
}

static inline bool rvv_segment_strided_load(riscv_t *rv,
                                            const rv_insn_t *ir,
                                            uint32_t dest,
                                            uint32_t nf)
{
    uint32_t eew = ir->eew;
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint64_t fill = rvv_data_fill_ext(eew);
    uint32_t field_span;
    int32_t stride = (int32_t) rv->X[ir->rs2];
    uint64_t staged[8];

    if (!rvv_eew_reg_span(rv, eew, &field_span)) {
        rvv_trap_illegal_state(rv, 0);
        return false;
    }

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint32_t elem_addr;

        if (!rvv_mask_enabled_for_elem(rv, ir, elem))
            continue;
        elem_addr =
            rv->X[ir->rs1] + (uint32_t) ((int64_t) stride * (int64_t) elem);
        for (uint32_t field = 0; field < nf; field++) {
            uint32_t field_addr = elem_addr + (field * (eew >> 3));
            if (!rvv_read_mem_elem_ext(rv, field_addr, eew, &staged[field])) {
                rv->csr_vstart = elem;
                return false;
            }
        }
        for (uint32_t field = 0; field < nf; field++)
            rvv_set_elem_ext(rv, dest + (field * field_span), elem, eew,
                             staged[field]);
    }

    if ((rv->csr_vtype >> 6) & 0x1) {
        for (uint32_t field = 0; field < nf; field++) {
            for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++) {
                rvv_set_elem_ext(rv, dest + (field * field_span), elem, eew,
                                 fill);
            }
        }
    }

    rv->csr_vstart = 0;
    return true;
}

static inline bool rvv_segment_strided_store(riscv_t *rv,
                                             const rv_insn_t *ir,
                                             uint32_t src,
                                             uint32_t nf)
{
    uint32_t eew = ir->eew;
    uint32_t field_span;
    int32_t stride = (int32_t) rv->X[ir->rs2];

    if (!rvv_eew_reg_span(rv, eew, &field_span)) {
        rvv_trap_illegal_state(rv, 0);
        return false;
    }

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint32_t elem_addr;

        if (!rvv_mask_enabled_for_elem(rv, ir, elem))
            continue;
        elem_addr =
            rv->X[ir->rs1] + (uint32_t) ((int64_t) stride * (int64_t) elem);
        for (uint32_t field = 0; field < nf; field++) {
            uint32_t field_addr = elem_addr + (field * (eew >> 3));
            if (!rvv_write_mem_elem_ext(
                    rv, field_addr, eew,
                    rvv_get_elem_ext(rv, src + (field * field_span), elem,
                                     eew))) {
                rv->csr_vstart = elem;
                return false;
            }
        }
    }

    rv->csr_vstart = 0;
    return true;
}

static inline bool rvv_segment_indexed_load(riscv_t *rv,
                                            const rv_insn_t *ir,
                                            uint32_t dest,
                                            uint32_t nf)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint64_t fill = rvv_data_fill_ext(sew_bits);
    uint32_t field_span = rvv_group_regs(rv->csr_vtype);
    uint32_t field_bytes = sew_bits >> 3;
    uint64_t staged[8];

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint32_t elem_addr;

        if (!rvv_mask_enabled_for_elem(rv, ir, elem))
            continue;
        elem_addr =
            rv->X[ir->rs1] + rvv_index_offset(rv, ir->vs2, elem, ir->eew);
        for (uint32_t field = 0; field < nf; field++) {
            uint32_t field_addr = elem_addr + (field * field_bytes);
            if (!rvv_read_mem_elem_ext(rv, field_addr, sew_bits,
                                       &staged[field])) {
                rv->csr_vstart = elem;
                return false;
            }
        }
        for (uint32_t field = 0; field < nf; field++)
            rvv_set_elem_ext(rv, dest + (field * field_span), elem, sew_bits,
                             staged[field]);
    }

    if ((rv->csr_vtype >> 6) & 0x1) {
        for (uint32_t field = 0; field < nf; field++) {
            for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++) {
                rvv_set_elem_ext(rv, dest + (field * field_span), elem,
                                 sew_bits, fill);
            }
        }
    }

    rv->csr_vstart = 0;
    return true;
}

static inline bool rvv_segment_indexed_store(riscv_t *rv,
                                             const rv_insn_t *ir,
                                             uint32_t src,
                                             uint32_t nf)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t field_span = rvv_group_regs(rv->csr_vtype);
    uint32_t field_bytes = sew_bits >> 3;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint32_t elem_addr;

        if (!rvv_mask_enabled_for_elem(rv, ir, elem))
            continue;
        elem_addr =
            rv->X[ir->rs1] + rvv_index_offset(rv, ir->vs2, elem, ir->eew);
        for (uint32_t field = 0; field < nf; field++) {
            uint32_t field_addr = elem_addr + (field * field_bytes);
            if (!rvv_write_mem_elem_ext(
                    rv, field_addr, sew_bits,
                    rvv_get_elem_ext(rv, src + (field * field_span), elem,
                                     sew_bits))) {
                rv->csr_vstart = elem;
                return false;
            }
        }
    }

    rv->csr_vstart = 0;
    return true;
}

/* Segmented memory RVOP wrapper macros.
 *
 * Validation discipline (V 1.0 §7.7):
 *   - rvv_require_operable: vill / vtype sanity.
 *   - rvv_eew_reg_span + rvv_validate_segment_reg_group: nf*field_span ≤ 32
 *     and proper LMUL alignment.
 *   - Masked loads (vm=0): destination segment group must NOT overlap v0.
 *     Otherwise rvv_mask_enabled_for_elem reads a partially-clobbered v0
 *     mid-instruction.
 *   - Indexed loads: destination segment group must NOT overlap the index
 *     group. When data EEW differs from index EEW, the cross-EEW lower-half
 *     exception applies; otherwise any overlap is illegal.
 *
 * Trap propagation: every helper returns bool. A false return means the
 * helper either raised illegal-instruction or stopped at a memory fault
 * with rv->csr_vstart left at the faulting segment for resume. The macro
 * propagates return false to the dispatcher.
 */
#define RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(name, nf_value, fault_first)       \
    RVOP(name, {                                                           \
        uint32_t field_span;                                               \
        if (rvv_require_operable(rv))                                      \
            return false;                                                  \
        if (!rvv_eew_reg_span(rv, ir->eew, &field_span) ||                 \
            !rvv_validate_segment_reg_group(ir->vd, field_span, nf_value)) \
            return rvv_trap_illegal_state(rv, 0);                          \
        if (!ir->vm &&                                                     \
            rvv_reg_spans_overlap(ir->vd, field_span * (nf_value), 0, 1))  \
            return rvv_trap_illegal_state(rv, 0);                          \
        if (!rvv_segment_unit_stride_load(rv, ir, ir->vd, nf_value,        \
                                          fault_first))                    \
            return false;                                                  \
    })

#define RVV_SEGMENT_UNIT_STRIDE_STORE_OP(name, nf_value)                    \
    RVOP(name, {                                                            \
        uint32_t field_span;                                                \
        if (rvv_require_operable(rv))                                       \
            return false;                                                   \
        if (!rvv_eew_reg_span(rv, ir->eew, &field_span) ||                  \
            !rvv_validate_segment_reg_group(ir->vs3, field_span, nf_value)) \
            return rvv_trap_illegal_state(rv, 0);                           \
        if (!rvv_segment_unit_stride_store(rv, ir, ir->vs3, nf_value))      \
            return false;                                                   \
    })

#define RVV_SEGMENT_STRIDED_LOAD_OP(name, nf_value)                        \
    RVOP(name, {                                                           \
        uint32_t field_span;                                               \
        if (rvv_require_operable(rv))                                      \
            return false;                                                  \
        if (!rvv_eew_reg_span(rv, ir->eew, &field_span) ||                 \
            !rvv_validate_segment_reg_group(ir->vd, field_span, nf_value)) \
            return rvv_trap_illegal_state(rv, 0);                          \
        if (!ir->vm &&                                                     \
            rvv_reg_spans_overlap(ir->vd, field_span * (nf_value), 0, 1))  \
            return rvv_trap_illegal_state(rv, 0);                          \
        if (!rvv_segment_strided_load(rv, ir, ir->vd, nf_value))           \
            return false;                                                  \
    })

#define RVV_SEGMENT_STRIDED_STORE_OP(name, nf_value)                        \
    RVOP(name, {                                                            \
        uint32_t field_span;                                                \
        if (rvv_require_operable(rv))                                       \
            return false;                                                   \
        if (!rvv_eew_reg_span(rv, ir->eew, &field_span) ||                  \
            !rvv_validate_segment_reg_group(ir->vs3, field_span, nf_value)) \
            return rvv_trap_illegal_state(rv, 0);                           \
        if (!rvv_segment_strided_store(rv, ir, ir->vs3, nf_value))          \
            return false;                                                   \
    })

#define RVV_SEGMENT_INDEXED_LOAD_OP(name, nf_value)                          \
    RVOP(name, {                                                             \
        uint32_t data_span = rvv_group_regs(rv->csr_vtype);                  \
        uint32_t index_span;                                                 \
        uint32_t total_dest_span;                                            \
        if (rvv_require_operable(rv))                                        \
            return false;                                                    \
        if (!rvv_validate_segment_reg_group(ir->vd, data_span, nf_value) ||  \
            !rvv_index_group_span(rv, ir->eew, &index_span) ||               \
            !rvv_validate_reg_span(ir->vs2, index_span))                     \
            return rvv_trap_illegal_state(rv, 0);                            \
        total_dest_span = data_span * (nf_value);                            \
        if (!ir->vm && rvv_reg_spans_overlap(ir->vd, total_dest_span, 0, 1)) \
            return rvv_trap_illegal_state(rv, 0);                            \
        if (data_span != index_span                                          \
                ? rvv_cross_eew_overlap_illegal(ir->vd, total_dest_span,     \
                                                ir->vs2, index_span)         \
                : rvv_reg_spans_overlap(ir->vd, total_dest_span, ir->vs2,    \
                                        index_span))                         \
            return rvv_trap_illegal_state(rv, 0);                            \
        if (!rvv_segment_indexed_load(rv, ir, ir->vd, nf_value))             \
            return false;                                                    \
    })

#define RVV_SEGMENT_INDEXED_STORE_OP(name, nf_value)                         \
    RVOP(name, {                                                             \
        uint32_t data_span = rvv_group_regs(rv->csr_vtype);                  \
        uint32_t index_span;                                                 \
        if (rvv_require_operable(rv))                                        \
            return false;                                                    \
        if (!rvv_validate_segment_reg_group(ir->vs3, data_span, nf_value) || \
            !rvv_index_group_span(rv, ir->eew, &index_span) ||               \
            !rvv_validate_reg_span(ir->vs2, index_span))                     \
            return rvv_trap_illegal_state(rv, 0);                            \
        if (!rvv_segment_indexed_store(rv, ir, ir->vs3, nf_value))           \
            return false;                                                    \
    })

typedef bool (*rvv_mask_cmp_fn)(uint32_t lhs, uint32_t rhs, uint32_t sew_bits);
typedef uint32_t (*rvv_data_binop_fn)(uint32_t lhs,
                                      uint32_t rhs,
                                      uint32_t sew_bits);
typedef uint32_t (*rvv_data_binop_stateful_fn)(riscv_t *rv,
                                               uint32_t lhs,
                                               uint32_t rhs,
                                               uint32_t sew_bits);
typedef uint64_t (*rvv_wide_narrow_binop_fn)(uint32_t lhs,
                                             uint32_t rhs,
                                             uint32_t sew_bits);
typedef uint64_t (*rvv_wide_mixed_binop_fn)(uint64_t lhs,
                                            uint32_t rhs,
                                            uint32_t sew_bits);
typedef uint32_t (*rvv_narrow_shift_op_fn)(riscv_t *rv,
                                           uint64_t value,
                                           uint32_t shift,
                                           uint32_t sew_bits);

/* Common loop body for the five mask-compare variants. Loops body elements
 * [vstart, vl); leaves prestart elements undisturbed. Mask-result tail is
 * always agnostic and bounded by VLMAX, not VLEN (§4.5).
 */
static inline void rvv_exec_mask_compare_vv(riscv_t *rv,
                                            const rv_insn_t *ir,
                                            uint32_t dest,
                                            rvv_mask_cmp_fn cmp)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_mask_set_bit(rv, dest, elem, true);
            continue;
        }
        rvv_mask_set_bit(
            rv, dest, elem,
            cmp(rvv_get_elem(rv, ir->vs2, elem, sew_bits),
                rvv_get_elem(rv, ir->vs1, elem, sew_bits), sew_bits));
    }

    rvv_mask_fill_tail(rv, dest, rv->csr_vl, vlmax);
    rv->csr_vstart = 0;
}

static inline void rvv_exec_mask_compare_vx(riscv_t *rv,
                                            const rv_insn_t *ir,
                                            uint32_t dest,
                                            rvv_mask_cmp_fn cmp)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint32_t scalar = rvv_trunc_elem(rv->X[ir->rs1], sew_bits);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_mask_set_bit(rv, dest, elem, true);
            continue;
        }
        rvv_mask_set_bit(
            rv, dest, elem,
            cmp(rvv_get_elem(rv, ir->vs2, elem, sew_bits), scalar, sew_bits));
    }

    rvv_mask_fill_tail(rv, dest, rv->csr_vl, vlmax);
    rv->csr_vstart = 0;
}

static inline void rvv_exec_mask_compare_vi(riscv_t *rv,
                                            const rv_insn_t *ir,
                                            uint32_t dest,
                                            rvv_mask_cmp_fn cmp)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint32_t imm = rvv_trunc_elem((uint32_t) ir->imm, sew_bits);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_mask_set_bit(rv, dest, elem, true);
            continue;
        }
        rvv_mask_set_bit(
            rv, dest, elem,
            cmp(rvv_get_elem(rv, ir->vs2, elem, sew_bits), imm, sew_bits));
    }

    rvv_mask_fill_tail(rv, dest, rv->csr_vl, vlmax);
    rv->csr_vstart = 0;
}

static inline void rvv_exec_mask_compare_vx_rev(riscv_t *rv,
                                                const rv_insn_t *ir,
                                                uint32_t dest,
                                                rvv_mask_cmp_fn cmp)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint32_t scalar = rvv_trunc_elem(rv->X[ir->rs1], sew_bits);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_mask_set_bit(rv, dest, elem, true);
            continue;
        }
        rvv_mask_set_bit(
            rv, dest, elem,
            cmp(scalar, rvv_get_elem(rv, ir->vs2, elem, sew_bits), sew_bits));
    }

    rvv_mask_fill_tail(rv, dest, rv->csr_vl, vlmax);
    rv->csr_vstart = 0;
}

static inline void rvv_exec_mask_compare_vi_rev(riscv_t *rv,
                                                const rv_insn_t *ir,
                                                uint32_t dest,
                                                rvv_mask_cmp_fn cmp)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint32_t imm = rvv_trunc_elem((uint32_t) ir->imm, sew_bits);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_mask_set_bit(rv, dest, elem, true);
            continue;
        }
        rvv_mask_set_bit(
            rv, dest, elem,
            cmp(imm, rvv_get_elem(rv, ir->vs2, elem, sew_bits), sew_bits));
    }

    rvv_mask_fill_tail(rv, dest, rv->csr_vl, vlmax);
    rv->csr_vstart = 0;
}

/* Element body loop respecting V 1.0 §3.5 element groups:
 *   [0, vstart):   prestart - always undisturbed, never written.
 *   [vstart, vl):  body - executes op, masked-off elements use vma.
 *   [vl, vlmax):   tail - uses vta.
 */
static inline void rvv_exec_data_vv(riscv_t *rv,
                                    const rv_insn_t *ir,
                                    uint32_t dest,
                                    rvv_data_binop_fn op)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, sew_bits, fill);
            continue;
        }
        rvv_set_elem(rv, dest, elem, sew_bits,
                     op(rvv_get_elem(rv, ir->vs2, elem, sew_bits),
                        rvv_get_elem(rv, ir->vs1, elem, sew_bits), sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_data_vx(riscv_t *rv,
                                    const rv_insn_t *ir,
                                    uint32_t dest,
                                    rvv_data_binop_fn op)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t scalar = rvv_trunc_elem(rv->X[ir->rs1], sew_bits);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, sew_bits, fill);
            continue;
        }
        rvv_set_elem(
            rv, dest, elem, sew_bits,
            op(rvv_get_elem(rv, ir->vs2, elem, sew_bits), scalar, sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_data_vi(riscv_t *rv,
                                    const rv_insn_t *ir,
                                    uint32_t dest,
                                    rvv_data_binop_fn op)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t imm = rvv_trunc_elem((uint32_t) ir->imm, sew_bits);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, sew_bits, fill);
            continue;
        }
        rvv_set_elem(
            rv, dest, elem, sew_bits,
            op(rvv_get_elem(rv, ir->vs2, elem, sew_bits), imm, sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_data_vv_stateful(riscv_t *rv,
                                             const rv_insn_t *ir,
                                             uint32_t dest,
                                             rvv_data_binop_stateful_fn op)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, sew_bits, fill);
            continue;
        }
        rvv_set_elem(rv, dest, elem, sew_bits,
                     op(rv, rvv_get_elem(rv, ir->vs2, elem, sew_bits),
                        rvv_get_elem(rv, ir->vs1, elem, sew_bits), sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_data_vx_stateful(riscv_t *rv,
                                             const rv_insn_t *ir,
                                             uint32_t dest,
                                             rvv_data_binop_stateful_fn op)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t scalar = rvv_trunc_elem(rv->X[ir->rs1], sew_bits);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, sew_bits, fill);
            continue;
        }
        rvv_set_elem(rv, dest, elem, sew_bits,
                     op(rv, rvv_get_elem(rv, ir->vs2, elem, sew_bits), scalar,
                        sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_data_vi_stateful(riscv_t *rv,
                                             const rv_insn_t *ir,
                                             uint32_t dest,
                                             rvv_data_binop_stateful_fn op)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t imm = rvv_trunc_elem((uint32_t) ir->imm, sew_bits);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, sew_bits, fill);
            continue;
        }
        rvv_set_elem(
            rv, dest, elem, sew_bits,
            op(rv, rvv_get_elem(rv, ir->vs2, elem, sew_bits), imm, sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline bool rvv_cmp_eq(uint32_t lhs, uint32_t rhs, uint32_t sew_bits)
{
    return rvv_trunc_elem(lhs, sew_bits) == rvv_trunc_elem(rhs, sew_bits);
}

static inline bool rvv_cmp_ne(uint32_t lhs, uint32_t rhs, uint32_t sew_bits)
{
    return !rvv_cmp_eq(lhs, rhs, sew_bits);
}

static inline bool rvv_cmp_ltu(uint32_t lhs, uint32_t rhs, uint32_t sew_bits)
{
    return rvv_trunc_elem(lhs, sew_bits) < rvv_trunc_elem(rhs, sew_bits);
}

static inline bool rvv_cmp_lt(uint32_t lhs, uint32_t rhs, uint32_t sew_bits)
{
    return rvv_signed_elem(lhs, sew_bits) < rvv_signed_elem(rhs, sew_bits);
}

static inline bool rvv_cmp_leu(uint32_t lhs, uint32_t rhs, uint32_t sew_bits)
{
    return rvv_trunc_elem(lhs, sew_bits) <= rvv_trunc_elem(rhs, sew_bits);
}

static inline bool rvv_cmp_le(uint32_t lhs, uint32_t rhs, uint32_t sew_bits)
{
    return rvv_signed_elem(lhs, sew_bits) <= rvv_signed_elem(rhs, sew_bits);
}

static inline uint32_t rvv_op_minu(uint32_t lhs,
                                   uint32_t rhs,
                                   uint32_t sew_bits)
{
    lhs = rvv_trunc_elem(lhs, sew_bits);
    rhs = rvv_trunc_elem(rhs, sew_bits);
    return (lhs < rhs) ? lhs : rhs;
}

static inline uint32_t rvv_op_min(uint32_t lhs, uint32_t rhs, uint32_t sew_bits)
{
    return (rvv_signed_elem(lhs, sew_bits) < rvv_signed_elem(rhs, sew_bits))
               ? rvv_trunc_elem(lhs, sew_bits)
               : rvv_trunc_elem(rhs, sew_bits);
}

static inline uint32_t rvv_op_maxu(uint32_t lhs,
                                   uint32_t rhs,
                                   uint32_t sew_bits)
{
    lhs = rvv_trunc_elem(lhs, sew_bits);
    rhs = rvv_trunc_elem(rhs, sew_bits);
    return (lhs > rhs) ? lhs : rhs;
}

static inline uint32_t rvv_op_max(uint32_t lhs, uint32_t rhs, uint32_t sew_bits)
{
    return (rvv_signed_elem(lhs, sew_bits) > rvv_signed_elem(rhs, sew_bits))
               ? rvv_trunc_elem(lhs, sew_bits)
               : rvv_trunc_elem(rhs, sew_bits);
}

static inline uint32_t rvv_op_add(uint32_t lhs, uint32_t rhs, uint32_t sew_bits)
{
    return rvv_trunc_elem(lhs + rhs, sew_bits);
}

static inline uint32_t rvv_op_and(uint32_t lhs, uint32_t rhs, uint32_t sew_bits)
{
    return rvv_trunc_elem(lhs & rhs, sew_bits);
}

static inline uint32_t rvv_op_or(uint32_t lhs, uint32_t rhs, uint32_t sew_bits)
{
    return rvv_trunc_elem(lhs | rhs, sew_bits);
}

static inline uint32_t rvv_op_xor(uint32_t lhs, uint32_t rhs, uint32_t sew_bits)
{
    return rvv_trunc_elem(lhs ^ rhs, sew_bits);
}

static inline uint32_t rvv_op_divu(uint32_t lhs,
                                   uint32_t rhs,
                                   uint32_t sew_bits)
{
    lhs = rvv_trunc_elem(lhs, sew_bits);
    rhs = rvv_trunc_elem(rhs, sew_bits);
    if (!rhs)
        return rvv_data_fill(sew_bits);
    return rvv_trunc_elem(lhs / rhs, sew_bits);
}

static inline uint32_t rvv_op_div(uint32_t lhs, uint32_t rhs, uint32_t sew_bits)
{
    int32_t dividend = rvv_signed_elem(lhs, sew_bits);
    int32_t divisor = rvv_signed_elem(rhs, sew_bits);
    int32_t min_value = rvv_signed_elem(1U << (sew_bits - 1), sew_bits);

    if (!divisor)
        return rvv_data_fill(sew_bits);
    if ((dividend == min_value) && (divisor == -1))
        return rvv_trunc_elem((uint32_t) dividend, sew_bits);
    return rvv_trunc_elem((uint32_t) (dividend / divisor), sew_bits);
}

static inline uint32_t rvv_op_remu(uint32_t lhs,
                                   uint32_t rhs,
                                   uint32_t sew_bits)
{
    lhs = rvv_trunc_elem(lhs, sew_bits);
    rhs = rvv_trunc_elem(rhs, sew_bits);
    if (!rhs)
        return lhs;
    return rvv_trunc_elem(lhs % rhs, sew_bits);
}

static inline uint32_t rvv_op_rem(uint32_t lhs, uint32_t rhs, uint32_t sew_bits)
{
    int32_t dividend = rvv_signed_elem(lhs, sew_bits);
    int32_t divisor = rvv_signed_elem(rhs, sew_bits);
    int32_t min_value = rvv_signed_elem(1U << (sew_bits - 1), sew_bits);

    if (!divisor)
        return rvv_trunc_elem((uint32_t) dividend, sew_bits);
    if ((dividend == min_value) && (divisor == -1))
        return 0;
    return rvv_trunc_elem((uint32_t) (dividend % divisor), sew_bits);
}

static inline uint32_t rvv_op_mulhu(uint32_t lhs,
                                    uint32_t rhs,
                                    uint32_t sew_bits)
{
    uint64_t prod = (uint64_t) rvv_trunc_elem(lhs, sew_bits) *
                    (uint64_t) rvv_trunc_elem(rhs, sew_bits);
    return rvv_trunc_elem((uint32_t) (prod >> sew_bits), sew_bits);
}

static inline uint32_t rvv_op_mulhsu(uint32_t lhs,
                                     uint32_t rhs,
                                     uint32_t sew_bits)
{
    int64_t prod = (int64_t) rvv_signed_elem(lhs, sew_bits) *
                   (int64_t) rvv_trunc_elem(rhs, sew_bits);
    return rvv_trunc_elem((uint32_t) (((uint64_t) prod) >> sew_bits), sew_bits);
}

static inline uint32_t rvv_op_mulh(uint32_t lhs,
                                   uint32_t rhs,
                                   uint32_t sew_bits)
{
    int64_t prod = (int64_t) rvv_signed_elem(lhs, sew_bits) *
                   (int64_t) rvv_signed_elem(rhs, sew_bits);
    return rvv_trunc_elem((uint32_t) (((uint64_t) prod) >> sew_bits), sew_bits);
}

static inline uint32_t rvv_elem_addr_stride(uint32_t base,
                                            int32_t stride,
                                            uint32_t elem)
{
    return base + (uint32_t) ((int64_t) stride * (int64_t) elem);
}

static inline void rvv_strided_load(riscv_t *rv,
                                    const rv_insn_t *ir,
                                    uint32_t dest,
                                    uint32_t addr)
{
    uint32_t eew = ir->eew;
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint64_t fill = rvv_data_fill_ext(eew);
    int32_t stride = (int32_t) rv->X[ir->rs2];

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint64_t value;
        if (!rvv_mask_enabled_for_elem(rv, ir, elem))
            continue;
        if (!rvv_read_mem_elem_ext(rv, rvv_elem_addr_stride(addr, stride, elem),
                                   eew, &value)) {
            rv->csr_vstart = elem;
            return;
        }
        rvv_set_elem_ext(rv, dest, elem, eew, value);
    }

    if ((rv->csr_vtype >> 6) & 0x1) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem_ext(rv, dest, elem, eew, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_strided_store(riscv_t *rv,
                                     const rv_insn_t *ir,
                                     uint32_t src,
                                     uint32_t addr)
{
    uint32_t eew = ir->eew;
    int32_t stride = (int32_t) rv->X[ir->rs2];

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem))
            continue;
        if (!rvv_write_mem_elem_ext(
                rv, rvv_elem_addr_stride(addr, stride, elem), eew,
                rvv_get_elem_ext(rv, src, elem, eew))) {
            rv->csr_vstart = elem;
            return;
        }
    }

    rv->csr_vstart = 0;
}

/* vmsbf.m / vmsof.m / vmsif.m - set bits relative to the first source mask
 * bit found in vs2.
 *
 * Per V 1.0 §16.5: bits in vs2 corresponding to disabled elements are
 * ignored when finding the first set bit. The seen flag therefore only
 * advances when the element is mask-active; reading vs2 for a disabled
 * element must not contribute to the prefix state.
 *
 * vmsbf walks the destination once, no second "tail-fill" pass needed:
 * once seen flips true, every subsequent active body element gets
 * out=false naturally. (The previous code's "if !seen at end, fill rest
 * with 1" pass duplicated work the main loop already did.)
 */
static inline void rvv_exec_mask_prefix(riscv_t *rv,
                                        const rv_insn_t *ir,
                                        uint32_t dest,
                                        uint8_t mode)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    bool seen = false;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_mask_set_bit(rv, dest, elem, true);
            continue;
        }

        bool src_bit = rvv_mask_get_bit(rv, ir->vs2, elem);
        bool out;

        switch (mode) {
        case 0: /* vmsbf: set bits before the first source bit */
            out = !seen && !src_bit;
            break;
        case 1: /* vmsof: set only the first source bit */
            out = src_bit && !seen;
            break;
        default: /* vmsif: set bits up to and including the first source bit */
            out = !seen;
            break;
        }
        rvv_mask_set_bit(rv, dest, elem, out);

        if (src_bit)
            seen = true;
    }

    rvv_mask_fill_tail(rv, dest, rv->csr_vl, vlmax);
    rv->csr_vstart = 0;
}

/* viota.m - destination[i] = count of mask-active source bits in [0, i).
 *
 * Per V 1.0 §16.4: disabled elements do not contribute to the prefix sum;
 * the source bit is only consulted (and counted) when the element is
 * mask-active. To resume from vstart, the running count for elements
 * [0, vstart) is reconstructed by walking those elements first without
 * writing the destination (prestart elements stay undisturbed).
 */
static inline void rvv_exec_viota(riscv_t *rv,
                                  const rv_insn_t *ir,
                                  uint32_t dest)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t count = 0;

    /* Reconstruct prefix count over the prestart range without writing. */
    for (uint32_t elem = 0; elem < rv->csr_vstart; elem++) {
        if (rvv_mask_enabled_for_elem(rv, ir, elem) &&
            rvv_mask_get_bit(rv, ir->vs2, elem))
            count++;
    }

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        bool active = rvv_mask_enabled_for_elem(rv, ir, elem);

        if (!active) {
            if (vma)
                rvv_set_elem(rv, dest, elem, sew_bits, fill);
            continue;
        }
        rvv_set_elem(rv, dest, elem, sew_bits, rvv_trunc_elem(count, sew_bits));
        if (rvv_mask_get_bit(rv, ir->vs2, elem))
            count++;
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_vid(riscv_t *rv, const rv_insn_t *ir, uint32_t dest)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, sew_bits, fill);
            continue;
        }
        rvv_set_elem(rv, dest, elem, sew_bits, rvv_trunc_elem(elem, sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_mask_logical(riscv_t *rv,
                                         uint32_t dest,
                                         uint32_t lhs,
                                         uint32_t rhs,
                                         uint8_t op)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        bool a = rvv_mask_get_bit(rv, lhs, elem);
        bool b = rvv_mask_get_bit(rv, rhs, elem);
        bool out;

        switch (op) {
        case 0:
            out = a && !b;
            break;
        case 1:
            out = a && b;
            break;
        case 2:
            out = a || b;
            break;
        case 3:
            out = a ^ b;
            break;
        case 4:
            out = a || !b;
            break;
        case 5:
            out = !(a && b);
            break;
        case 6:
            out = !(a || b);
            break;
        default:
            out = !(a ^ b);
            break;
        }
        rvv_mask_set_bit(rv, dest, elem, out);
    }

    rvv_mask_fill_tail(rv, dest, rv->csr_vl, vlmax);
    rv->csr_vstart = 0;
}

static inline void rvv_exec_vmerge_vv(riscv_t *rv,
                                      const rv_insn_t *ir,
                                      uint32_t dest)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        rvv_set_elem(rv, dest, elem, sew_bits,
                     rvv_mask_get_bit(rv, 0, elem)
                         ? rvv_get_elem(rv, ir->vs1, elem, sew_bits)
                         : rvv_get_elem(rv, ir->vs2, elem, sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_vmerge_vx(riscv_t *rv,
                                      const rv_insn_t *ir,
                                      uint32_t dest)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t scalar = rvv_trunc_elem(rv->X[ir->rs1], sew_bits);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        rvv_set_elem(rv, dest, elem, sew_bits,
                     rvv_mask_get_bit(rv, 0, elem)
                         ? scalar
                         : rvv_get_elem(rv, ir->vs2, elem, sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_vmerge_vi(riscv_t *rv,
                                      const rv_insn_t *ir,
                                      uint32_t dest)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t imm = rvv_trunc_elem((uint32_t) ir->imm, sew_bits);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        rvv_set_elem(rv, dest, elem, sew_bits,
                     rvv_mask_get_bit(rv, 0, elem)
                         ? imm
                         : rvv_get_elem(rv, ir->vs2, elem, sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline uint32_t rvv_op_srl(uint32_t lhs, uint32_t rhs, uint32_t sew_bits)
{
    lhs = rvv_trunc_elem(lhs, sew_bits);
    return lhs >> (rhs & (sew_bits - 1U));
}

static inline uint32_t rvv_op_sra(uint32_t lhs, uint32_t rhs, uint32_t sew_bits)
{
    return rvv_trunc_elem(
        (uint32_t) (rvv_signed_elem(lhs, sew_bits) >> (rhs & (sew_bits - 1U))),
        sew_bits);
}

static inline uint32_t rvv_op_adc(uint32_t lhs,
                                  uint32_t rhs,
                                  uint32_t carry_in,
                                  uint32_t sew_bits)
{
    uint64_t sum = (uint64_t) rvv_trunc_elem(lhs, sew_bits) +
                   (uint64_t) rvv_trunc_elem(rhs, sew_bits) + carry_in;
    return rvv_trunc_elem((uint32_t) sum, sew_bits);
}

static inline bool rvv_op_adc_carry(uint32_t lhs,
                                    uint32_t rhs,
                                    uint32_t carry_in,
                                    uint32_t sew_bits)
{
    uint64_t sum = (uint64_t) rvv_trunc_elem(lhs, sew_bits) +
                   (uint64_t) rvv_trunc_elem(rhs, sew_bits) + carry_in;
    return (sum >> sew_bits) & 0x1;
}

static inline uint32_t rvv_op_sbc(uint32_t lhs,
                                  uint32_t rhs,
                                  uint32_t borrow_in,
                                  uint32_t sew_bits)
{
    uint64_t lhs_u = rvv_trunc_elem(lhs, sew_bits);
    uint64_t rhs_u = rvv_trunc_elem(rhs, sew_bits) + borrow_in;
    return rvv_trunc_elem((uint32_t) (lhs_u - rhs_u), sew_bits);
}

static inline bool rvv_op_sbc_borrow(uint32_t lhs,
                                     uint32_t rhs,
                                     uint32_t borrow_in,
                                     uint32_t sew_bits)
{
    uint64_t lhs_u = rvv_trunc_elem(lhs, sew_bits);
    uint64_t rhs_u = rvv_trunc_elem(rhs, sew_bits) + borrow_in;
    return lhs_u < rhs_u;
}

/* Carry/borrow body+tail helpers - all five iterate the body over
 * [vstart, vl); prestart elements [0, vstart) stay undisturbed, and the
 * tail [vl, vlmax) is filled with the per-instruction policy:
 *   - data-result form (write_carry/write_borrow == false): apply vta
 *   - mask-result form (vmadc/vmsbc, write_carry/borrow == true): mask
 *     destinations are unconditionally tail-agnostic per V 1.0 §4.5,
 *     handled by rvv_mask_fill_tail at the end.
 */
static inline void rvv_exec_adc_vv(riscv_t *rv,
                                   const rv_insn_t *ir,
                                   uint32_t dest,
                                   bool write_carry)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint32_t carry_in = ir->vm ? 0U : rvv_mask_get_bit(rv, 0, elem);
        uint32_t lhs = rvv_get_elem(rv, ir->vs2, elem, sew_bits);
        uint32_t rhs = rvv_get_elem(rv, ir->vs1, elem, sew_bits);

        if (write_carry)
            rvv_mask_set_bit(rv, dest, elem,
                             rvv_op_adc_carry(lhs, rhs, carry_in, sew_bits));
        else
            rvv_set_elem(rv, dest, elem, sew_bits,
                         rvv_op_adc(lhs, rhs, carry_in, sew_bits));
    }
    if (write_carry)
        rvv_mask_fill_tail(rv, dest, rv->csr_vl, vlmax);
    else if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_adc_vx(riscv_t *rv,
                                   const rv_insn_t *ir,
                                   uint32_t dest,
                                   bool write_carry)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t scalar = rv->X[ir->rs1];

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint32_t carry_in = ir->vm ? 0U : rvv_mask_get_bit(rv, 0, elem);
        uint32_t lhs = rvv_get_elem(rv, ir->vs2, elem, sew_bits);

        if (write_carry)
            rvv_mask_set_bit(rv, dest, elem,
                             rvv_op_adc_carry(lhs, scalar, carry_in, sew_bits));
        else
            rvv_set_elem(rv, dest, elem, sew_bits,
                         rvv_op_adc(lhs, scalar, carry_in, sew_bits));
    }
    if (write_carry)
        rvv_mask_fill_tail(rv, dest, rv->csr_vl, vlmax);
    else if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_adc_vi(riscv_t *rv,
                                   const rv_insn_t *ir,
                                   uint32_t dest,
                                   bool write_carry)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t imm = (uint32_t) ir->imm;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint32_t carry_in = ir->vm ? 0U : rvv_mask_get_bit(rv, 0, elem);
        uint32_t lhs = rvv_get_elem(rv, ir->vs2, elem, sew_bits);

        if (write_carry)
            rvv_mask_set_bit(rv, dest, elem,
                             rvv_op_adc_carry(lhs, imm, carry_in, sew_bits));
        else
            rvv_set_elem(rv, dest, elem, sew_bits,
                         rvv_op_adc(lhs, imm, carry_in, sew_bits));
    }
    if (write_carry)
        rvv_mask_fill_tail(rv, dest, rv->csr_vl, vlmax);
    else if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_sbc_vv(riscv_t *rv,
                                   const rv_insn_t *ir,
                                   uint32_t dest,
                                   bool write_borrow)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint32_t borrow_in = ir->vm ? 0U : rvv_mask_get_bit(rv, 0, elem);
        uint32_t lhs = rvv_get_elem(rv, ir->vs2, elem, sew_bits);
        uint32_t rhs = rvv_get_elem(rv, ir->vs1, elem, sew_bits);

        if (write_borrow)
            rvv_mask_set_bit(rv, dest, elem,
                             rvv_op_sbc_borrow(lhs, rhs, borrow_in, sew_bits));
        else
            rvv_set_elem(rv, dest, elem, sew_bits,
                         rvv_op_sbc(lhs, rhs, borrow_in, sew_bits));
    }
    if (write_borrow)
        rvv_mask_fill_tail(rv, dest, rv->csr_vl, vlmax);
    else if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_sbc_vx(riscv_t *rv,
                                   const rv_insn_t *ir,
                                   uint32_t dest,
                                   bool write_borrow)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t scalar = rv->X[ir->rs1];

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint32_t borrow_in = ir->vm ? 0U : rvv_mask_get_bit(rv, 0, elem);
        uint32_t lhs = rvv_get_elem(rv, ir->vs2, elem, sew_bits);

        if (write_borrow)
            rvv_mask_set_bit(
                rv, dest, elem,
                rvv_op_sbc_borrow(lhs, scalar, borrow_in, sew_bits));
        else
            rvv_set_elem(rv, dest, elem, sew_bits,
                         rvv_op_sbc(lhs, scalar, borrow_in, sew_bits));
    }
    if (write_borrow)
        rvv_mask_fill_tail(rv, dest, rv->csr_vl, vlmax);
    else if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_reduction(riscv_t *rv,
                                      const rv_insn_t *ir,
                                      uint32_t dest,
                                      rvv_data_binop_fn op)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t elems = VLEN / sew_bits;
    uint32_t acc;

    if (rv->csr_vl == 0)
        return;

    acc = rvv_get_elem(rv, ir->vs1, 0, sew_bits);
    for (uint32_t elem = 0; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem))
            continue;
        acc = op(acc, rvv_get_elem(rv, ir->vs2, elem, sew_bits), sew_bits);
    }
    rvv_set_elem(rv, dest, 0, sew_bits, acc);
    if ((rv->csr_vtype >> 6) & 0x1) {
        for (uint32_t elem = 1; elem < elems; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, rvv_data_fill(sew_bits));
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_widen_reduction(riscv_t *rv,
                                            const rv_insn_t *ir,
                                            uint32_t dest,
                                            bool is_signed)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t wide_bits = sew_bits << 1;
    uint32_t elems;
    uint64_t acc;

    if (rv->csr_vl == 0)
        return;
    if (wide_bits > 32) {
        rvv_trap_illegal_state(rv, 0);
        return;
    }

    elems = VLEN / wide_bits;
    acc = rvv_get_elem(rv, ir->vs1, 0, wide_bits);
    for (uint32_t elem = 0; elem < rv->csr_vl; elem++) {
        uint32_t val;

        if (!rvv_mask_enabled_for_elem(rv, ir, elem))
            continue;
        val = rvv_get_elem(rv, ir->vs2, elem, sew_bits);
        acc += is_signed ? (uint64_t) (int64_t) rvv_signed_elem(val, sew_bits)
                         : (uint64_t) rvv_trunc_elem(val, sew_bits);
    }
    rvv_set_elem(rv, dest, 0, wide_bits, (uint32_t) acc);
    if ((rv->csr_vtype >> 6) & 0x1) {
        for (uint32_t elem = 1; elem < elems; elem++)
            rvv_set_elem(rv, dest, elem, wide_bits, rvv_data_fill(wide_bits));
    }
    rv->csr_vstart = 0;
}

static inline uint64_t rvv_elem_mask64(uint32_t width)
{
    return (width >= 64) ? UINT64_MAX : ((UINT64_C(1) << width) - 1U);
}

static inline int64_t rvv_sign_extend64(uint64_t value, uint32_t width)
{
    if (width >= 64)
        return (int64_t) value;
    return ((int64_t) (value << (64 - width))) >> (64 - width);
}

static inline uint64_t rvv_round_increment(riscv_t *rv,
                                           uint64_t value,
                                           uint32_t shift)
{
    uint64_t halfway, discarded, lower, guard, lsb;

    if (!shift)
        return 0;

    halfway = UINT64_C(1) << (shift - 1);
    discarded = value & ((UINT64_C(1) << shift) - 1U);
    lower = discarded & (halfway - 1U);
    guard = discarded & halfway;
    lsb = (value >> shift) & 0x1;

    switch (rv->csr_vxrm & 0x3) {
    case 0: /* rnu */
        return guard ? halfway : 0;
    case 1: /* rne */
        return (guard && (lower || lsb)) ? halfway : 0;
    case 2: /* rdn */
        return 0;
    default: /* rod */
        return (discarded && !lsb) ? ((UINT64_C(1) << shift) - 1U) : 0;
    }
}

/* Apply the vxrm rounding increment to an unsigned width-bit value, shift
 * right, then mask to width bits. Returns the FULL post-shift result so
 * callers (notably the narrowing-clip helpers) can saturation-check at the
 * source width before narrowing to SEW. Casting to uint32_t inside this
 * helper would silently lose high bits at width=64 (SEW=32 vnclipu).
 */
static inline uint64_t rvv_roundoff_unsigned(riscv_t *rv,
                                             uint64_t value,
                                             uint32_t width,
                                             uint32_t shift)
{
    uint64_t rounded = value + rvv_round_increment(rv, value, shift);
    return (rounded >> shift) & rvv_elem_mask64(width);
}

/* Signed counterpart. The sign-extension MUST happen on the rounded value
 * BEFORE the arithmetic right shift, otherwise negative results turn into
 * large positive ones (the previous code did (uint32_t) sext64(...) >>
 * shift, where C cast precedence truncated before shifting).
 */
static inline int64_t rvv_roundoff_signed(riscv_t *rv,
                                          uint64_t raw_value,
                                          uint32_t width,
                                          uint32_t shift)
{
    uint64_t sum = (raw_value + rvv_round_increment(rv, raw_value, shift)) &
                   rvv_elem_mask64(width);
    int64_t rounded = rvv_sign_extend64(sum, width);
    return rounded >> shift;
}

static inline void rvv_set_vxsat(riscv_t *rv)
{
    rv->csr_vxsat = 1;
    rv->csr_vcsr = (rv->csr_vxrm & 0x3) | (1U << 2);
}

static inline uint32_t rvv_op_vsaddu(riscv_t *rv,
                                     uint32_t lhs,
                                     uint32_t rhs,
                                     uint32_t sew_bits)
{
    uint64_t max = rvv_elem_mask64(sew_bits);
    uint64_t sum = (uint64_t) rvv_trunc_elem(lhs, sew_bits) +
                   (uint64_t) rvv_trunc_elem(rhs, sew_bits);

    if (sum > max) {
        rvv_set_vxsat(rv);
        return (uint32_t) max;
    }
    return rvv_trunc_elem((uint32_t) sum, sew_bits);
}

static inline uint32_t rvv_op_vsadd(riscv_t *rv,
                                    uint32_t lhs,
                                    uint32_t rhs,
                                    uint32_t sew_bits)
{
    int64_t min = -(INT64_C(1) << (sew_bits - 1));
    int64_t max = (INT64_C(1) << (sew_bits - 1)) - 1;
    int64_t sum = (int64_t) rvv_signed_elem(lhs, sew_bits) +
                  (int64_t) rvv_signed_elem(rhs, sew_bits);

    if (sum > max) {
        rvv_set_vxsat(rv);
        return rvv_trunc_elem((uint32_t) max, sew_bits);
    }
    if (sum < min) {
        rvv_set_vxsat(rv);
        return rvv_trunc_elem((uint32_t) min, sew_bits);
    }
    return rvv_trunc_elem((uint32_t) sum, sew_bits);
}

static inline uint32_t rvv_op_vssubu(riscv_t *rv,
                                     uint32_t lhs,
                                     uint32_t rhs,
                                     uint32_t sew_bits)
{
    uint32_t lhs_u = rvv_trunc_elem(lhs, sew_bits);
    uint32_t rhs_u = rvv_trunc_elem(rhs, sew_bits);

    if (lhs_u < rhs_u) {
        rvv_set_vxsat(rv);
        return 0;
    }
    return rvv_trunc_elem(lhs_u - rhs_u, sew_bits);
}

static inline uint32_t rvv_op_vssub(riscv_t *rv,
                                    uint32_t lhs,
                                    uint32_t rhs,
                                    uint32_t sew_bits)
{
    int64_t min = -(INT64_C(1) << (sew_bits - 1));
    int64_t max = (INT64_C(1) << (sew_bits - 1)) - 1;
    int64_t diff = (int64_t) rvv_signed_elem(lhs, sew_bits) -
                   (int64_t) rvv_signed_elem(rhs, sew_bits);

    if (diff > max) {
        rvv_set_vxsat(rv);
        return rvv_trunc_elem((uint32_t) max, sew_bits);
    }
    if (diff < min) {
        rvv_set_vxsat(rv);
        return rvv_trunc_elem((uint32_t) min, sew_bits);
    }
    return rvv_trunc_elem((uint32_t) diff, sew_bits);
}

static inline uint32_t rvv_op_vaaddu(riscv_t *rv,
                                     uint32_t lhs,
                                     uint32_t rhs,
                                     uint32_t sew_bits)
{
    uint64_t sum = (uint64_t) rvv_trunc_elem(lhs, sew_bits) +
                   (uint64_t) rvv_trunc_elem(rhs, sew_bits);
    return rvv_roundoff_unsigned(rv, sum, sew_bits, 1);
}

static inline uint32_t rvv_op_vaadd(riscv_t *rv,
                                    uint32_t lhs,
                                    uint32_t rhs,
                                    uint32_t sew_bits)
{
    uint32_t width = sew_bits + 1;
    int64_t sum = (int64_t) rvv_signed_elem(lhs, sew_bits) +
                  (int64_t) rvv_signed_elem(rhs, sew_bits);
    return rvv_trunc_elem(
        rvv_roundoff_signed(rv, (uint64_t) sum & rvv_elem_mask64(width), width,
                            1),
        sew_bits);
}

static inline uint32_t rvv_op_vasubu(riscv_t *rv,
                                     uint32_t lhs,
                                     uint32_t rhs,
                                     uint32_t sew_bits)
{
    uint32_t lhs_u = rvv_trunc_elem(lhs, sew_bits);
    uint32_t rhs_u = rvv_trunc_elem(rhs, sew_bits);
    uint64_t diff = (uint64_t) lhs_u - (uint64_t) rhs_u;
    return rvv_roundoff_unsigned(rv, diff & rvv_elem_mask64(sew_bits), sew_bits,
                                 1);
}

static inline uint32_t rvv_op_vasub(riscv_t *rv,
                                    uint32_t lhs,
                                    uint32_t rhs,
                                    uint32_t sew_bits)
{
    uint32_t width = sew_bits + 1;
    int64_t diff = (int64_t) rvv_signed_elem(lhs, sew_bits) -
                   (int64_t) rvv_signed_elem(rhs, sew_bits);
    return rvv_trunc_elem(
        rvv_roundoff_signed(rv, (uint64_t) diff & rvv_elem_mask64(width), width,
                            1),
        sew_bits);
}

static inline uint32_t rvv_op_vssrl(riscv_t *rv,
                                    uint32_t lhs,
                                    uint32_t rhs,
                                    uint32_t sew_bits)
{
    uint32_t shift = rhs & (sew_bits - 1U);
    return rvv_roundoff_unsigned(rv, rvv_trunc_elem(lhs, sew_bits), sew_bits,
                                 shift);
}

static inline uint32_t rvv_op_vssra(riscv_t *rv,
                                    uint32_t lhs,
                                    uint32_t rhs,
                                    uint32_t sew_bits)
{
    uint32_t shift = rhs & (sew_bits - 1U);
    return rvv_trunc_elem(
        rvv_roundoff_signed(rv, rvv_trunc_elem(lhs, sew_bits), sew_bits, shift),
        sew_bits);
}

static inline uint32_t rvv_op_vsmul(riscv_t *rv,
                                    uint32_t lhs,
                                    uint32_t rhs,
                                    uint32_t sew_bits)
{
    int64_t prod = (int64_t) rvv_signed_elem(lhs, sew_bits) *
                   (int64_t) rvv_signed_elem(rhs, sew_bits);
    uint32_t width = sew_bits << 1;
    int64_t rounded = rvv_roundoff_signed(
        rv, (uint64_t) prod & rvv_elem_mask64(width), width, sew_bits - 1U);
    int64_t min = -(INT64_C(1) << (sew_bits - 1));
    int64_t max = (INT64_C(1) << (sew_bits - 1)) - 1;

    if (rounded > max) {
        rvv_set_vxsat(rv);
        return rvv_trunc_elem((uint32_t) max, sew_bits);
    }
    if (rounded < min) {
        rvv_set_vxsat(rv);
        return rvv_trunc_elem((uint32_t) min, sew_bits);
    }
    return rvv_trunc_elem((uint32_t) rounded, sew_bits);
}

static inline uint64_t rvv_wop_addu(uint32_t lhs,
                                    uint32_t rhs,
                                    uint32_t sew_bits)
{
    return (uint64_t) rvv_trunc_elem(lhs, sew_bits) +
           (uint64_t) rvv_trunc_elem(rhs, sew_bits);
}

static inline uint64_t rvv_wop_add(uint32_t lhs,
                                   uint32_t rhs,
                                   uint32_t sew_bits)
{
    return rvv_trunc_elem64(
        (uint64_t) ((int64_t) rvv_signed_elem(lhs, sew_bits) +
                    (int64_t) rvv_signed_elem(rhs, sew_bits)),
        sew_bits << 1);
}

static inline uint64_t rvv_wop_subu(uint32_t lhs,
                                    uint32_t rhs,
                                    uint32_t sew_bits)
{
    return rvv_trunc_elem64((uint64_t) rvv_trunc_elem(lhs, sew_bits) -
                                (uint64_t) rvv_trunc_elem(rhs, sew_bits),
                            sew_bits << 1);
}

static inline uint64_t rvv_wop_sub(uint32_t lhs,
                                   uint32_t rhs,
                                   uint32_t sew_bits)
{
    return rvv_trunc_elem64(
        (uint64_t) ((int64_t) rvv_signed_elem(lhs, sew_bits) -
                    (int64_t) rvv_signed_elem(rhs, sew_bits)),
        sew_bits << 1);
}

static inline uint64_t rvv_wop_addu_mixed(uint64_t lhs,
                                          uint32_t rhs,
                                          uint32_t sew_bits)
{
    return rvv_trunc_elem64(lhs + (uint64_t) rvv_trunc_elem(rhs, sew_bits),
                            sew_bits << 1);
}

static inline uint64_t rvv_wop_add_mixed(uint64_t lhs,
                                         uint32_t rhs,
                                         uint32_t sew_bits)
{
    return rvv_trunc_elem64(
        lhs + (uint64_t) (int64_t) rvv_signed_elem(rhs, sew_bits),
        sew_bits << 1);
}

static inline uint64_t rvv_wop_subu_mixed(uint64_t lhs,
                                          uint32_t rhs,
                                          uint32_t sew_bits)
{
    return rvv_trunc_elem64(lhs - (uint64_t) rvv_trunc_elem(rhs, sew_bits),
                            sew_bits << 1);
}

static inline uint64_t rvv_wop_sub_mixed(uint64_t lhs,
                                         uint32_t rhs,
                                         uint32_t sew_bits)
{
    return rvv_trunc_elem64(
        lhs - (uint64_t) (int64_t) rvv_signed_elem(rhs, sew_bits),
        sew_bits << 1);
}

static inline uint64_t rvv_wop_mulu(uint32_t lhs,
                                    uint32_t rhs,
                                    uint32_t sew_bits)
{
    return (uint64_t) rvv_trunc_elem(lhs, sew_bits) *
           (uint64_t) rvv_trunc_elem(rhs, sew_bits);
}

static inline uint64_t rvv_wop_mulsu(uint32_t lhs,
                                     uint32_t rhs,
                                     uint32_t sew_bits)
{
    return rvv_trunc_elem64(
        (uint64_t) ((int64_t) rvv_signed_elem(lhs, sew_bits) *
                    (int64_t) rvv_trunc_elem(rhs, sew_bits)),
        sew_bits << 1);
}

static inline uint64_t rvv_wop_mulus(uint32_t lhs,
                                     uint32_t rhs,
                                     uint32_t sew_bits)
{
    return rvv_trunc_elem64(
        (uint64_t) ((int64_t) rvv_trunc_elem(lhs, sew_bits) *
                    (int64_t) rvv_signed_elem(rhs, sew_bits)),
        sew_bits << 1);
}

static inline uint64_t rvv_wop_mul(uint32_t lhs,
                                   uint32_t rhs,
                                   uint32_t sew_bits)
{
    return rvv_trunc_elem64(
        (uint64_t) ((int64_t) rvv_signed_elem(lhs, sew_bits) *
                    (int64_t) rvv_signed_elem(rhs, sew_bits)),
        sew_bits << 1);
}

static inline void rvv_exec_wide_narrow_vv(riscv_t *rv,
                                           const rv_insn_t *ir,
                                           uint32_t dest,
                                           rvv_wide_narrow_binop_fn op)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t wide_bits = sew_bits << 1;
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint64_t fill = rvv_elem_mask64(wide_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem_ext(rv, dest, elem, wide_bits, fill);
            continue;
        }
        rvv_set_elem_ext(
            rv, dest, elem, wide_bits,
            op(rvv_get_elem(rv, ir->vs2, elem, sew_bits),
               rvv_get_elem(rv, ir->vs1, elem, sew_bits), sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem_ext(rv, dest, elem, wide_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_wide_narrow_vx(riscv_t *rv,
                                           const rv_insn_t *ir,
                                           uint32_t dest,
                                           rvv_wide_narrow_binop_fn op)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t wide_bits = sew_bits << 1;
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint64_t fill = rvv_elem_mask64(wide_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t scalar = rvv_trunc_elem(rv->X[ir->rs1], sew_bits);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem_ext(rv, dest, elem, wide_bits, fill);
            continue;
        }
        rvv_set_elem_ext(
            rv, dest, elem, wide_bits,
            op(rvv_get_elem(rv, ir->vs2, elem, sew_bits), scalar, sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem_ext(rv, dest, elem, wide_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_wide_mixed_vv(riscv_t *rv,
                                          const rv_insn_t *ir,
                                          uint32_t dest,
                                          rvv_wide_mixed_binop_fn op)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t wide_bits = sew_bits << 1;
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint64_t fill = rvv_elem_mask64(wide_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem_ext(rv, dest, elem, wide_bits, fill);
            continue;
        }
        rvv_set_elem_ext(
            rv, dest, elem, wide_bits,
            op(rvv_get_elem_ext(rv, ir->vs2, elem, wide_bits),
               rvv_get_elem(rv, ir->vs1, elem, sew_bits), sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem_ext(rv, dest, elem, wide_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_wide_mixed_vx(riscv_t *rv,
                                          const rv_insn_t *ir,
                                          uint32_t dest,
                                          rvv_wide_mixed_binop_fn op)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t wide_bits = sew_bits << 1;
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint64_t fill = rvv_elem_mask64(wide_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t scalar = rvv_trunc_elem(rv->X[ir->rs1], sew_bits);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem_ext(rv, dest, elem, wide_bits, fill);
            continue;
        }
        rvv_set_elem_ext(rv, dest, elem, wide_bits,
                         op(rvv_get_elem_ext(rv, ir->vs2, elem, wide_bits),
                            scalar, sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem_ext(rv, dest, elem, wide_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline uint32_t rvv_nop_vnsrl(riscv_t *rv,
                                     uint64_t value,
                                     uint32_t shift,
                                     uint32_t sew_bits)
{
    (void) rv;
    return rvv_trunc_elem((uint32_t) (value >> shift), sew_bits);
}

static inline uint32_t rvv_nop_vnsra(riscv_t *rv,
                                     uint64_t value,
                                     uint32_t shift,
                                     uint32_t sew_bits)
{
    (void) rv;
    return rvv_trunc_elem(
        (uint32_t) (rvv_sign_extend64(value, sew_bits << 1) >> shift),
        sew_bits);
}

static inline uint32_t rvv_nop_vnclipu(riscv_t *rv,
                                       uint64_t value,
                                       uint32_t shift,
                                       uint32_t sew_bits)
{
    /* Saturation-check at the unshifted source width (up to 64 bits for
     * SEW=32) BEFORE truncating to SEW; otherwise high bits silently wrap
     * and vxsat never gets set. */
    uint64_t rounded = rvv_roundoff_unsigned(rv, value, sew_bits << 1, shift);
    uint64_t max = rvv_elem_mask64(sew_bits);

    if (rounded > max) {
        rvv_set_vxsat(rv);
        return (uint32_t) max;
    }
    return rvv_trunc_elem((uint32_t) rounded, sew_bits);
}

static inline uint32_t rvv_nop_vnclip(riscv_t *rv,
                                      uint64_t value,
                                      uint32_t shift,
                                      uint32_t sew_bits)
{
    /* Same width-preservation rule as vnclipu, but signed: rvv_roundoff_signed
     * now returns int64_t with arithmetic shift applied at full width, so the
     * saturation check sees the true magnitude of negative results. */
    int64_t rounded = rvv_roundoff_signed(rv, value, sew_bits << 1, shift);
    int64_t min = -(INT64_C(1) << (sew_bits - 1));
    int64_t max = (INT64_C(1) << (sew_bits - 1)) - 1;

    if (rounded > max) {
        rvv_set_vxsat(rv);
        return rvv_trunc_elem((uint32_t) max, sew_bits);
    }
    if (rounded < min) {
        rvv_set_vxsat(rv);
        return rvv_trunc_elem((uint32_t) min, sew_bits);
    }
    return rvv_trunc_elem((uint32_t) rounded, sew_bits);
}

static inline void rvv_exec_narrow_shift_vv(riscv_t *rv,
                                            const rv_insn_t *ir,
                                            uint32_t dest,
                                            rvv_narrow_shift_op_fn op)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t wide_bits = sew_bits << 1;
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, sew_bits, fill);
            continue;
        }
        rvv_set_elem(
            rv, dest, elem, sew_bits,
            op(rv, rvv_get_elem_ext(rv, ir->vs2, elem, wide_bits),
               rvv_get_elem(rv, ir->vs1, elem, sew_bits) & (wide_bits - 1U),
               sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_narrow_shift_vx(riscv_t *rv,
                                            const rv_insn_t *ir,
                                            uint32_t dest,
                                            rvv_narrow_shift_op_fn op)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t wide_bits = sew_bits << 1;
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t shift = rv->X[ir->rs1] & (wide_bits - 1U);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, sew_bits, fill);
            continue;
        }
        rvv_set_elem(rv, dest, elem, sew_bits,
                     op(rv, rvv_get_elem_ext(rv, ir->vs2, elem, wide_bits),
                        shift, sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_narrow_shift_vi(riscv_t *rv,
                                            const rv_insn_t *ir,
                                            uint32_t dest,
                                            rvv_narrow_shift_op_fn op)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t wide_bits = sew_bits << 1;
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t shift = ((uint32_t) ir->imm) & (wide_bits - 1U);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, sew_bits, fill);
            continue;
        }
        rvv_set_elem(rv, dest, elem, sew_bits,
                     op(rv, rvv_get_elem_ext(rv, ir->vs2, elem, wide_bits),
                        shift, sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline uint32_t rvv_op_vmacc(uint32_t dest,
                                    uint32_t lhs,
                                    uint32_t rhs,
                                    uint32_t sew_bits)
{
    return rvv_trunc_elem(dest + (lhs * rhs), sew_bits);
}

static inline uint32_t rvv_op_vnmsac(uint32_t dest,
                                     uint32_t lhs,
                                     uint32_t rhs,
                                     uint32_t sew_bits)
{
    return rvv_trunc_elem(dest - (lhs * rhs), sew_bits);
}

/* vmadd / vnmsub differ from vmacc / vnmsac in WHICH operand the multiply
 * uses for vd_in. Per V 1.0 §11.13:
 *   vmadd.vv:  vd = (vs1 * vd) + vs2
 *   vnmsub.vv: vd = vs2 - (vs1 * vd)
 *
 * The exec helpers call op(dest=vd, lhs=vs2, rhs=vs1, sew). So the spec
 * formulas translate to:
 *   vmadd:  rhs * dest + lhs   (vs1 * vd + vs2)
 *   vnmsub: lhs - rhs * dest   (vs2 - vs1 * vd)
 *
 * The earlier code computed rhs + dest*lhs / rhs - dest*lhs which puts vd
 * on the wrong side of the multiply.
 */
static inline uint32_t rvv_op_vmadd(uint32_t dest,
                                    uint32_t lhs,
                                    uint32_t rhs,
                                    uint32_t sew_bits)
{
    return rvv_trunc_elem((rhs * dest) + lhs, sew_bits);
}

static inline uint32_t rvv_op_vnmsub(uint32_t dest,
                                     uint32_t lhs,
                                     uint32_t rhs,
                                     uint32_t sew_bits)
{
    return rvv_trunc_elem(lhs - (rhs * dest), sew_bits);
}

static inline void rvv_exec_mac_vv(
    riscv_t *rv,
    const rv_insn_t *ir,
    uint32_t dest,
    uint32_t (*op)(uint32_t, uint32_t, uint32_t, uint32_t))
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, sew_bits, fill);
            continue;
        }
        rvv_set_elem(rv, dest, elem, sew_bits,
                     op(rvv_get_elem(rv, dest, elem, sew_bits),
                        rvv_get_elem(rv, ir->vs2, elem, sew_bits),
                        rvv_get_elem(rv, ir->vs1, elem, sew_bits), sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_mac_vx(
    riscv_t *rv,
    const rv_insn_t *ir,
    uint32_t dest,
    uint32_t (*op)(uint32_t, uint32_t, uint32_t, uint32_t))
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t scalar = rvv_trunc_elem(rv->X[ir->rs1], sew_bits);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, sew_bits, fill);
            continue;
        }
        rvv_set_elem(
            rv, dest, elem, sew_bits,
            op(rvv_get_elem(rv, dest, elem, sew_bits),
               rvv_get_elem(rv, ir->vs2, elem, sew_bits), scalar, sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_wide_mac_vv(riscv_t *rv,
                                        const rv_insn_t *ir,
                                        uint32_t dest,
                                        rvv_wide_narrow_binop_fn mulop)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t wide_bits = sew_bits << 1;
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint64_t fill = rvv_elem_mask64(wide_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint64_t acc;

        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem_ext(rv, dest, elem, wide_bits, fill);
            continue;
        }
        acc = rvv_get_elem_ext(rv, dest, elem, wide_bits);
        rvv_set_elem_ext(
            rv, dest, elem, wide_bits,
            rvv_trunc_elem64(
                acc + mulop(rvv_get_elem(rv, ir->vs2, elem, sew_bits),
                            rvv_get_elem(rv, ir->vs1, elem, sew_bits),
                            sew_bits),
                wide_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem_ext(rv, dest, elem, wide_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_wide_mac_vx(riscv_t *rv,
                                        const rv_insn_t *ir,
                                        uint32_t dest,
                                        rvv_wide_narrow_binop_fn mulop)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t wide_bits = sew_bits << 1;
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint64_t fill = rvv_elem_mask64(wide_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t scalar = rvv_trunc_elem(rv->X[ir->rs1], sew_bits);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint64_t acc;

        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem_ext(rv, dest, elem, wide_bits, fill);
            continue;
        }
        acc = rvv_get_elem_ext(rv, dest, elem, wide_bits);
        rvv_set_elem_ext(
            rv, dest, elem, wide_bits,
            rvv_trunc_elem64(
                acc + mulop(rvv_get_elem(rv, ir->vs2, elem, sew_bits), scalar,
                            sew_bits),
                wide_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem_ext(rv, dest, elem, wide_bits, fill);
    }

    rv->csr_vstart = 0;
}

/* vmv.s.x - scalar to vector element-0 move.
 *
 * Per V 1.0 §16.1, this instruction has a UNIQUE tail policy: only vd[0]
 * is updated, and "all elements past 0 are unchanged" REGARDLESS of vta.
 * Do not apply the standard tail-agnostic fill here.
 *
 * If vstart >= 1 or vl == 0 the spec says no element is updated.
 */
static inline void rvv_exec_vmv_s_x(riscv_t *rv, const rv_insn_t *ir)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t value = rvv_trunc_elem(rv->X[ir->rs1], sew_bits);

    if ((rv->csr_vstart == 0) && (rv->csr_vl > 0))
        rvv_set_elem(rv, ir->vd, 0, sew_bits, value);
    rv->csr_vstart = 0;
}

static inline void rvv_exec_vmv_x_s(riscv_t *rv, const rv_insn_t *ir)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);

    rv->X[ir->rd] = (uint32_t) rvv_signed_elem(
        rvv_get_elem(rv, ir->vs2, 0, sew_bits), sew_bits);
    rv->csr_vstart = 0;
}

static inline void rvv_exec_vrgather_vv(riscv_t *rv,
                                        const rv_insn_t *ir,
                                        uint32_t dest,
                                        uint32_t index_eew)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint32_t index;

        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, sew_bits, fill);
            continue;
        }
        index = rvv_get_elem(rv, ir->vs1, elem, index_eew);
        rvv_set_elem(
            rv, dest, elem, sew_bits,
            (index >= vlmax) ? 0 : rvv_get_elem(rv, ir->vs2, index, sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_vrgather_scalar(riscv_t *rv,
                                            const rv_insn_t *ir,
                                            uint32_t dest,
                                            uint32_t index)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t value =
        (index >= vlmax) ? 0 : rvv_get_elem(rv, ir->vs2, index, sew_bits);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, sew_bits, fill);
            continue;
        }
        rvv_set_elem(rv, dest, elem, sew_bits, value);
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_vslideup(riscv_t *rv,
                                     const rv_insn_t *ir,
                                     uint32_t dest,
                                     uint32_t offset)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t start = rv->csr_vstart;

    if (offset < start)
        offset = start;
    for (uint32_t elem = offset; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, sew_bits, fill);
            continue;
        }
        rvv_set_elem(rv, dest, elem, sew_bits,
                     rvv_get_elem(rv, ir->vs2, elem - offset, sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_vslidedown(riscv_t *rv,
                                       const rv_insn_t *ir,
                                       uint32_t dest,
                                       uint32_t offset)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint32_t src_idx = elem + offset;

        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, sew_bits, fill);
            continue;
        }
        rvv_set_elem(rv, dest, elem, sew_bits,
                     (src_idx >= vlmax)
                         ? 0
                         : rvv_get_elem(rv, ir->vs2, src_idx, sew_bits));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_vslide1up(riscv_t *rv,
                                      const rv_insn_t *ir,
                                      uint32_t dest)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t scalar = rvv_trunc_elem(rv->X[ir->rs1], sew_bits);

    if (rv->csr_vstart < rv->csr_vl) {
        if (rv->csr_vstart == 0) {
            if (rvv_mask_enabled_for_elem(rv, ir, 0))
                rvv_set_elem(rv, dest, 0, sew_bits, scalar);
            else if (vma)
                rvv_set_elem(rv, dest, 0, sew_bits, fill);
        }

        for (uint32_t elem = rv->csr_vstart > 1 ? rv->csr_vstart : 1;
             elem < rv->csr_vl; elem++) {
            if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
                if (vma)
                    rvv_set_elem(rv, dest, elem, sew_bits, fill);
                continue;
            }
            rvv_set_elem(rv, dest, elem, sew_bits,
                         rvv_get_elem(rv, ir->vs2, elem - 1, sew_bits));
        }
    }

    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_vslide1down(riscv_t *rv,
                                        const rv_insn_t *ir,
                                        uint32_t dest)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t scalar = rvv_trunc_elem(rv->X[ir->rs1], sew_bits);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, sew_bits, fill);
            continue;
        }
        if ((elem + 1) < rv->csr_vl)
            rvv_set_elem(rv, dest, elem, sew_bits,
                         rvv_get_elem(rv, ir->vs2, elem + 1, sew_bits));
        else
            rvv_set_elem(rv, dest, elem, sew_bits, scalar);
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}

static inline void rvv_exec_vcompress(riscv_t *rv,
                                      const rv_insn_t *ir,
                                      uint32_t dest)
{
    uint32_t sew_bits = rvv_sew_bits(rv->csr_vtype);
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t fill = rvv_data_fill(sew_bits);
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t out_idx = 0;

    for (uint32_t elem = 0; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_get_bit(rv, ir->vs1, elem))
            continue;
        rvv_set_elem(rv, dest, out_idx++, sew_bits,
                     rvv_get_elem(rv, ir->vs2, elem, sew_bits));
    }
    if (vta) {
        for (uint32_t elem = out_idx; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, sew_bits, fill);
    }

    rv->csr_vstart = 0;
}
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

/* Apply the result of a vset{i}vl{i} family instruction.
 *
 * Per V 1.0 §6.4 the vl-update rules depend on the (rd, rs1) operand pair:
 *   rs1 != x0           -> vl = min(AVL, VLMAX) via rvv_compute_vl
 *   rs1 == x0, rd != x0 -> vl = VLMAX  (AVL is treated as ~0)
 *   rs1 == x0, rd == x0 -> vl unchanged ("keep vl"); only vtype is updated
 *
 * The keep-vl form is reserved unless the new configuration preserves the
 * current VLMAX. We therefore reject any keep-vl transition that starts
 * from vill/unsupported state or changes VLMAX; silently clamping via
 * rvv_compute_vl would create impossible architectural states instead.
 */
static inline void rvv_apply_vsetvl(riscv_t *rv,
                                    const rv_insn_t *ir,
                                    uint32_t new_vtype,
                                    uint32_t avl,
                                    bool keep_vl)
{
    uint32_t old_vlmax = 0;

    if (keep_vl) {
        if (!rvv_require_valid_state(rv)) {
            rvv_set_vill(rv);
            rv->X[ir->rd] = rv->csr_vl;
            return;
        }
        old_vlmax = rvv_vlmax(rv->csr_vtype);
    }

    if (!rvv_vtype_is_supported(new_vtype)) {
        rvv_set_vill(rv);
        rv->X[ir->rd] = rv->csr_vl;
        return;
    }

    if (keep_vl && (rvv_vlmax(new_vtype) != old_vlmax)) {
        rvv_set_vill(rv);
        rv->X[ir->rd] = rv->csr_vl;
        return;
    }

    rv->csr_vtype = new_vtype;
    if (!keep_vl)
        rv->csr_vl = rvv_compute_vl(avl, rvv_vlmax(new_vtype));
    rv->X[ir->rd] = rv->csr_vl;
}

RVOP(vsetvli, {
    bool keep_vl = (ir->rs1 == 0) && (ir->rd == 0);
    uint32_t avl = ir->rs1 ? rv->X[ir->rs1] : UINT32_MAX;
    rvv_apply_vsetvl(rv, ir, ir->zimm, avl, keep_vl);
})

RVOP(vsetivli, {
    /* vsetivli encodes a 5-bit immediate in the rs1 field; rd == x0 with
     * a zero immediate is a normal "vl = 0" request, not the keep-vl form
     * (which only exists for vsetvli/vsetvl per spec §6.4).
     */
    rvv_apply_vsetvl(rv, ir, ir->zimm, ir->rs1, false);
})

RVOP(vsetvl, {
    bool keep_vl = (ir->rs1 == 0) && (ir->rd == 0);
    uint32_t avl = ir->rs1 ? rv->X[ir->rs1] : UINT32_MAX;
    rvv_apply_vsetvl(rv, ir, rv->X[ir->rs2], avl, keep_vl);
})

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
        if (rvv_require_operable(rv))                    \
            return false;                                \
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
#define GET_FILL_VALUE(sew_bits)            \
    ((sew_bits) == 8    ? AGNOSTIC_FILL_8b  \
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
     : (vma) ? (((fill) & (MASK)) << ((k) << (SHIFT)))              \
             : (tmp_d & ((uint32_t) (MASK) << ((k) << (SHIFT)))))

#define VI_LOOP(des, op1, op2, op, SHIFT, MASK, i, j, itr, vm)            \
    uint32_t tmp_1 = rv->V[(op1) + (j)][i];                               \
    uint32_t tmp_d = rv->V[(des) + (j)][i];                               \
    uint32_t out = 0;                                                     \
    for (uint8_t k = 0; k < (itr); k++) {                                 \
        out |= V_LANE_VALUE(op_##op((tmp_1 >> (k << (SHIFT))), (op2)), k, \
                            SHIFT, MASK);                                 \
    }                                                                     \
    rv->V[(des) + (j)][i] = out;

/* Partial-tail word: 0 < body_count < itr body lanes followed by tail
 * lanes within the same word. Caller (sew_*b_handler) guarantees this
 * word is valid (j < num_regs). When body_count == 0 the macro is a
 * no-op; the caller then fills the entirely-tail word.
 */
#define VI_LOOP_LEFT(des, op1, op2, op, SHIFT, MASK, i, j, itr, vm)       \
    uint32_t tmp_1 = rv->V[(op1) + (j)][i];                               \
    uint32_t tmp_d = rv->V[(des) + (j)][i];                               \
    uint8_t body_count = rv->csr_vl % (itr);                              \
    if (body_count > 0 && vta) {                                          \
        uint32_t body_mask = (1U << (body_count << (SHIFT))) - 1;         \
        rv->V[(des) + (j)][i] =                                           \
            (rv->V[(des) + (j)][i] & body_mask) | ~body_mask;             \
    }                                                                     \
    for (uint8_t k = 0; k < body_count; k++) {                            \
        assert(((des) + (j)) < 32);                                       \
        uint32_t lane = V_LANE_VALUE(                                     \
            op_##op((tmp_1 >> (k << (SHIFT))), (op2)), k, SHIFT, MASK);   \
        rv->V[(des) + (j)][i] =                                           \
            (rv->V[(des) + (j)][i] & ~((MASK) << (k << (SHIFT)))) | lane; \
    }

#define VV_LOOP(des, op1, op2, op, SHIFT, MASK, i, j, itr, vm)                \
    uint32_t tmp_1 = rv->V[(op1) + (j)][i];                                   \
    uint32_t tmp_2 = rv->V[(op2) + (j)][i];                                   \
    uint32_t tmp_d = rv->V[(des) + (j)][i];                                   \
    uint32_t out = 0;                                                         \
    for (uint8_t k = 0; k < (itr); k++) {                                     \
        out |= V_LANE_VALUE(                                                  \
            op_##op((tmp_1 >> (k << (SHIFT))), (tmp_2 >> (k << (SHIFT)))), k, \
            SHIFT, MASK);                                                     \
    }                                                                         \
    rv->V[(des) + (j)][i] = out;

#define VV_LOOP_LEFT(des, op1, op2, op, SHIFT, MASK, i, j, itr, vm)           \
    uint32_t tmp_1 = rv->V[(op1) + (j)][i];                                   \
    uint32_t tmp_2 = rv->V[(op2) + (j)][i];                                   \
    uint32_t tmp_d = rv->V[(des) + (j)][i];                                   \
    uint8_t body_count = rv->csr_vl % (itr);                                  \
    if (body_count > 0 && vta) {                                              \
        uint32_t body_mask = (1U << (body_count << (SHIFT))) - 1;             \
        rv->V[(des) + (j)][i] =                                               \
            (rv->V[(des) + (j)][i] & body_mask) | ~body_mask;                 \
    }                                                                         \
    for (uint8_t k = 0; k < body_count; k++) {                                \
        assert(((des) + (j)) < 32);                                           \
        uint32_t lane = V_LANE_VALUE(                                         \
            op_##op((tmp_1 >> (k << (SHIFT))), (tmp_2 >> (k << (SHIFT)))), k, \
            SHIFT, MASK);                                                     \
        rv->V[(des) + (j)][i] =                                               \
            (rv->V[(des) + (j)][i] & ~((MASK) << (k << (SHIFT)))) | lane;     \
    }

#define VX_LOOP(des, op1, op2, op, SHIFT, MASK, i, j, itr, vm)            \
    uint32_t tmp_1 = rv->V[(op1) + (j)][i];                               \
    uint32_t tmp_2 = rv->X[op2];                                          \
    uint32_t tmp_d = rv->V[(des) + (j)][i];                               \
    uint32_t out = 0;                                                     \
    for (uint8_t k = 0; k < (itr); k++) {                                 \
        out |= V_LANE_VALUE(op_##op((tmp_1 >> (k << (SHIFT))), tmp_2), k, \
                            SHIFT, MASK);                                 \
    }                                                                     \
    rv->V[(des) + (j)][i] = out;

#define VX_LOOP_LEFT(des, op1, op2, op, SHIFT, MASK, i, j, itr, vm)       \
    uint32_t tmp_1 = rv->V[(op1) + (j)][i];                               \
    uint32_t tmp_2 = rv->X[op2];                                          \
    uint32_t tmp_d = rv->V[(des) + (j)][i];                               \
    uint8_t body_count = rv->csr_vl % (itr);                              \
    if (body_count > 0 && vta) {                                          \
        uint32_t body_mask = (1U << (body_count << (SHIFT))) - 1;         \
        rv->V[(des) + (j)][i] =                                           \
            (rv->V[(des) + (j)][i] & body_mask) | ~body_mask;             \
    }                                                                     \
    for (uint8_t k = 0; k < body_count; k++) {                            \
        assert(((des) + (j)) < 32);                                       \
        uint32_t lane = V_LANE_VALUE(                                     \
            op_##op((tmp_1 >> (k << (SHIFT))), tmp_2), k, SHIFT, MASK);   \
        rv->V[(des) + (j)][i] =                                           \
            (rv->V[(des) + (j)][i] & ~((MASK) << (k << (SHIFT)))) | lane; \
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

#define sew_16b_handler(des, op1, op2, op, op_type)                        \
    {                                                                      \
        uint8_t vma = GET_VMA(rv->csr_vtype);                              \
        uint8_t vta = GET_VTA(rv->csr_vtype);                              \
        uint8_t v_lmul = GET_VLMUL(rv->csr_vtype);                         \
        uint8_t num_regs = (v_lmul < 4) ? (1 << v_lmul) : 1;               \
        uint32_t fill = AGNOSTIC_FILL_16b;                                 \
        uint8_t __i = 0;                                                   \
        uint8_t __j = 0;                                                   \
        uint8_t __m = 0;                                                   \
        uint32_t vm = rv->V[0][__m];                                       \
        for (uint32_t __k = 0; (rv->csr_vl - __k) >= 2;) {                 \
            __i %= VREG_U32_COUNT;                                         \
            assert((des + __j) < 32);                                      \
            op_type##_LOOP(des, op1, op2, op, 4, 0xFFFF, __i, __j, 2, vm); \
            __k += 2;                                                      \
            __i++;                                                         \
            if (!(__k % (VREG_U32_COUNT << 1))) {                          \
                __j++;                                                     \
                __i = 0;                                                   \
            }                                                              \
            vm >>= 2;                                                      \
            if (!(__k % 32)) {                                             \
                __m++;                                                     \
                vm = rv->V[0][__m];                                        \
            }                                                              \
        }                                                                  \
        if (__j < num_regs) {                                              \
            op_type##_LOOP_LEFT(des, op1, op2, op, 4, 0xFFFF, __i, __j, 2, \
                                vm);                                       \
            if (vta) {                                                     \
                if (rv->csr_vl % 2)                                        \
                    __i++;                                                 \
                V_FILL_LMUL_TAIL(des, num_regs);                           \
            }                                                              \
        }                                                                  \
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

RVOP(vle8_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_eew_reg(rv, ir->eew, ir->vd))
        return rvv_trap_illegal_state(rv, 0);
    rvv_unit_stride_load(rv, ir, ir->vd, rv->X[ir->rs1]);
})

RVOP(vle16_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_eew_reg(rv, ir->eew, ir->vd))
        return rvv_trap_illegal_state(rv, 0);
    rvv_unit_stride_load(rv, ir, ir->vd, rv->X[ir->rs1]);
})

RVOP(vle32_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_eew_reg(rv, ir->eew, ir->vd))
        return rvv_trap_illegal_state(rv, 0);
    rvv_unit_stride_load(rv, ir, ir->vd, rv->X[ir->rs1]);
})

RVOP(vle64_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_eew_reg(rv, ir->eew, ir->vd))
        return rvv_trap_illegal_state(rv, 0);
    rvv_unit_stride_load(rv, ir, ir->vd, rv->X[ir->rs1]);
})

RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg2e8_v, 2, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg3e8_v, 3, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg4e8_v, 4, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg5e8_v, 5, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg6e8_v, 6, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg7e8_v, 7, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg8e8_v, 8, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg2e16_v, 2, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg3e16_v, 3, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg4e16_v, 4, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg5e16_v, 5, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg6e16_v, 6, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg7e16_v, 7, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg8e16_v, 8, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg2e32_v, 2, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg3e32_v, 3, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg4e32_v, 4, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg5e32_v, 5, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg6e32_v, 6, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg7e32_v, 7, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg8e32_v, 8, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg2e64_v, 2, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg3e64_v, 3, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg4e64_v, 4, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg5e64_v, 5, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg6e64_v, 6, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg7e64_v, 7, false);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg8e64_v, 8, false);

RVOP(vl1re8_v, {
    if (!rvv_validate_reg_span(ir->vd, 1))
        return rvv_trap_illegal_state(rv, 0);
    rvv_whole_reg_load(rv, ir->vd, 1, rv->X[ir->rs1]);
})

RVOP(vl1re16_v, {
    if (!rvv_validate_reg_span(ir->vd, 1))
        return rvv_trap_illegal_state(rv, 0);
    rvv_whole_reg_load(rv, ir->vd, 1, rv->X[ir->rs1]);
})

RVOP(vl1re32_v, {
    if (!rvv_validate_reg_span(ir->vd, 1))
        return rvv_trap_illegal_state(rv, 0);
    rvv_whole_reg_load(rv, ir->vd, 1, rv->X[ir->rs1]);
})

RVOP(vl1re64_v, {
    if (!rvv_validate_reg_span(ir->vd, 1))
        return rvv_trap_illegal_state(rv, 0);
    rvv_whole_reg_load(rv, ir->vd, 1, rv->X[ir->rs1]);
})

RVOP(vl2re8_v, {
    if (!rvv_validate_reg_span(ir->vd, 2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_whole_reg_load(rv, ir->vd, 2, rv->X[ir->rs1]);
})

RVOP(vl2re16_v, {
    if (!rvv_validate_reg_span(ir->vd, 2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_whole_reg_load(rv, ir->vd, 2, rv->X[ir->rs1]);
})
RVOP(vl2re32_v, {
    if (!rvv_validate_reg_span(ir->vd, 2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_whole_reg_load(rv, ir->vd, 2, rv->X[ir->rs1]);
})

RVOP(vl2re64_v, {
    if (!rvv_validate_reg_span(ir->vd, 2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_whole_reg_load(rv, ir->vd, 2, rv->X[ir->rs1]);
})

RVOP(vl4re8_v, {
    if (!rvv_validate_reg_span(ir->vd, 4))
        return rvv_trap_illegal_state(rv, 0);
    rvv_whole_reg_load(rv, ir->vd, 4, rv->X[ir->rs1]);
})

RVOP(vl4re16_v, {
    if (!rvv_validate_reg_span(ir->vd, 4))
        return rvv_trap_illegal_state(rv, 0);
    rvv_whole_reg_load(rv, ir->vd, 4, rv->X[ir->rs1]);
})

RVOP(vl4re32_v, {
    if (!rvv_validate_reg_span(ir->vd, 4))
        return rvv_trap_illegal_state(rv, 0);
    rvv_whole_reg_load(rv, ir->vd, 4, rv->X[ir->rs1]);
})

RVOP(vl4re64_v, {
    if (!rvv_validate_reg_span(ir->vd, 4))
        return rvv_trap_illegal_state(rv, 0);
    rvv_whole_reg_load(rv, ir->vd, 4, rv->X[ir->rs1]);
})

RVOP(vl8re8_v, {
    if (!rvv_validate_reg_span(ir->vd, 8))
        return rvv_trap_illegal_state(rv, 0);
    rvv_whole_reg_load(rv, ir->vd, 8, rv->X[ir->rs1]);
})

RVOP(vl8re16_v, {
    if (!rvv_validate_reg_span(ir->vd, 8))
        return rvv_trap_illegal_state(rv, 0);
    rvv_whole_reg_load(rv, ir->vd, 8, rv->X[ir->rs1]);
})

RVOP(vl8re32_v, {
    if (!rvv_validate_reg_span(ir->vd, 8))
        return rvv_trap_illegal_state(rv, 0);
    rvv_whole_reg_load(rv, ir->vd, 8, rv->X[ir->rs1]);
})

RVOP(vl8re64_v, {
    if (!rvv_validate_reg_span(ir->vd, 8))
        return rvv_trap_illegal_state(rv, 0);
    rvv_whole_reg_load(rv, ir->vd, 8, rv->X[ir->rs1]);
})

RVOP(vlm_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_reg_span(ir->vd, 1))
        return rvv_trap_illegal_state(rv, 0);
    rvv_mask_load(rv, ir->vd, rv->X[ir->rs1]);
})

RVOP(vle8ff_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_eew_reg(rv, ir->eew, ir->vd))
        return rvv_trap_illegal_state(rv, 0);
    if (!rvv_unit_stride_load_ff(rv, ir, ir->vd, rv->X[ir->rs1]))
        return false;
})

RVOP(vle16ff_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_eew_reg(rv, ir->eew, ir->vd))
        return rvv_trap_illegal_state(rv, 0);
    if (!rvv_unit_stride_load_ff(rv, ir, ir->vd, rv->X[ir->rs1]))
        return false;
})

RVOP(vle32ff_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_eew_reg(rv, ir->eew, ir->vd))
        return rvv_trap_illegal_state(rv, 0);
    if (!rvv_unit_stride_load_ff(rv, ir, ir->vd, rv->X[ir->rs1]))
        return false;
})

RVOP(vle64ff_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_eew_reg(rv, ir->eew, ir->vd))
        return rvv_trap_illegal_state(rv, 0);
    if (!rvv_unit_stride_load_ff(rv, ir, ir->vd, rv->X[ir->rs1]))
        return false;
})

RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg2e8ff_v, 2, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg3e8ff_v, 3, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg4e8ff_v, 4, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg5e8ff_v, 5, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg6e8ff_v, 6, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg7e8ff_v, 7, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg8e8ff_v, 8, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg2e16ff_v, 2, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg3e16ff_v, 3, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg4e16ff_v, 4, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg5e16ff_v, 5, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg6e16ff_v, 6, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg7e16ff_v, 7, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg8e16ff_v, 8, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg2e32ff_v, 2, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg3e32ff_v, 3, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg4e32ff_v, 4, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg5e32ff_v, 5, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg6e32ff_v, 6, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg7e32ff_v, 7, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg8e32ff_v, 8, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg2e64ff_v, 2, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg3e64ff_v, 3, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg4e64ff_v, 4, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg5e64ff_v, 5, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg6e64ff_v, 6, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg7e64ff_v, 7, true);
RVV_SEGMENT_UNIT_STRIDE_LOAD_OP(vlseg8e64ff_v, 8, true);

RVOP(vluxei8_v, {
    uint32_t span;
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_index_group_span(rv, ir->eew, &span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_indexed_load(rv, ir, ir->vd, rv->X[ir->rs1]);
})

RVOP(vluxei16_v, {
    uint32_t span;
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_index_group_span(rv, ir->eew, &span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_indexed_load(rv, ir, ir->vd, rv->X[ir->rs1]);
})

RVOP(vluxei32_v, {
    uint32_t span;
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_index_group_span(rv, ir->eew, &span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_indexed_load(rv, ir, ir->vd, rv->X[ir->rs1]);
})

RVOP(vluxei64_v, {
    uint32_t span;
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_index_group_span(rv, ir->eew, &span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_indexed_load(rv, ir, ir->vd, rv->X[ir->rs1]);
})

RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg2ei8_v, 2);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg3ei8_v, 3);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg4ei8_v, 4);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg5ei8_v, 5);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg6ei8_v, 6);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg7ei8_v, 7);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg8ei8_v, 8);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg2ei16_v, 2);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg3ei16_v, 3);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg4ei16_v, 4);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg5ei16_v, 5);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg6ei16_v, 6);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg7ei16_v, 7);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg8ei16_v, 8);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg2ei32_v, 2);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg3ei32_v, 3);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg4ei32_v, 4);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg5ei32_v, 5);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg6ei32_v, 6);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg7ei32_v, 7);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg8ei32_v, 8);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg2ei64_v, 2);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg3ei64_v, 3);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg4ei64_v, 4);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg5ei64_v, 5);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg6ei64_v, 6);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg7ei64_v, 7);
RVV_SEGMENT_INDEXED_LOAD_OP(vluxseg8ei64_v, 8);

RVOP(vlse8_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_eew_reg(rv, ir->eew, ir->vd))
        return rvv_trap_illegal_state(rv, 0);
    rvv_strided_load(rv, ir, ir->vd, rv->X[ir->rs1]);
})

RVOP(vlse16_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_eew_reg(rv, ir->eew, ir->vd))
        return rvv_trap_illegal_state(rv, 0);
    rvv_strided_load(rv, ir, ir->vd, rv->X[ir->rs1]);
})

RVOP(vlse32_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_eew_reg(rv, ir->eew, ir->vd))
        return rvv_trap_illegal_state(rv, 0);
    rvv_strided_load(rv, ir, ir->vd, rv->X[ir->rs1]);
})

RVOP(vlse64_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_eew_reg(rv, ir->eew, ir->vd))
        return rvv_trap_illegal_state(rv, 0);
    rvv_strided_load(rv, ir, ir->vd, rv->X[ir->rs1]);
})

RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg2e8_v, 2);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg3e8_v, 3);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg4e8_v, 4);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg5e8_v, 5);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg6e8_v, 6);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg7e8_v, 7);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg8e8_v, 8);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg2e16_v, 2);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg3e16_v, 3);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg4e16_v, 4);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg5e16_v, 5);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg6e16_v, 6);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg7e16_v, 7);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg8e16_v, 8);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg2e32_v, 2);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg3e32_v, 3);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg4e32_v, 4);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg5e32_v, 5);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg6e32_v, 6);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg7e32_v, 7);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg8e32_v, 8);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg2e64_v, 2);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg3e64_v, 3);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg4e64_v, 4);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg5e64_v, 5);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg6e64_v, 6);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg7e64_v, 7);
RVV_SEGMENT_STRIDED_LOAD_OP(vlsseg8e64_v, 8);

RVOP(vloxei8_v, {
    uint32_t span;
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_index_group_span(rv, ir->eew, &span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_indexed_load(rv, ir, ir->vd, rv->X[ir->rs1]);
})

RVOP(vloxei16_v, {
    uint32_t span;
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_index_group_span(rv, ir->eew, &span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_indexed_load(rv, ir, ir->vd, rv->X[ir->rs1]);
})

RVOP(vloxei32_v, {
    uint32_t span;
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_index_group_span(rv, ir->eew, &span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_indexed_load(rv, ir, ir->vd, rv->X[ir->rs1]);
})

RVOP(vloxei64_v, {
    uint32_t span;
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_index_group_span(rv, ir->eew, &span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_indexed_load(rv, ir, ir->vd, rv->X[ir->rs1]);
})

RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg2ei8_v, 2);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg3ei8_v, 3);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg4ei8_v, 4);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg5ei8_v, 5);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg6ei8_v, 6);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg7ei8_v, 7);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg8ei8_v, 8);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg2ei16_v, 2);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg3ei16_v, 3);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg4ei16_v, 4);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg5ei16_v, 5);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg6ei16_v, 6);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg7ei16_v, 7);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg8ei16_v, 8);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg2ei32_v, 2);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg3ei32_v, 3);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg4ei32_v, 4);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg5ei32_v, 5);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg6ei32_v, 6);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg7ei32_v, 7);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg8ei32_v, 8);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg2ei64_v, 2);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg3ei64_v, 3);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg4ei64_v, 4);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg5ei64_v, 5);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg6ei64_v, 6);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg7ei64_v, 7);
RVV_SEGMENT_INDEXED_LOAD_OP(vloxseg8ei64_v, 8);

RVOP(vse8_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_eew_reg(rv, ir->eew, ir->vs3))
        return rvv_trap_illegal_state(rv, 0);
    rvv_unit_stride_store(rv, ir, ir->vs3, rv->X[ir->rs1]);
})

RVOP(vse16_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_eew_reg(rv, ir->eew, ir->vs3))
        return rvv_trap_illegal_state(rv, 0);
    rvv_unit_stride_store(rv, ir, ir->vs3, rv->X[ir->rs1]);
})

RVOP(vse32_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_eew_reg(rv, ir->eew, ir->vs3))
        return rvv_trap_illegal_state(rv, 0);
    rvv_unit_stride_store(rv, ir, ir->vs3, rv->X[ir->rs1]);
})

RVOP(vse64_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_eew_reg(rv, ir->eew, ir->vs3))
        return rvv_trap_illegal_state(rv, 0);
    rvv_unit_stride_store(rv, ir, ir->vs3, rv->X[ir->rs1]);
})

RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg2e8_v, 2);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg3e8_v, 3);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg4e8_v, 4);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg5e8_v, 5);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg6e8_v, 6);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg7e8_v, 7);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg8e8_v, 8);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg2e16_v, 2);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg3e16_v, 3);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg4e16_v, 4);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg5e16_v, 5);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg6e16_v, 6);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg7e16_v, 7);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg8e16_v, 8);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg2e32_v, 2);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg3e32_v, 3);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg4e32_v, 4);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg5e32_v, 5);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg6e32_v, 6);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg7e32_v, 7);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg8e32_v, 8);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg2e64_v, 2);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg3e64_v, 3);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg4e64_v, 4);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg5e64_v, 5);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg6e64_v, 6);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg7e64_v, 7);
RVV_SEGMENT_UNIT_STRIDE_STORE_OP(vsseg8e64_v, 8);

RVOP(vs1r_v, {
    if (!rvv_validate_reg_span(ir->vs3, 1))
        return rvv_trap_illegal_state(rv, 0);
    rvv_whole_reg_store(rv, ir->vs3, 1, rv->X[ir->rs1]);
})

RVOP(vs2r_v, {
    if (!rvv_validate_reg_span(ir->vs3, 2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_whole_reg_store(rv, ir->vs3, 2, rv->X[ir->rs1]);
})

RVOP(vs4r_v, {
    if (!rvv_validate_reg_span(ir->vs3, 4))
        return rvv_trap_illegal_state(rv, 0);
    rvv_whole_reg_store(rv, ir->vs3, 4, rv->X[ir->rs1]);
})

RVOP(vs8r_v, {
    if (!rvv_validate_reg_span(ir->vs3, 8))
        return rvv_trap_illegal_state(rv, 0);
    rvv_whole_reg_store(rv, ir->vs3, 8, rv->X[ir->rs1]);
})

RVOP(vsm_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_reg_span(ir->vs3, 1))
        return rvv_trap_illegal_state(rv, 0);
    rvv_mask_store(rv, ir->vs3, rv->X[ir->rs1]);
})

RVOP(vsuxei8_v, {
    uint32_t span;
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vs3) ||
        !rvv_index_group_span(rv, ir->eew, &span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_indexed_store(rv, ir, ir->vs3, rv->X[ir->rs1]);
})

RVOP(vsuxei16_v, {
    uint32_t span;
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vs3) ||
        !rvv_index_group_span(rv, ir->eew, &span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_indexed_store(rv, ir, ir->vs3, rv->X[ir->rs1]);
})

RVOP(vsuxei32_v, {
    uint32_t span;
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vs3) ||
        !rvv_index_group_span(rv, ir->eew, &span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_indexed_store(rv, ir, ir->vs3, rv->X[ir->rs1]);
})

RVOP(vsuxei64_v, {
    uint32_t span;
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vs3) ||
        !rvv_index_group_span(rv, ir->eew, &span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_indexed_store(rv, ir, ir->vs3, rv->X[ir->rs1]);
})

RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg2ei8_v, 2);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg3ei8_v, 3);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg4ei8_v, 4);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg5ei8_v, 5);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg6ei8_v, 6);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg7ei8_v, 7);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg8ei8_v, 8);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg2ei16_v, 2);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg3ei16_v, 3);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg4ei16_v, 4);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg5ei16_v, 5);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg6ei16_v, 6);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg7ei16_v, 7);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg8ei16_v, 8);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg2ei32_v, 2);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg3ei32_v, 3);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg4ei32_v, 4);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg5ei32_v, 5);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg6ei32_v, 6);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg7ei32_v, 7);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg8ei32_v, 8);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg2ei64_v, 2);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg3ei64_v, 3);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg4ei64_v, 4);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg5ei64_v, 5);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg6ei64_v, 6);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg7ei64_v, 7);
RVV_SEGMENT_INDEXED_STORE_OP(vsuxseg8ei64_v, 8);

RVOP(vsse8_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_eew_reg(rv, ir->eew, ir->vs3))
        return rvv_trap_illegal_state(rv, 0);
    rvv_strided_store(rv, ir, ir->vs3, rv->X[ir->rs1]);
})

RVOP(vsse16_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_eew_reg(rv, ir->eew, ir->vs3))
        return rvv_trap_illegal_state(rv, 0);
    rvv_strided_store(rv, ir, ir->vs3, rv->X[ir->rs1]);
})

RVOP(vsse32_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_eew_reg(rv, ir->eew, ir->vs3))
        return rvv_trap_illegal_state(rv, 0);
    rvv_strided_store(rv, ir, ir->vs3, rv->X[ir->rs1]);
})

RVOP(vsse64_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_eew_reg(rv, ir->eew, ir->vs3))
        return rvv_trap_illegal_state(rv, 0);
    rvv_strided_store(rv, ir, ir->vs3, rv->X[ir->rs1]);
})

RVV_SEGMENT_STRIDED_STORE_OP(vssseg2e8_v, 2);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg3e8_v, 3);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg4e8_v, 4);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg5e8_v, 5);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg6e8_v, 6);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg7e8_v, 7);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg8e8_v, 8);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg2e16_v, 2);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg3e16_v, 3);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg4e16_v, 4);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg5e16_v, 5);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg6e16_v, 6);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg7e16_v, 7);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg8e16_v, 8);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg2e32_v, 2);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg3e32_v, 3);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg4e32_v, 4);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg5e32_v, 5);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg6e32_v, 6);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg7e32_v, 7);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg8e32_v, 8);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg2e64_v, 2);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg3e64_v, 3);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg4e64_v, 4);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg5e64_v, 5);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg6e64_v, 6);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg7e64_v, 7);
RVV_SEGMENT_STRIDED_STORE_OP(vssseg8e64_v, 8);

RVOP(vsoxei8_v, {
    uint32_t span;
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vs3) ||
        !rvv_index_group_span(rv, ir->eew, &span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_indexed_store(rv, ir, ir->vs3, rv->X[ir->rs1]);
})

RVOP(vsoxei16_v, {
    uint32_t span;
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vs3) ||
        !rvv_index_group_span(rv, ir->eew, &span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_indexed_store(rv, ir, ir->vs3, rv->X[ir->rs1]);
})

RVOP(vsoxei32_v, {
    uint32_t span;
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vs3) ||
        !rvv_index_group_span(rv, ir->eew, &span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_indexed_store(rv, ir, ir->vs3, rv->X[ir->rs1]);
})

RVOP(vsoxei64_v, {
    uint32_t span;
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vs3) ||
        !rvv_index_group_span(rv, ir->eew, &span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_indexed_store(rv, ir, ir->vs3, rv->X[ir->rs1]);
})

RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg2ei8_v, 2);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg3ei8_v, 3);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg4ei8_v, 4);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg5ei8_v, 5);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg6ei8_v, 6);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg7ei8_v, 7);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg8ei8_v, 8);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg2ei16_v, 2);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg3ei16_v, 3);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg4ei16_v, 4);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg5ei16_v, 5);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg6ei16_v, 6);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg7ei16_v, 7);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg8ei16_v, 8);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg2ei32_v, 2);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg3ei32_v, 3);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg4ei32_v, 4);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg5ei32_v, 5);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg6ei32_v, 6);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg7ei32_v, 7);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg8ei32_v, 8);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg2ei64_v, 2);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg3ei64_v, 3);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg4ei64_v, 4);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg5ei64_v, 5);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg6ei64_v, 6);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg7ei64_v, 7);
RVV_SEGMENT_INDEXED_STORE_OP(vsoxseg8ei64_v, 8);

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

RVOP(vminu_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv(rv, ir, ir->vd, rvv_op_minu);
})

RVOP(vminu_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx(rv, ir, ir->vd, rvv_op_minu);
})

RVOP(vmin_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv(rv, ir, ir->vd, rvv_op_min);
})

RVOP(vmin_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx(rv, ir, ir->vd, rvv_op_min);
})

RVOP(vmaxu_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv(rv, ir, ir->vd, rvv_op_maxu);
})

RVOP(vmaxu_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx(rv, ir, ir->vd, rvv_op_maxu);
})

RVOP(vmax_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv(rv, ir, ir->vd, rvv_op_max);
})

RVOP(vmax_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx(rv, ir, ir->vd, rvv_op_max);
})

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

RVOP(vrgather_vv, {
    uint32_t span = rvv_group_regs(rv->csr_vtype);

    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_reg_span(ir->vd, span) ||
        !rvv_validate_reg_span(ir->vs1, span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_reg_spans_overlap(ir->vd, span, ir->vs1, span) ||
        rvv_reg_spans_overlap(ir->vd, span, ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_vrgather_vv(rv, ir, ir->vd, rvv_sew_bits(rv->csr_vtype));
})

RVOP(vrgather_vx, {
    uint32_t span = rvv_group_regs(rv->csr_vtype);

    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_reg_span(ir->vd, span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_reg_spans_overlap(ir->vd, span, ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_vrgather_scalar(rv, ir, ir->vd, rv->X[ir->rs1]);
})

RVOP(vrgather_vi, {
    uint32_t span = rvv_group_regs(rv->csr_vtype);

    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_reg_span(ir->vd, span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_reg_spans_overlap(ir->vd, span, ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_vrgather_scalar(rv, ir, ir->vd, (uint32_t) (ir->imm & 0x1f));
})

RVOP(vslideup_vx, {
    uint32_t span = rvv_group_regs(rv->csr_vtype);

    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_reg_span(ir->vd, span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_reg_spans_overlap(ir->vd, span, ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_vslideup(rv, ir, ir->vd, rv->X[ir->rs1]);
})

RVOP(vslideup_vi, {
    uint32_t span = rvv_group_regs(rv->csr_vtype);

    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_reg_span(ir->vd, span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_reg_spans_overlap(ir->vd, span, ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_vslideup(rv, ir, ir->vd, (uint32_t) (ir->imm & 0x1f));
})

RVOP(vrgatherei16_vv, {
    uint32_t data_span = rvv_group_regs(rv->csr_vtype);
    uint32_t index_span;

    if (rvv_require_operable(rv))
        return false;
    if (!rvv_index_group_span(rv, 16, &index_span) ||
        !rvv_validate_reg_span(ir->vd, data_span) ||
        !rvv_validate_reg_span(ir->vs2, data_span) ||
        !rvv_validate_reg_span(ir->vs1, index_span))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_reg_spans_overlap(ir->vd, data_span, ir->vs2, data_span) ||
        rvv_reg_spans_overlap(ir->vd, data_span, ir->vs1, index_span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_vrgather_vv(rv, ir, ir->vd, 16);
})

RVOP(vslidedown_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_vslidedown(rv, ir, ir->vd, rv->X[ir->rs1]);
})

RVOP(vslidedown_vi, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_vslidedown(rv, ir, ir->vd, (uint32_t) (ir->imm & 0x1f));
})

RVOP(vadc_vvm, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_adc_vv(rv, ir, ir->vd, false);
})

RVOP(vadc_vxm, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_adc_vx(rv, ir, ir->vd, false);
})

RVOP(vadc_vim, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_adc_vi(rv, ir, ir->vd, false);
})

RVOP(vmadc_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs1) || rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_adc_vv(rv, ir, ir->vd, true);
})

RVOP(vmadc_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_adc_vx(rv, ir, ir->vd, true);
})

RVOP(vmadc_vi, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_adc_vi(rv, ir, ir->vd, true);
})

RVOP(vsbc_vvm, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_sbc_vv(rv, ir, ir->vd, false);
})

RVOP(vsbc_vxm, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_sbc_vx(rv, ir, ir->vd, false);
})

RVOP(vmsbc_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs1) || rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_sbc_vv(rv, ir, ir->vd, true);
})

RVOP(vmsbc_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_sbc_vx(rv, ir, ir->vd, true);
})

RVOP(vmerge_vvm, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_vmerge_vv(rv, ir, ir->vd);
})

RVOP(vmerge_vxm, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_vmerge_vx(rv, ir, ir->vd);
})

RVOP(vmerge_vim, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_vmerge_vi(rv, ir, ir->vd);
})

#define op_vmv(a, b) (((a) & 0) + (b))
RVOP(vmv_v_v, {VECTOR_DISPATCH(ir->vd, 0, ir->vs1, vmv, VV)})

RVOP(vmv_v_x, {VECTOR_DISPATCH(ir->vd, 0, ir->rs1, vmv, VX)})

RVOP(vmv_v_i, {VECTOR_DISPATCH(ir->vd, 0, ir->imm, vmv, VI)})
#undef op_vmv

RVOP(vmseq_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs1) || rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_mask_compare_vv(rv, ir, ir->vd, rvv_cmp_eq);
})

RVOP(vmseq_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_mask_compare_vx(rv, ir, ir->vd, rvv_cmp_eq);
})

RVOP(vmseq_vi, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_mask_compare_vi(rv, ir, ir->vd, rvv_cmp_eq);
})

RVOP(vmsne_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs1) || rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_mask_compare_vv(rv, ir, ir->vd, rvv_cmp_ne);
})

RVOP(vmsne_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_mask_compare_vx(rv, ir, ir->vd, rvv_cmp_ne);
})

RVOP(vmsne_vi, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_mask_compare_vi(rv, ir, ir->vd, rvv_cmp_ne);
})

RVOP(vmsltu_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs1) || rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_mask_compare_vv(rv, ir, ir->vd, rvv_cmp_ltu);
})

RVOP(vmsltu_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_mask_compare_vx(rv, ir, ir->vd, rvv_cmp_ltu);
})

RVOP(vmslt_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs1) || rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_mask_compare_vv(rv, ir, ir->vd, rvv_cmp_lt);
})

RVOP(vmslt_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_mask_compare_vx(rv, ir, ir->vd, rvv_cmp_lt);
})

RVOP(vmsleu_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs1) || rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_mask_compare_vv(rv, ir, ir->vd, rvv_cmp_leu);
})

RVOP(vmsleu_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_mask_compare_vx(rv, ir, ir->vd, rvv_cmp_leu);
})

RVOP(vmsleu_vi, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_mask_compare_vi(rv, ir, ir->vd, rvv_cmp_leu);
})

RVOP(vmsle_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs1) || rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_mask_compare_vv(rv, ir, ir->vd, rvv_cmp_le);
})

RVOP(vmsle_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_mask_compare_vx(rv, ir, ir->vd, rvv_cmp_le);
})

RVOP(vmsle_vi, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_mask_compare_vi(rv, ir, ir->vd, rvv_cmp_le);
})

RVOP(vmsgtu_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_mask_compare_vx_rev(rv, ir, ir->vd, rvv_cmp_ltu);
})

RVOP(vmsgtu_vi, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_mask_compare_vi_rev(rv, ir, ir->vd, rvv_cmp_ltu);
})

RVOP(vmsgt_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_mask_compare_vx_rev(rv, ir, ir->vd, rvv_cmp_lt);
})

RVOP(vmsgt_vi, {
    if (rvv_require_operable(rv))
        return false;
    if (rvv_check_data_reg(rv, ir->vs2))
        return false;
    rvv_exec_mask_compare_vi_rev(rv, ir, ir->vd, rvv_cmp_lt);
})

RVOP(vsaddu_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv_stateful(rv, ir, ir->vd, rvv_op_vsaddu);
})

RVOP(vsaddu_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx_stateful(rv, ir, ir->vd, rvv_op_vsaddu);
})

RVOP(vsaddu_vi, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vi_stateful(rv, ir, ir->vd, rvv_op_vsaddu);
})

RVOP(vsadd_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv_stateful(rv, ir, ir->vd, rvv_op_vsadd);
})

RVOP(vsadd_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx_stateful(rv, ir, ir->vd, rvv_op_vsadd);
})

RVOP(vsadd_vi, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vi_stateful(rv, ir, ir->vd, rvv_op_vsadd);
})

RVOP(vssubu_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv_stateful(rv, ir, ir->vd, rvv_op_vssubu);
})

RVOP(vssubu_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx_stateful(rv, ir, ir->vd, rvv_op_vssubu);
})

RVOP(vssub_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv_stateful(rv, ir, ir->vd, rvv_op_vssub);
})

RVOP(vssub_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx_stateful(rv, ir, ir->vd, rvv_op_vssub);
})

#define op_sll(a, b) \
    ((a) << ((b) & ((8 << ((rv->csr_vtype >> 3) & 0b111)) - 1)))
RVOP(vsll_vv, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->vs1, sll, VV)})

RVOP(vsll_vx, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->rs1, sll, VX)})

RVOP(vsll_vi, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->imm, sll, VI)})
#undef op_sll

RVOP(vsmul_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv_stateful(rv, ir, ir->vd, rvv_op_vsmul);
})

RVOP(vsmul_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx_stateful(rv, ir, ir->vd, rvv_op_vsmul);
})

RVOP(vsrl_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv(rv, ir, ir->vd, rvv_op_srl);
})

RVOP(vsrl_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx(rv, ir, ir->vd, rvv_op_srl);
})

RVOP(vsrl_vi, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vi(rv, ir, ir->vd, rvv_op_srl);
})

RVOP(vsra_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv(rv, ir, ir->vd, rvv_op_sra);
})

RVOP(vsra_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx(rv, ir, ir->vd, rvv_op_sra);
})

RVOP(vsra_vi, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vi(rv, ir, ir->vd, rvv_op_sra);
})

RVOP(vssrl_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv_stateful(rv, ir, ir->vd, rvv_op_vssrl);
})

RVOP(vssrl_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx_stateful(rv, ir, ir->vd, rvv_op_vssrl);
})

RVOP(vssrl_vi, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vi_stateful(rv, ir, ir->vd, rvv_op_vssrl);
})

RVOP(vssra_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv_stateful(rv, ir, ir->vd, rvv_op_vssra);
})

RVOP(vssra_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx_stateful(rv, ir, ir->vd, rvv_op_vssra);
})

RVOP(vssra_vi, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vi_stateful(rv, ir, ir->vd, rvv_op_vssra);
})

RVOP(vnsrl_wv, {
    uint32_t wide_span;
    uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_wide_group_span(rv, &wide_span) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_wide_reg(rv, ir->vs2) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_cross_eew_overlap_illegal(ir->vd, narrow_span, ir->vs2, wide_span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_narrow_shift_vv(rv, ir, ir->vd, rvv_nop_vnsrl);
})

RVOP(vnsrl_wx, {
    uint32_t wide_span;
    uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_wide_group_span(rv, &wide_span) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_wide_reg(rv, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_cross_eew_overlap_illegal(ir->vd, narrow_span, ir->vs2, wide_span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_narrow_shift_vx(rv, ir, ir->vd, rvv_nop_vnsrl);
})

RVOP(vnsrl_wi, {
    uint32_t wide_span;
    uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_wide_group_span(rv, &wide_span) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_wide_reg(rv, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_cross_eew_overlap_illegal(ir->vd, narrow_span, ir->vs2, wide_span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_narrow_shift_vi(rv, ir, ir->vd, rvv_nop_vnsrl);
})

RVOP(vnsra_wv, {
    uint32_t wide_span;
    uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_wide_group_span(rv, &wide_span) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_wide_reg(rv, ir->vs2) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_cross_eew_overlap_illegal(ir->vd, narrow_span, ir->vs2, wide_span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_narrow_shift_vv(rv, ir, ir->vd, rvv_nop_vnsra);
})

RVOP(vnsra_wx, {
    uint32_t wide_span;
    uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_wide_group_span(rv, &wide_span) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_wide_reg(rv, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_cross_eew_overlap_illegal(ir->vd, narrow_span, ir->vs2, wide_span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_narrow_shift_vx(rv, ir, ir->vd, rvv_nop_vnsra);
})

RVOP(vnsra_wi, {
    uint32_t wide_span;
    uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_wide_group_span(rv, &wide_span) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_wide_reg(rv, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_cross_eew_overlap_illegal(ir->vd, narrow_span, ir->vs2, wide_span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_narrow_shift_vi(rv, ir, ir->vd, rvv_nop_vnsra);
})

RVOP(vnclipu_wv, {
    uint32_t wide_span;
    uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_wide_group_span(rv, &wide_span) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_wide_reg(rv, ir->vs2) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_cross_eew_overlap_illegal(ir->vd, narrow_span, ir->vs2, wide_span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_narrow_shift_vv(rv, ir, ir->vd, rvv_nop_vnclipu);
})

RVOP(vnclipu_wx, {
    uint32_t wide_span;
    uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_wide_group_span(rv, &wide_span) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_wide_reg(rv, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_cross_eew_overlap_illegal(ir->vd, narrow_span, ir->vs2, wide_span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_narrow_shift_vx(rv, ir, ir->vd, rvv_nop_vnclipu);
})

RVOP(vnclipu_wi, {
    uint32_t wide_span;
    uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_wide_group_span(rv, &wide_span) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_wide_reg(rv, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_cross_eew_overlap_illegal(ir->vd, narrow_span, ir->vs2, wide_span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_narrow_shift_vi(rv, ir, ir->vd, rvv_nop_vnclipu);
})

RVOP(vnclip_wv, {
    uint32_t wide_span;
    uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_wide_group_span(rv, &wide_span) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_wide_reg(rv, ir->vs2) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_cross_eew_overlap_illegal(ir->vd, narrow_span, ir->vs2, wide_span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_narrow_shift_vv(rv, ir, ir->vd, rvv_nop_vnclip);
})

RVOP(vnclip_wx, {
    uint32_t wide_span;
    uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_wide_group_span(rv, &wide_span) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_wide_reg(rv, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_cross_eew_overlap_illegal(ir->vd, narrow_span, ir->vs2, wide_span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_narrow_shift_vx(rv, ir, ir->vd, rvv_nop_vnclip);
})

RVOP(vnclip_wi, {
    uint32_t wide_span;
    uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_wide_group_span(rv, &wide_span) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_wide_reg(rv, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_cross_eew_overlap_illegal(ir->vd, narrow_span, ir->vs2, wide_span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_narrow_shift_vi(rv, ir, ir->vd, rvv_nop_vnclip);
})

RVOP(vwredsumu_vs, {
    if (rvv_require_operable(rv))
        return false;
    if (rv->csr_vstart)
        return rvv_trap_illegal_state(rv, 0);
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_widen_reduction(rv, ir, ir->vd, false);
})

RVOP(vwredsum_vs, {
    if (rvv_require_operable(rv))
        return false;
    if (rv->csr_vstart)
        return rvv_trap_illegal_state(rv, 0);
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_widen_reduction(rv, ir, ir->vd, true);
})



RVOP(vredsum_vs, {
    if (rvv_require_operable(rv))
        return false;
    if (rv->csr_vstart)
        return rvv_trap_illegal_state(rv, 0);
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_reduction(rv, ir, ir->vd, rvv_op_add);
})

RVOP(vredand_vs, {
    if (rvv_require_operable(rv))
        return false;
    if (rv->csr_vstart)
        return rvv_trap_illegal_state(rv, 0);
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_reduction(rv, ir, ir->vd, rvv_op_and);
})

RVOP(vredor_vs, {
    if (rvv_require_operable(rv))
        return false;
    if (rv->csr_vstart)
        return rvv_trap_illegal_state(rv, 0);
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_reduction(rv, ir, ir->vd, rvv_op_or);
})

RVOP(vredxor_vs, {
    if (rvv_require_operable(rv))
        return false;
    if (rv->csr_vstart)
        return rvv_trap_illegal_state(rv, 0);
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_reduction(rv, ir, ir->vd, rvv_op_xor);
})

RVOP(vredminu_vs, {
    if (rvv_require_operable(rv))
        return false;
    if (rv->csr_vstart)
        return rvv_trap_illegal_state(rv, 0);
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_reduction(rv, ir, ir->vd, rvv_op_minu);
})

RVOP(vredmin_vs, {
    if (rvv_require_operable(rv))
        return false;
    if (rv->csr_vstart)
        return rvv_trap_illegal_state(rv, 0);
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_reduction(rv, ir, ir->vd, rvv_op_min);
})

RVOP(vredmaxu_vs, {
    if (rvv_require_operable(rv))
        return false;
    if (rv->csr_vstart)
        return rvv_trap_illegal_state(rv, 0);
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_reduction(rv, ir, ir->vd, rvv_op_maxu);
})

RVOP(vredmax_vs, {
    if (rvv_require_operable(rv))
        return false;
    if (rv->csr_vstart)
        return rvv_trap_illegal_state(rv, 0);
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_reduction(rv, ir, ir->vd, rvv_op_max);
})

RVOP(vaaddu_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv_stateful(rv, ir, ir->vd, rvv_op_vaaddu);
})

RVOP(vaaddu_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx_stateful(rv, ir, ir->vd, rvv_op_vaaddu);
})

RVOP(vaadd_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv_stateful(rv, ir, ir->vd, rvv_op_vaadd);
})

RVOP(vaadd_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx_stateful(rv, ir, ir->vd, rvv_op_vaadd);
})

RVOP(vasubu_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv_stateful(rv, ir, ir->vd, rvv_op_vasubu);
})

RVOP(vasubu_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx_stateful(rv, ir, ir->vd, rvv_op_vasubu);
})

RVOP(vasub_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv_stateful(rv, ir, ir->vd, rvv_op_vasub);
})

RVOP(vasub_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx_stateful(rv, ir, ir->vd, rvv_op_vasub);
})

RVOP(vslide1up_vx, {
    uint32_t span = rvv_group_regs(rv->csr_vtype);

    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_reg_span(ir->vd, span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_reg_spans_overlap(ir->vd, span, ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_vslide1up(rv, ir, ir->vd);
})

RVOP(vslide1down_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_vslide1down(rv, ir, ir->vd);
})

RVOP(vcompress_vm, {
    uint32_t span = rvv_group_regs(rv->csr_vtype);

    if (rvv_require_operable(rv))
        return false;
    if (!ir->vm)
        return rvv_trap_illegal_state(rv, 0);
    /* vcompress requires vstart == 0 (V 1.0 §16.4); raise the trap
     * without touching csr_vstart so the trap handler observes the
     * architectural vstart value. */
    if (rv->csr_vstart)
        return rvv_trap_illegal_state(rv, 0);
    if (!rvv_validate_reg_span(ir->vd, span) ||
        !rvv_validate_reg_span(ir->vs2, span))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_reg_spans_overlap(ir->vd, span, ir->vs2, span) ||
        rvv_reg_spans_overlap(ir->vd, span, ir->vs1, 1))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_vcompress(rv, ir, ir->vd);
})

RVOP(vmandn_mm, {
    if (rvv_require_operable(rv))
        return false;
    if (!ir->vm)
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_mask_logical(rv, ir->vd, ir->vs2, ir->vs1, 0);
})

RVOP(vmand_mm, {
    if (rvv_require_operable(rv))
        return false;
    if (!ir->vm)
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_mask_logical(rv, ir->vd, ir->vs2, ir->vs1, 1);
})

RVOP(vmor_mm, {
    if (rvv_require_operable(rv))
        return false;
    if (!ir->vm)
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_mask_logical(rv, ir->vd, ir->vs2, ir->vs1, 2);
})

RVOP(vmxor_mm, {
    if (rvv_require_operable(rv))
        return false;
    if (!ir->vm)
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_mask_logical(rv, ir->vd, ir->vs2, ir->vs1, 3);
})

RVOP(vmorn_mm, {
    if (rvv_require_operable(rv))
        return false;
    if (!ir->vm)
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_mask_logical(rv, ir->vd, ir->vs2, ir->vs1, 4);
})

RVOP(vmnand_mm, {
    if (rvv_require_operable(rv))
        return false;
    if (!ir->vm)
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_mask_logical(rv, ir->vd, ir->vs2, ir->vs1, 5);
})

RVOP(vmnor_mm, {
    if (rvv_require_operable(rv))
        return false;
    if (!ir->vm)
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_mask_logical(rv, ir->vd, ir->vs2, ir->vs1, 6);
})

RVOP(vmxnor_mm, {
    if (rvv_require_operable(rv))
        return false;
    if (!ir->vm)
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_mask_logical(rv, ir->vd, ir->vs2, ir->vs1, 7);
})

RVOP(vdivu_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv(rv, ir, ir->vd, rvv_op_divu);
})

RVOP(vdivu_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx(rv, ir, ir->vd, rvv_op_divu);
})

RVOP(vdiv_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv(rv, ir, ir->vd, rvv_op_div);
})

RVOP(vdiv_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx(rv, ir, ir->vd, rvv_op_div);
})

RVOP(vremu_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv(rv, ir, ir->vd, rvv_op_remu);
})

RVOP(vremu_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx(rv, ir, ir->vd, rvv_op_remu);
})

RVOP(vrem_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv(rv, ir, ir->vd, rvv_op_rem);
})

RVOP(vrem_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx(rv, ir, ir->vd, rvv_op_rem);
})

RVOP(vmulhu_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv(rv, ir, ir->vd, rvv_op_mulhu);
})

RVOP(vmulhu_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx(rv, ir, ir->vd, rvv_op_mulhu);
})

#define op_mul(a, b) ((a) * (b))
RVOP(vmul_vv, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->vs1, mul, VV)})

RVOP(vmul_vx, {VECTOR_DISPATCH(ir->vd, ir->vs2, ir->rs1, mul, VX)})
#undef op_mul

RVOP(vmulhsu_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv(rv, ir, ir->vd, rvv_op_mulhsu);
})

RVOP(vmulhsu_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx(rv, ir, ir->vd, rvv_op_mulhsu);
})

RVOP(vmulh_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vv(rv, ir, ir->vd, rvv_op_mulh);
})

RVOP(vmulh_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_data_vx(rv, ir, ir->vd, rvv_op_mulh);
})

RVOP(vmadd_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_mac_vv(rv, ir, ir->vd, rvv_op_vmadd);
})

RVOP(vmadd_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_mac_vx(rv, ir, ir->vd, rvv_op_vmadd);
})

RVOP(vnmsub_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_mac_vv(rv, ir, ir->vd, rvv_op_vnmsub);
})

RVOP(vnmsub_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_mac_vx(rv, ir, ir->vd, rvv_op_vnmsub);
})

RVOP(vmacc_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_mac_vv(rv, ir, ir->vd, rvv_op_vmacc);
})

RVOP(vmacc_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_mac_vx(rv, ir, ir->vd, rvv_op_vmacc);
})

RVOP(vnmsac_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_mac_vv(rv, ir, ir->vd, rvv_op_vnmsac);
})

RVOP(vnmsac_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_mac_vx(rv, ir, ir->vd, rvv_op_vnmsac);
})

RVOP(vwaddu_vv, {
    uint32_t wide_span;
    uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_wide_group_span(rv, &wide_span) ||
        !rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span, ir->vs1,
                                      narrow_span) ||
        rvv_cross_eew_overlap_illegal(ir->vd, wide_span, ir->vs2, narrow_span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_wide_narrow_vv(rv, ir, ir->vd, rvv_wop_addu);
})

RVOP(vwaddu_vx, {
    uint32_t wide_span;
    uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_wide_group_span(rv, &wide_span) ||
        !rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span, ir->vs2, narrow_span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_wide_narrow_vx(rv, ir, ir->vd, rvv_wop_addu);
})

RVOP(vwadd_vv, {
    uint32_t wide_span;
    uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_wide_group_span(rv, &wide_span) ||
        !rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span, ir->vs1,
                                      narrow_span) ||
        rvv_cross_eew_overlap_illegal(ir->vd, wide_span, ir->vs2, narrow_span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_wide_narrow_vv(rv, ir, ir->vd, rvv_wop_add);
})

RVOP(vwadd_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    {
        uint32_t wide_span_cx;
        uint32_t narrow_span_cx = rvv_group_regs(rv->csr_vtype);
        if (!rvv_wide_group_span(rv, &wide_span_cx))
            return rvv_trap_illegal_state(rv, 0);
        if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs2,
                                          narrow_span_cx))
            return rvv_trap_illegal_state(rv, 0);
    }
    rvv_exec_wide_narrow_vx(rv, ir, ir->vd, rvv_wop_add);
})

RVOP(vwsubu_vv, {
    uint32_t wide_span;
    uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_wide_group_span(rv, &wide_span) ||
        !rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span, ir->vs1,
                                      narrow_span) ||
        rvv_cross_eew_overlap_illegal(ir->vd, wide_span, ir->vs2, narrow_span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_wide_narrow_vv(rv, ir, ir->vd, rvv_wop_subu);
})

RVOP(vwsubu_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    {
        uint32_t wide_span_cx;
        uint32_t narrow_span_cx = rvv_group_regs(rv->csr_vtype);
        if (!rvv_wide_group_span(rv, &wide_span_cx))
            return rvv_trap_illegal_state(rv, 0);
        if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs2,
                                          narrow_span_cx))
            return rvv_trap_illegal_state(rv, 0);
    }
    rvv_exec_wide_narrow_vx(rv, ir, ir->vd, rvv_wop_subu);
})

RVOP(vwsub_vv, {
    uint32_t wide_span;
    uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_wide_group_span(rv, &wide_span) ||
        !rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span, ir->vs1,
                                      narrow_span) ||
        rvv_cross_eew_overlap_illegal(ir->vd, wide_span, ir->vs2, narrow_span))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_wide_narrow_vv(rv, ir, ir->vd, rvv_wop_sub);
})

RVOP(vwsub_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    {
        uint32_t wide_span_cx;
        uint32_t narrow_span_cx = rvv_group_regs(rv->csr_vtype);
        if (!rvv_wide_group_span(rv, &wide_span_cx))
            return rvv_trap_illegal_state(rv, 0);
        if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs2,
                                          narrow_span_cx))
            return rvv_trap_illegal_state(rv, 0);
    }
    rvv_exec_wide_narrow_vx(rv, ir, ir->vd, rvv_wop_sub);
})

RVOP(vwaddu_wv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_wide_reg(rv, ir->vs2) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1))
        return rvv_trap_illegal_state(rv, 0);
    {
        uint32_t wide_span_cx;
        uint32_t narrow_span_cx = rvv_group_regs(rv->csr_vtype);
        if (!rvv_wide_group_span(rv, &wide_span_cx))
            return rvv_trap_illegal_state(rv, 0);
        /* vs2 has the same EEW as vd (both 2*SEW); only vs1 (SEW) is a
         * cross-EEW source for the lower-half overlap rule. */
        if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs1,
                                          narrow_span_cx))
            return rvv_trap_illegal_state(rv, 0);
    }
    rvv_exec_wide_mixed_vv(rv, ir, ir->vd, rvv_wop_addu_mixed);
})

RVOP(vwaddu_wx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_wide_reg(rv, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_wide_mixed_vx(rv, ir, ir->vd, rvv_wop_addu_mixed);
})

RVOP(vwadd_wv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_wide_reg(rv, ir->vs2) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1))
        return rvv_trap_illegal_state(rv, 0);
    {
        uint32_t wide_span_cx;
        uint32_t narrow_span_cx = rvv_group_regs(rv->csr_vtype);
        if (!rvv_wide_group_span(rv, &wide_span_cx))
            return rvv_trap_illegal_state(rv, 0);
        /* vs2 has the same EEW as vd (both 2*SEW); only vs1 (SEW) is a
         * cross-EEW source for the lower-half overlap rule. */
        if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs1,
                                          narrow_span_cx))
            return rvv_trap_illegal_state(rv, 0);
    }
    rvv_exec_wide_mixed_vv(rv, ir, ir->vd, rvv_wop_add_mixed);
})

RVOP(vwadd_wx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_wide_reg(rv, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_wide_mixed_vx(rv, ir, ir->vd, rvv_wop_add_mixed);
})

RVOP(vwsubu_wv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_wide_reg(rv, ir->vs2) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1))
        return rvv_trap_illegal_state(rv, 0);
    {
        uint32_t wide_span_cx;
        uint32_t narrow_span_cx = rvv_group_regs(rv->csr_vtype);
        if (!rvv_wide_group_span(rv, &wide_span_cx))
            return rvv_trap_illegal_state(rv, 0);
        /* vs2 has the same EEW as vd (both 2*SEW); only vs1 (SEW) is a
         * cross-EEW source for the lower-half overlap rule. */
        if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs1,
                                          narrow_span_cx))
            return rvv_trap_illegal_state(rv, 0);
    }
    rvv_exec_wide_mixed_vv(rv, ir, ir->vd, rvv_wop_subu_mixed);
})

RVOP(vwsubu_wx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_wide_reg(rv, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_wide_mixed_vx(rv, ir, ir->vd, rvv_wop_subu_mixed);
})

RVOP(vwsub_wv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_wide_reg(rv, ir->vs2) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1))
        return rvv_trap_illegal_state(rv, 0);
    {
        uint32_t wide_span_cx;
        uint32_t narrow_span_cx = rvv_group_regs(rv->csr_vtype);
        if (!rvv_wide_group_span(rv, &wide_span_cx))
            return rvv_trap_illegal_state(rv, 0);
        /* vs2 has the same EEW as vd (both 2*SEW); only vs1 (SEW) is a
         * cross-EEW source for the lower-half overlap rule. */
        if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs1,
                                          narrow_span_cx))
            return rvv_trap_illegal_state(rv, 0);
    }
    rvv_exec_wide_mixed_vv(rv, ir, ir->vd, rvv_wop_sub_mixed);
})

RVOP(vwsub_wx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_wide_reg(rv, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_wide_mixed_vx(rv, ir, ir->vd, rvv_wop_sub_mixed);
})

RVOP(vwmulu_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    {
        uint32_t wide_span_cx;
        uint32_t narrow_span_cx = rvv_group_regs(rv->csr_vtype);
        if (!rvv_wide_group_span(rv, &wide_span_cx))
            return rvv_trap_illegal_state(rv, 0);
        if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs1,
                                          narrow_span_cx) ||
            rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs2,
                                          narrow_span_cx))
            return rvv_trap_illegal_state(rv, 0);
    }
    rvv_exec_wide_narrow_vv(rv, ir, ir->vd, rvv_wop_mulu);
})

RVOP(vwmulu_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    {
        uint32_t wide_span_cx;
        uint32_t narrow_span_cx = rvv_group_regs(rv->csr_vtype);
        if (!rvv_wide_group_span(rv, &wide_span_cx))
            return rvv_trap_illegal_state(rv, 0);
        if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs2,
                                          narrow_span_cx))
            return rvv_trap_illegal_state(rv, 0);
    }
    rvv_exec_wide_narrow_vx(rv, ir, ir->vd, rvv_wop_mulu);
})

RVOP(vwmulsu_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    {
        uint32_t wide_span_cx;
        uint32_t narrow_span_cx = rvv_group_regs(rv->csr_vtype);
        if (!rvv_wide_group_span(rv, &wide_span_cx))
            return rvv_trap_illegal_state(rv, 0);
        if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs1,
                                          narrow_span_cx) ||
            rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs2,
                                          narrow_span_cx))
            return rvv_trap_illegal_state(rv, 0);
    }
    rvv_exec_wide_narrow_vv(rv, ir, ir->vd, rvv_wop_mulsu);
})

RVOP(vwmulsu_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    {
        uint32_t wide_span_cx;
        uint32_t narrow_span_cx = rvv_group_regs(rv->csr_vtype);
        if (!rvv_wide_group_span(rv, &wide_span_cx))
            return rvv_trap_illegal_state(rv, 0);
        if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs2,
                                          narrow_span_cx))
            return rvv_trap_illegal_state(rv, 0);
    }
    rvv_exec_wide_narrow_vx(rv, ir, ir->vd, rvv_wop_mulsu);
})

RVOP(vwmul_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    {
        uint32_t wide_span_cx;
        uint32_t narrow_span_cx = rvv_group_regs(rv->csr_vtype);
        if (!rvv_wide_group_span(rv, &wide_span_cx))
            return rvv_trap_illegal_state(rv, 0);
        if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs1,
                                          narrow_span_cx) ||
            rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs2,
                                          narrow_span_cx))
            return rvv_trap_illegal_state(rv, 0);
    }
    rvv_exec_wide_narrow_vv(rv, ir, ir->vd, rvv_wop_mul);
})

RVOP(vwmul_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    {
        uint32_t wide_span_cx;
        uint32_t narrow_span_cx = rvv_group_regs(rv->csr_vtype);
        if (!rvv_wide_group_span(rv, &wide_span_cx))
            return rvv_trap_illegal_state(rv, 0);
        if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs2,
                                          narrow_span_cx))
            return rvv_trap_illegal_state(rv, 0);
    }
    rvv_exec_wide_narrow_vx(rv, ir, ir->vd, rvv_wop_mul);
})

RVOP(vwmaccu_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    {
        uint32_t wide_span_cx;
        uint32_t narrow_span_cx = rvv_group_regs(rv->csr_vtype);
        if (!rvv_wide_group_span(rv, &wide_span_cx))
            return rvv_trap_illegal_state(rv, 0);
        if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs1,
                                          narrow_span_cx) ||
            rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs2,
                                          narrow_span_cx))
            return rvv_trap_illegal_state(rv, 0);
    }
    rvv_exec_wide_mac_vv(rv, ir, ir->vd, rvv_wop_mulu);
})

RVOP(vwmaccu_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    {
        uint32_t wide_span_cx;
        uint32_t narrow_span_cx = rvv_group_regs(rv->csr_vtype);
        if (!rvv_wide_group_span(rv, &wide_span_cx))
            return rvv_trap_illegal_state(rv, 0);
        if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs2,
                                          narrow_span_cx))
            return rvv_trap_illegal_state(rv, 0);
    }
    rvv_exec_wide_mac_vx(rv, ir, ir->vd, rvv_wop_mulu);
})

RVOP(vwmacc_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    {
        uint32_t wide_span_cx;
        uint32_t narrow_span_cx = rvv_group_regs(rv->csr_vtype);
        if (!rvv_wide_group_span(rv, &wide_span_cx))
            return rvv_trap_illegal_state(rv, 0);
        if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs1,
                                          narrow_span_cx) ||
            rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs2,
                                          narrow_span_cx))
            return rvv_trap_illegal_state(rv, 0);
    }
    rvv_exec_wide_mac_vv(rv, ir, ir->vd, rvv_wop_mul);
})

RVOP(vwmacc_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    {
        uint32_t wide_span_cx;
        uint32_t narrow_span_cx = rvv_group_regs(rv->csr_vtype);
        if (!rvv_wide_group_span(rv, &wide_span_cx))
            return rvv_trap_illegal_state(rv, 0);
        if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs2,
                                          narrow_span_cx))
            return rvv_trap_illegal_state(rv, 0);
    }
    rvv_exec_wide_mac_vx(rv, ir, ir->vd, rvv_wop_mul);
})

RVOP(vwmaccus_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    {
        uint32_t wide_span_cx;
        uint32_t narrow_span_cx = rvv_group_regs(rv->csr_vtype);
        if (!rvv_wide_group_span(rv, &wide_span_cx))
            return rvv_trap_illegal_state(rv, 0);
        if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs2,
                                          narrow_span_cx))
            return rvv_trap_illegal_state(rv, 0);
    }
    rvv_exec_wide_mac_vx(rv, ir, ir->vd, rvv_wop_mulus);
})

RVOP(vwmaccsu_vv, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    {
        uint32_t wide_span_cx;
        uint32_t narrow_span_cx = rvv_group_regs(rv->csr_vtype);
        if (!rvv_wide_group_span(rv, &wide_span_cx))
            return rvv_trap_illegal_state(rv, 0);
        if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs1,
                                          narrow_span_cx) ||
            rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs2,
                                          narrow_span_cx))
            return rvv_trap_illegal_state(rv, 0);
    }
    rvv_exec_wide_mac_vv(rv, ir, ir->vd, rvv_wop_mulsu);
})

RVOP(vwmaccsu_vx, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_wide_reg(rv, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    {
        uint32_t wide_span_cx;
        uint32_t narrow_span_cx = rvv_group_regs(rv->csr_vtype);
        if (!rvv_wide_group_span(rv, &wide_span_cx))
            return rvv_trap_illegal_state(rv, 0);
        if (rvv_cross_eew_overlap_illegal(ir->vd, wide_span_cx, ir->vs2,
                                          narrow_span_cx))
            return rvv_trap_illegal_state(rv, 0);
    }
    rvv_exec_wide_mac_vx(rv, ir, ir->vd, rvv_wop_mulsu);
})

RVOP(vmv_s_x, {
    if (rvv_require_operable(rv))
        return false;
    if (!ir->vm)
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_vmv_s_x(rv, ir);
})

RVOP(vmv_x_s, {
    if (rvv_require_operable(rv))
        return false;
    if (!ir->vm)
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_vmv_x_s(rv, ir);
})

RVOP(vcpop_m, {
    uint32_t count = 0;
    if (rvv_require_operable(rv))
        return false;
    for (uint32_t elem = 0; elem < rv->csr_vl; elem++) {
        if (rvv_mask_enabled_for_elem(rv, ir, elem) &&
            rvv_mask_get_bit(rv, ir->vs2, elem))
            count++;
    }
    rv->X[ir->rd] = count;
    rv->csr_vstart = 0;
})

RVOP(vfirst_m, {
    int32_t first = -1;
    if (rvv_require_operable(rv))
        return false;
    for (uint32_t elem = 0; elem < rv->csr_vl; elem++) {
        if (rvv_mask_enabled_for_elem(rv, ir, elem) &&
            rvv_mask_get_bit(rv, ir->vs2, elem)) {
            first = (int32_t) elem;
            break;
        }
    }
    rv->X[ir->rd] = (uint32_t) first;
    rv->csr_vstart = 0;
})

RVOP(vmsbf_m, {
    if (rvv_require_operable(rv))
        return false;
    rvv_exec_mask_prefix(rv, ir, ir->rd, 0);
})

RVOP(vmsof_m, {
    if (rvv_require_operable(rv))
        return false;
    rvv_exec_mask_prefix(rv, ir, ir->rd, 1);
})

RVOP(vmsif_m, {
    if (rvv_require_operable(rv))
        return false;
    rvv_exec_mask_prefix(rv, ir, ir->rd, 2);
})

RVOP(viota_m, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->rd))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_viota(rv, ir, ir->rd);
})

RVOP(vid_v, {
    if (rvv_require_operable(rv))
        return false;
    if (!rvv_validate_data_reg(rv->csr_vtype, ir->vd))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_vid(rv, ir, ir->vd);
})

#if RV32_HAS(EXT_F)
typedef uint32_t (*rvv_fp32_binop_fn)(uint32_t lhs, uint32_t rhs);
typedef uint32_t (*rvv_fp32_triop_fn)(uint32_t dest,
                                      uint32_t lhs,
                                      uint32_t rhs);
typedef bool (*rvv_fp32_cmp_fn)(uint32_t lhs, uint32_t rhs);
typedef uint64_t (*rvv_fp64_narrow_binop_fn)(uint32_t lhs, uint32_t rhs);
typedef uint64_t (*rvv_fp64_mixed_binop_fn)(uint64_t lhs, uint32_t rhs);
typedef uint64_t (*rvv_fp64_triop_fn)(uint64_t dest,
                                      uint32_t lhs,
                                      uint32_t rhs);

static inline riscv_float_t rvv_fp32_from_raw(uint32_t bits)
{
    riscv_float_t value;
    value.v = bits;
    return value;
}

static inline softfloat_float64_t rvv_fp64_from_raw(uint64_t bits)
{
    softfloat_float64_t value;
    value.v = bits;
    return value;
}

static inline uint32_t rvv_fp32_neg(uint32_t bits)
{
    return bits ^ FMASK_SIGN;
}

static inline uint64_t rvv_fp64_neg(uint64_t bits)
{
    return bits ^ UINT64_C(0x8000000000000000);
}

static inline void rvv_fp_begin_round(riscv_t *rv)
{
    softfloat_exceptionFlags = 0;
    set_rounding_mode(rv, 0x7);
}

static inline void rvv_fp_begin_flags(void)
{
    softfloat_exceptionFlags = 0;
}

static inline uint32_t rvv_fp_add32(uint32_t lhs, uint32_t rhs)
{
    return f32_add(rvv_fp32_from_raw(lhs), rvv_fp32_from_raw(rhs)).v;
}

static inline uint32_t rvv_fp_sub32(uint32_t lhs, uint32_t rhs)
{
    return f32_sub(rvv_fp32_from_raw(lhs), rvv_fp32_from_raw(rhs)).v;
}

static inline uint32_t rvv_fp_mul32(uint32_t lhs, uint32_t rhs)
{
    return f32_mul(rvv_fp32_from_raw(lhs), rvv_fp32_from_raw(rhs)).v;
}

static inline uint32_t rvv_fp_div32(uint32_t lhs, uint32_t rhs)
{
    return f32_div(rvv_fp32_from_raw(lhs), rvv_fp32_from_raw(rhs)).v;
}

static inline uint32_t rvv_fp_min32(uint32_t lhs, uint32_t rhs)
{
    riscv_float_t a = rvv_fp32_from_raw(lhs);
    riscv_float_t b = rvv_fp32_from_raw(rhs);
    bool less;

    if (f32_isSignalingNaN(a) || f32_isSignalingNaN(b))
        softfloat_exceptionFlags |= softfloat_flag_invalid;
    less = f32_lt_quiet(a, b) || (f32_eq(a, b) && (lhs & FMASK_SIGN));
    if (is_nan(lhs) && is_nan(rhs))
        return RV_NAN;
    return (less || is_nan(rhs)) ? lhs : rhs;
}

static inline uint32_t rvv_fp_max32(uint32_t lhs, uint32_t rhs)
{
    riscv_float_t a = rvv_fp32_from_raw(lhs);
    riscv_float_t b = rvv_fp32_from_raw(rhs);
    bool greater;

    if (f32_isSignalingNaN(a) || f32_isSignalingNaN(b))
        softfloat_exceptionFlags |= softfloat_flag_invalid;
    greater = f32_lt_quiet(b, a) || (f32_eq(a, b) && (rhs & FMASK_SIGN));
    if (is_nan(lhs) && is_nan(rhs))
        return RV_NAN;
    return (greater || is_nan(rhs)) ? lhs : rhs;
}

static inline uint32_t rvv_fp_sgnj32(uint32_t lhs, uint32_t rhs)
{
    return (lhs & ~FMASK_SIGN) | (rhs & FMASK_SIGN);
}

static inline uint32_t rvv_fp_sgnjn32(uint32_t lhs, uint32_t rhs)
{
    return (lhs & ~FMASK_SIGN) | ((~rhs) & FMASK_SIGN);
}

static inline uint32_t rvv_fp_sgnjx32(uint32_t lhs, uint32_t rhs)
{
    return lhs ^ (rhs & FMASK_SIGN);
}

static inline bool rvv_fp_eq32(uint32_t lhs, uint32_t rhs)
{
    return f32_eq(rvv_fp32_from_raw(lhs), rvv_fp32_from_raw(rhs));
}

static inline bool rvv_fp_le32(uint32_t lhs, uint32_t rhs)
{
    return f32_le(rvv_fp32_from_raw(lhs), rvv_fp32_from_raw(rhs));
}

static inline bool rvv_fp_lt32(uint32_t lhs, uint32_t rhs)
{
    return f32_lt(rvv_fp32_from_raw(lhs), rvv_fp32_from_raw(rhs));
}

static inline bool rvv_fp_ne32(uint32_t lhs, uint32_t rhs)
{
    return !f32_eq(rvv_fp32_from_raw(lhs), rvv_fp32_from_raw(rhs));
}

static inline uint32_t rvv_fp_fmacc32(uint32_t dest, uint32_t lhs, uint32_t rhs)
{
    return f32_mulAdd(rvv_fp32_from_raw(lhs), rvv_fp32_from_raw(rhs),
                      rvv_fp32_from_raw(dest))
        .v;
}

static inline uint32_t rvv_fp_fnmacc32(uint32_t dest,
                                       uint32_t lhs,
                                       uint32_t rhs)
{
    return f32_mulAdd(rvv_fp32_from_raw(rvv_fp32_neg(lhs)),
                      rvv_fp32_from_raw(rhs),
                      rvv_fp32_from_raw(rvv_fp32_neg(dest)))
        .v;
}

static inline uint32_t rvv_fp_fmsac32(uint32_t dest, uint32_t lhs, uint32_t rhs)
{
    return f32_mulAdd(rvv_fp32_from_raw(lhs), rvv_fp32_from_raw(rhs),
                      rvv_fp32_from_raw(rvv_fp32_neg(dest)))
        .v;
}

static inline uint32_t rvv_fp_fnmsac32(uint32_t dest,
                                       uint32_t lhs,
                                       uint32_t rhs)
{
    return f32_mulAdd(rvv_fp32_from_raw(rvv_fp32_neg(lhs)),
                      rvv_fp32_from_raw(rhs), rvv_fp32_from_raw(dest))
        .v;
}

static inline uint32_t rvv_fp_fmadd32(uint32_t dest, uint32_t lhs, uint32_t rhs)
{
    return f32_mulAdd(rvv_fp32_from_raw(rhs), rvv_fp32_from_raw(dest),
                      rvv_fp32_from_raw(lhs))
        .v;
}

static inline uint32_t rvv_fp_fmsub32(uint32_t dest, uint32_t lhs, uint32_t rhs)
{
    return f32_mulAdd(rvv_fp32_from_raw(rhs), rvv_fp32_from_raw(dest),
                      rvv_fp32_from_raw(rvv_fp32_neg(lhs)))
        .v;
}

static inline uint32_t rvv_fp_fnmadd32(uint32_t dest,
                                       uint32_t lhs,
                                       uint32_t rhs)
{
    return f32_mulAdd(rvv_fp32_from_raw(rvv_fp32_neg(rhs)),
                      rvv_fp32_from_raw(dest),
                      rvv_fp32_from_raw(rvv_fp32_neg(lhs)))
        .v;
}

static inline uint32_t rvv_fp_fnmsub32(uint32_t dest,
                                       uint32_t lhs,
                                       uint32_t rhs)
{
    return f32_mulAdd(rvv_fp32_from_raw(rvv_fp32_neg(rhs)),
                      rvv_fp32_from_raw(dest), rvv_fp32_from_raw(lhs))
        .v;
}

static inline uint64_t rvv_fp_widen32(uint32_t bits)
{
    return f32_to_f64(rvv_fp32_from_raw(bits)).v;
}

static inline uint64_t rvv_fp_add64(uint64_t lhs, uint64_t rhs)
{
    return f64_add(rvv_fp64_from_raw(lhs), rvv_fp64_from_raw(rhs)).v;
}

static inline uint64_t rvv_fp_sub64(uint64_t lhs, uint64_t rhs)
{
    return f64_sub(rvv_fp64_from_raw(lhs), rvv_fp64_from_raw(rhs)).v;
}

static inline uint64_t rvv_fp_mul64(uint64_t lhs, uint64_t rhs)
{
    return f64_mul(rvv_fp64_from_raw(lhs), rvv_fp64_from_raw(rhs)).v;
}

static inline uint64_t rvv_fp_wadd64(uint32_t lhs, uint32_t rhs)
{
    return rvv_fp_add64(rvv_fp_widen32(lhs), rvv_fp_widen32(rhs));
}

static inline uint64_t rvv_fp_wsub64(uint32_t lhs, uint32_t rhs)
{
    return rvv_fp_sub64(rvv_fp_widen32(lhs), rvv_fp_widen32(rhs));
}

static inline uint64_t rvv_fp_wmul64(uint32_t lhs, uint32_t rhs)
{
    return rvv_fp_mul64(rvv_fp_widen32(lhs), rvv_fp_widen32(rhs));
}

static inline uint64_t rvv_fp_wadd_mixed64(uint64_t lhs, uint32_t rhs)
{
    return rvv_fp_add64(lhs, rvv_fp_widen32(rhs));
}

static inline uint64_t rvv_fp_wsub_mixed64(uint64_t lhs, uint32_t rhs)
{
    return rvv_fp_sub64(lhs, rvv_fp_widen32(rhs));
}

static inline uint64_t rvv_fp_wmacc64(uint64_t dest, uint32_t lhs, uint32_t rhs)
{
    return f64_mulAdd(rvv_fp64_from_raw(rvv_fp_widen32(lhs)),
                      rvv_fp64_from_raw(rvv_fp_widen32(rhs)),
                      rvv_fp64_from_raw(dest))
        .v;
}

static inline uint64_t rvv_fp_wnmacc64(uint64_t dest,
                                       uint32_t lhs,
                                       uint32_t rhs)
{
    return f64_mulAdd(rvv_fp64_from_raw(rvv_fp64_neg(rvv_fp_widen32(lhs))),
                      rvv_fp64_from_raw(rvv_fp_widen32(rhs)),
                      rvv_fp64_from_raw(rvv_fp64_neg(dest)))
        .v;
}

static inline uint64_t rvv_fp_wmsac64(uint64_t dest, uint32_t lhs, uint32_t rhs)
{
    return f64_mulAdd(rvv_fp64_from_raw(rvv_fp_widen32(lhs)),
                      rvv_fp64_from_raw(rvv_fp_widen32(rhs)),
                      rvv_fp64_from_raw(rvv_fp64_neg(dest)))
        .v;
}

static inline uint64_t rvv_fp_wnmsac64(uint64_t dest,
                                       uint32_t lhs,
                                       uint32_t rhs)
{
    return f64_mulAdd(rvv_fp64_from_raw(rvv_fp64_neg(rvv_fp_widen32(lhs))),
                      rvv_fp64_from_raw(rvv_fp_widen32(rhs)),
                      rvv_fp64_from_raw(dest))
        .v;
}

static inline void rvv_exec_fp32_vv(riscv_t *rv,
                                    const rv_insn_t *ir,
                                    uint32_t dest,
                                    rvv_fp32_binop_fn op)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, 32, 0xFFFFFFFFU);
            continue;
        }
        rvv_set_elem(rv, dest, elem, 32,
                     op(rvv_get_elem(rv, ir->vs2, elem, 32),
                        rvv_get_elem(rv, ir->vs1, elem, 32)));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, 32, 0xFFFFFFFFU);
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_fp32_vf(riscv_t *rv,
                                    const rv_insn_t *ir,
                                    uint32_t dest,
                                    rvv_fp32_binop_fn op)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t scalar = rv->F[ir->rs1].v;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, 32, 0xFFFFFFFFU);
            continue;
        }
        rvv_set_elem(rv, dest, elem, 32,
                     op(rvv_get_elem(rv, ir->vs2, elem, 32), scalar));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, 32, 0xFFFFFFFFU);
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_fp32_mask_vv(riscv_t *rv,
                                         const rv_insn_t *ir,
                                         uint32_t dest,
                                         rvv_fp32_cmp_fn cmp)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!ir->vm && !rvv_mask_get_bit(rv, 0, elem))
            continue;
        rvv_mask_set_bit(rv, dest, elem,
                         cmp(rvv_get_elem(rv, ir->vs2, elem, 32),
                             rvv_get_elem(rv, ir->vs1, elem, 32)));
    }
    rvv_mask_fill_tail(rv, dest, rv->csr_vl, vlmax);
    rv->csr_vstart = 0;
}

static inline void rvv_exec_fp32_mask_vf(riscv_t *rv,
                                         const rv_insn_t *ir,
                                         uint32_t dest,
                                         rvv_fp32_cmp_fn cmp,
                                         bool reverse)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t scalar = rv->F[ir->rs1].v;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        uint32_t value;

        if (!ir->vm && !rvv_mask_get_bit(rv, 0, elem))
            continue;
        value = rvv_get_elem(rv, ir->vs2, elem, 32);
        rvv_mask_set_bit(rv, dest, elem,
                         reverse ? cmp(scalar, value) : cmp(value, scalar));
    }
    rvv_mask_fill_tail(rv, dest, rv->csr_vl, vlmax);
    rv->csr_vstart = 0;
}

static inline void rvv_exec_fp32_reduction(riscv_t *rv,
                                           const rv_insn_t *ir,
                                           uint32_t dest,
                                           rvv_fp32_binop_fn op)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint32_t acc = rvv_get_elem(rv, ir->vs1, 0, 32);

    for (uint32_t elem = 0; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem))
            continue;
        acc = op(acc, rvv_get_elem(rv, ir->vs2, elem, 32));
    }
    rvv_set_elem(rv, dest, 0, 32, acc);
    if ((rv->csr_vtype >> 6) & 0x1) {
        for (uint32_t elem = 1; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, 32, 0xFFFFFFFFU);
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_fp32_mac_vv(riscv_t *rv,
                                        const rv_insn_t *ir,
                                        uint32_t dest,
                                        rvv_fp32_triop_fn op)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, 32, 0xFFFFFFFFU);
            continue;
        }
        rvv_set_elem(rv, dest, elem, 32,
                     op(rvv_get_elem(rv, dest, elem, 32),
                        rvv_get_elem(rv, ir->vs2, elem, 32),
                        rvv_get_elem(rv, ir->vs1, elem, 32)));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, 32, 0xFFFFFFFFU);
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_fp32_mac_vf(riscv_t *rv,
                                        const rv_insn_t *ir,
                                        uint32_t dest,
                                        rvv_fp32_triop_fn op)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t scalar = rv->F[ir->rs1].v;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, 32, 0xFFFFFFFFU);
            continue;
        }
        rvv_set_elem(rv, dest, elem, 32,
                     op(rvv_get_elem(rv, dest, elem, 32),
                        rvv_get_elem(rv, ir->vs2, elem, 32), scalar));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, 32, 0xFFFFFFFFU);
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_fp64_widen_vv(riscv_t *rv,
                                          const rv_insn_t *ir,
                                          uint32_t dest,
                                          rvv_fp64_narrow_binop_fn op)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem_ext(rv, dest, elem, 64, UINT64_MAX);
            continue;
        }
        rvv_set_elem_ext(rv, dest, elem, 64,
                         op(rvv_get_elem(rv, ir->vs2, elem, 32),
                            rvv_get_elem(rv, ir->vs1, elem, 32)));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem_ext(rv, dest, elem, 64, UINT64_MAX);
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_fp64_widen_vf(riscv_t *rv,
                                          const rv_insn_t *ir,
                                          uint32_t dest,
                                          rvv_fp64_narrow_binop_fn op)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t scalar = rv->F[ir->rs1].v;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem_ext(rv, dest, elem, 64, UINT64_MAX);
            continue;
        }
        rvv_set_elem_ext(rv, dest, elem, 64,
                         op(rvv_get_elem(rv, ir->vs2, elem, 32), scalar));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem_ext(rv, dest, elem, 64, UINT64_MAX);
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_fp64_mixed_vv(riscv_t *rv,
                                          const rv_insn_t *ir,
                                          uint32_t dest,
                                          rvv_fp64_mixed_binop_fn op)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem_ext(rv, dest, elem, 64, UINT64_MAX);
            continue;
        }
        rvv_set_elem_ext(rv, dest, elem, 64,
                         op(rvv_get_elem_ext(rv, ir->vs2, elem, 64),
                            rvv_get_elem(rv, ir->vs1, elem, 32)));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem_ext(rv, dest, elem, 64, UINT64_MAX);
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_fp64_mixed_vf(riscv_t *rv,
                                          const rv_insn_t *ir,
                                          uint32_t dest,
                                          rvv_fp64_mixed_binop_fn op)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t scalar = rv->F[ir->rs1].v;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem_ext(rv, dest, elem, 64, UINT64_MAX);
            continue;
        }
        rvv_set_elem_ext(rv, dest, elem, 64,
                         op(rvv_get_elem_ext(rv, ir->vs2, elem, 64), scalar));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem_ext(rv, dest, elem, 64, UINT64_MAX);
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_fp64_reduction(riscv_t *rv,
                                           const rv_insn_t *ir,
                                           uint32_t dest,
                                           rvv_fp64_mixed_binop_fn op)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint64_t acc = rvv_get_elem_ext(rv, ir->vs1, 0, 64);

    for (uint32_t elem = 0; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem))
            continue;
        acc = op(acc, rvv_get_elem(rv, ir->vs2, elem, 32));
    }
    rvv_set_elem_ext(rv, dest, 0, 64, acc);
    if ((rv->csr_vtype >> 6) & 0x1) {
        for (uint32_t elem = 1; elem < vlmax; elem++)
            rvv_set_elem_ext(rv, dest, elem, 64, UINT64_MAX);
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_fp64_mac_vv(riscv_t *rv,
                                        const rv_insn_t *ir,
                                        uint32_t dest,
                                        rvv_fp64_triop_fn op)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem_ext(rv, dest, elem, 64, UINT64_MAX);
            continue;
        }
        rvv_set_elem_ext(rv, dest, elem, 64,
                         op(rvv_get_elem_ext(rv, dest, elem, 64),
                            rvv_get_elem(rv, ir->vs2, elem, 32),
                            rvv_get_elem(rv, ir->vs1, elem, 32)));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem_ext(rv, dest, elem, 64, UINT64_MAX);
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_fp64_mac_vf(riscv_t *rv,
                                        const rv_insn_t *ir,
                                        uint32_t dest,
                                        rvv_fp64_triop_fn op)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t scalar = rv->F[ir->rs1].v;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem_ext(rv, dest, elem, 64, UINT64_MAX);
            continue;
        }
        rvv_set_elem_ext(rv, dest, elem, 64,
                         op(rvv_get_elem_ext(rv, dest, elem, 64),
                            rvv_get_elem(rv, ir->vs2, elem, 32), scalar));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem_ext(rv, dest, elem, 64, UINT64_MAX);
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_vfslide1up(riscv_t *rv,
                                       const rv_insn_t *ir,
                                       uint32_t dest)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t scalar = rv->F[ir->rs1].v;

    if (rv->csr_vstart < rv->csr_vl) {
        if (rv->csr_vstart == 0) {
            if (rvv_mask_enabled_for_elem(rv, ir, 0))
                rvv_set_elem(rv, dest, 0, 32, scalar);
            else if (vma)
                rvv_set_elem(rv, dest, 0, 32, 0xFFFFFFFFU);
        }
        for (uint32_t elem = rv->csr_vstart > 1 ? rv->csr_vstart : 1;
             elem < rv->csr_vl; elem++) {
            if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
                if (vma)
                    rvv_set_elem(rv, dest, elem, 32, 0xFFFFFFFFU);
                continue;
            }
            rvv_set_elem(rv, dest, elem, 32,
                         rvv_get_elem(rv, ir->vs2, elem - 1, 32));
        }
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, 32, 0xFFFFFFFFU);
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_vfslide1down(riscv_t *rv,
                                         const rv_insn_t *ir,
                                         uint32_t dest)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t scalar = rv->F[ir->rs1].v;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, dest, elem, 32, 0xFFFFFFFFU);
            continue;
        }
        if ((elem + 1) < rv->csr_vl)
            rvv_set_elem(rv, dest, elem, 32,
                         rvv_get_elem(rv, ir->vs2, elem + 1, 32));
        else
            rvv_set_elem(rv, dest, elem, 32, scalar);
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, 32, 0xFFFFFFFFU);
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_vfmerge_vfm(riscv_t *rv,
                                        const rv_insn_t *ir,
                                        uint32_t dest)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t scalar = rv->F[ir->rs1].v;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        rvv_set_elem(rv, dest, elem, 32,
                     rvv_mask_get_bit(rv, 0, elem)
                         ? scalar
                         : rvv_get_elem(rv, ir->vs2, elem, 32));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, 32, 0xFFFFFFFFU);
    }
    rv->csr_vstart = 0;
}

static inline void rvv_exec_vfmv_v_f(riscv_t *rv,
                                     const rv_insn_t *ir,
                                     uint32_t dest)
{
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    uint32_t scalar = rv->F[ir->rs1].v;

    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++)
        rvv_set_elem(rv, dest, elem, 32, scalar);
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, dest, elem, 32, 0xFFFFFFFFU);
    }
    rv->csr_vstart = 0;
}

#define RVV_FP32_VV_OP(name, opfn, needs_round)               \
    RVOP(name, {                                              \
        if (rvv_require_operable(rv))                         \
            return false;                                     \
        if ((rvv_sew_bits(rv->csr_vtype) != 32) ||            \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||  \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) || \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))   \
            return rvv_trap_illegal_state(rv, 0);             \
        if (needs_round)                                      \
            rvv_fp_begin_round(rv);                           \
        else                                                  \
            rvv_fp_begin_flags();                             \
        rvv_exec_fp32_vv(rv, ir, ir->vd, opfn);               \
        set_fflag(rv);                                        \
    })

#define RVV_FP32_VF_OP(name, opfn, needs_round)              \
    RVOP(name, {                                             \
        if (rvv_require_operable(rv))                        \
            return false;                                    \
        if ((rvv_sew_bits(rv->csr_vtype) != 32) ||           \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vd) || \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))  \
            return rvv_trap_illegal_state(rv, 0);            \
        if (needs_round)                                     \
            rvv_fp_begin_round(rv);                          \
        else                                                 \
            rvv_fp_begin_flags();                            \
        rvv_exec_fp32_vf(rv, ir, ir->vd, opfn);              \
        set_fflag(rv);                                       \
    })

#define RVV_FP32_RED_OP(name, opfn, needs_round)              \
    RVOP(name, {                                              \
        if (rvv_require_operable(rv))                         \
            return false;                                     \
        if ((rvv_sew_bits(rv->csr_vtype) != 32) ||            \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||  \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) || \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))   \
            return rvv_trap_illegal_state(rv, 0);             \
        if (needs_round)                                      \
            rvv_fp_begin_round(rv);                           \
        else                                                  \
            rvv_fp_begin_flags();                             \
        rvv_exec_fp32_reduction(rv, ir, ir->vd, opfn);        \
        set_fflag(rv);                                        \
    })

#define RVV_FP32_CMP_VV_OP(name, cmpfn)                       \
    RVOP(name, {                                              \
        if (rvv_require_operable(rv))                         \
            return false;                                     \
        if ((rvv_sew_bits(rv->csr_vtype) != 32) ||            \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) || \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))   \
            return rvv_trap_illegal_state(rv, 0);             \
        rvv_fp_begin_flags();                                 \
        rvv_exec_fp32_mask_vv(rv, ir, ir->vd, cmpfn);         \
        set_fflag(rv);                                        \
    })

#define RVV_FP32_CMP_VF_OP(name, cmpfn, reverse_cmp)               \
    RVOP(name, {                                                   \
        if (rvv_require_operable(rv))                              \
            return false;                                          \
        if ((rvv_sew_bits(rv->csr_vtype) != 32) ||                 \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))        \
            return rvv_trap_illegal_state(rv, 0);                  \
        rvv_fp_begin_flags();                                      \
        rvv_exec_fp32_mask_vf(rv, ir, ir->vd, cmpfn, reverse_cmp); \
        set_fflag(rv);                                             \
    })

#define RVV_FP32_MAC_VV_OP(name, opfn)                        \
    RVOP(name, {                                              \
        if (rvv_require_operable(rv))                         \
            return false;                                     \
        if ((rvv_sew_bits(rv->csr_vtype) != 32) ||            \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||  \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) || \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))   \
            return rvv_trap_illegal_state(rv, 0);             \
        rvv_fp_begin_round(rv);                               \
        rvv_exec_fp32_mac_vv(rv, ir, ir->vd, opfn);           \
        set_fflag(rv);                                        \
    })

#define RVV_FP32_MAC_VF_OP(name, opfn)                       \
    RVOP(name, {                                             \
        if (rvv_require_operable(rv))                        \
            return false;                                    \
        if ((rvv_sew_bits(rv->csr_vtype) != 32) ||           \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vd) || \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))  \
            return rvv_trap_illegal_state(rv, 0);            \
        rvv_fp_begin_round(rv);                              \
        rvv_exec_fp32_mac_vf(rv, ir, ir->vd, opfn);          \
        set_fflag(rv);                                       \
    })

#define RVV_FP64_WIDEN_VV_OP(name, opfn)                              \
    RVOP(name, {                                                      \
        uint32_t wide_span;                                           \
        uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);         \
        if (rvv_require_operable(rv))                                 \
            return false;                                             \
        if ((rvv_sew_bits(rv->csr_vtype) != 32) ||                    \
            !rvv_validate_wide_reg(rv, ir->vd) ||                     \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||         \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vs2) ||         \
            !rvv_wide_group_span(rv, &wide_span) ||                   \
            rvv_cross_eew_overlap_illegal(ir->vd, wide_span, ir->vs1, \
                                          narrow_span) ||             \
            rvv_cross_eew_overlap_illegal(ir->vd, wide_span, ir->vs2, \
                                          narrow_span))               \
            return rvv_trap_illegal_state(rv, 0);                     \
        rvv_fp_begin_round(rv);                                       \
        rvv_exec_fp64_widen_vv(rv, ir, ir->vd, opfn);                 \
        set_fflag(rv);                                                \
    })

#define RVV_FP64_WIDEN_VF_OP(name, opfn)                              \
    RVOP(name, {                                                      \
        uint32_t wide_span;                                           \
        uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);         \
        if (rvv_require_operable(rv))                                 \
            return false;                                             \
        if ((rvv_sew_bits(rv->csr_vtype) != 32) ||                    \
            !rvv_validate_wide_reg(rv, ir->vd) ||                     \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vs2) ||         \
            !rvv_wide_group_span(rv, &wide_span) ||                   \
            rvv_cross_eew_overlap_illegal(ir->vd, wide_span, ir->vs2, \
                                          narrow_span))               \
            return rvv_trap_illegal_state(rv, 0);                     \
        rvv_fp_begin_round(rv);                                       \
        rvv_exec_fp64_widen_vf(rv, ir, ir->vd, opfn);                 \
        set_fflag(rv);                                                \
    })

#define RVV_FP64_RED_OP(name, opfn)                         \
    RVOP(name, {                                            \
        if (rvv_require_operable(rv))                       \
            return false;                                   \
        if ((rvv_sew_bits(rv->csr_vtype) != 32) ||          \
            !rvv_validate_wide_reg(rv, ir->vd) ||           \
            !rvv_validate_wide_reg(rv, ir->vs1) ||          \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vs2)) \
            return rvv_trap_illegal_state(rv, 0);           \
        rvv_fp_begin_round(rv);                             \
        rvv_exec_fp64_reduction(rv, ir, ir->vd, opfn);      \
        set_fflag(rv);                                      \
    })

#define RVV_FP64_MIXED_VV_OP(name, opfn)                              \
    RVOP(name, {                                                      \
        uint32_t wide_span;                                           \
        uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);         \
        if (rvv_require_operable(rv))                                 \
            return false;                                             \
        if ((rvv_sew_bits(rv->csr_vtype) != 32) ||                    \
            !rvv_validate_wide_reg(rv, ir->vd) ||                     \
            !rvv_validate_wide_reg(rv, ir->vs2) ||                    \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||         \
            !rvv_wide_group_span(rv, &wide_span) ||                   \
            rvv_cross_eew_overlap_illegal(ir->vd, wide_span, ir->vs1, \
                                          narrow_span))               \
            return rvv_trap_illegal_state(rv, 0);                     \
        rvv_fp_begin_round(rv);                                       \
        rvv_exec_fp64_mixed_vv(rv, ir, ir->vd, opfn);                 \
        set_fflag(rv);                                                \
    })

#define RVV_FP64_MIXED_VF_OP(name, opfn)              \
    RVOP(name, {                                      \
        if (rvv_require_operable(rv))                 \
            return false;                             \
        if ((rvv_sew_bits(rv->csr_vtype) != 32) ||    \
            !rvv_validate_wide_reg(rv, ir->vd) ||     \
            !rvv_validate_wide_reg(rv, ir->vs2))      \
            return rvv_trap_illegal_state(rv, 0);     \
        rvv_fp_begin_round(rv);                       \
        rvv_exec_fp64_mixed_vf(rv, ir, ir->vd, opfn); \
        set_fflag(rv);                                \
    })

#define RVV_FP64_MAC_VV_OP(name, opfn)                                \
    RVOP(name, {                                                      \
        uint32_t wide_span;                                           \
        uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);         \
        if (rvv_require_operable(rv))                                 \
            return false;                                             \
        if ((rvv_sew_bits(rv->csr_vtype) != 32) ||                    \
            !rvv_validate_wide_reg(rv, ir->vd) ||                     \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vs1) ||         \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vs2) ||         \
            !rvv_wide_group_span(rv, &wide_span) ||                   \
            rvv_cross_eew_overlap_illegal(ir->vd, wide_span, ir->vs1, \
                                          narrow_span) ||             \
            rvv_cross_eew_overlap_illegal(ir->vd, wide_span, ir->vs2, \
                                          narrow_span))               \
            return rvv_trap_illegal_state(rv, 0);                     \
        rvv_fp_begin_round(rv);                                       \
        rvv_exec_fp64_mac_vv(rv, ir, ir->vd, opfn);                   \
        set_fflag(rv);                                                \
    })

#define RVV_FP64_MAC_VF_OP(name, opfn)                                \
    RVOP(name, {                                                      \
        uint32_t wide_span;                                           \
        uint32_t narrow_span = rvv_group_regs(rv->csr_vtype);         \
        if (rvv_require_operable(rv))                                 \
            return false;                                             \
        if ((rvv_sew_bits(rv->csr_vtype) != 32) ||                    \
            !rvv_validate_wide_reg(rv, ir->vd) ||                     \
            !rvv_validate_data_reg(rv->csr_vtype, ir->vs2) ||         \
            !rvv_wide_group_span(rv, &wide_span) ||                   \
            rvv_cross_eew_overlap_illegal(ir->vd, wide_span, ir->vs2, \
                                          narrow_span))               \
            return rvv_trap_illegal_state(rv, 0);                     \
        rvv_fp_begin_round(rv);                                       \
        rvv_exec_fp64_mac_vf(rv, ir, ir->vd, opfn);                   \
        set_fflag(rv);                                                \
    })

RVV_FP32_VV_OP(vfadd_vv, rvv_fp_add32, true);
RVV_FP32_VF_OP(vfadd_vf, rvv_fp_add32, true);
RVV_FP32_RED_OP(vfredusum_vs, rvv_fp_add32, true);
RVV_FP32_VV_OP(vfsub_vv, rvv_fp_sub32, true);
RVV_FP32_VF_OP(vfsub_vf, rvv_fp_sub32, true);
RVV_FP32_RED_OP(vfredosum_vs, rvv_fp_add32, true);
RVV_FP32_VV_OP(vfmin_vv, rvv_fp_min32, false);
RVV_FP32_VF_OP(vfmin_vf, rvv_fp_min32, false);
RVV_FP32_RED_OP(vfredmin_vs, rvv_fp_min32, false);
RVV_FP32_VV_OP(vfmax_vv, rvv_fp_max32, false);
RVV_FP32_VF_OP(vfmax_vf, rvv_fp_max32, false);
RVV_FP32_RED_OP(vfredmax_vs, rvv_fp_max32, false);
RVV_FP32_VV_OP(vfsgnj_vv, rvv_fp_sgnj32, false);
RVV_FP32_VF_OP(vfsgnj_vf, rvv_fp_sgnj32, false);
RVV_FP32_VV_OP(vfsgnjn_vv, rvv_fp_sgnjn32, false);
RVV_FP32_VF_OP(vfsgnjn_vf, rvv_fp_sgnjn32, false);
RVV_FP32_VV_OP(vfsgnjx_vv, rvv_fp_sgnjx32, false);
RVV_FP32_VF_OP(vfsgnjx_vf, rvv_fp_sgnjx32, false);

RVOP(vfslide1up_vf, {
    if (rvv_require_operable(rv))
        return false;
    if ((rvv_sew_bits(rv->csr_vtype) != 32) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_vfslide1up(rv, ir, ir->vd);
})

RVOP(vfslide1down_vf, {
    if (rvv_require_operable(rv))
        return false;
    if ((rvv_sew_bits(rv->csr_vtype) != 32) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_vfslide1down(rv, ir, ir->vd);
})

RVOP(vfmerge_vfm, {
    if (rvv_require_operable(rv))
        return false;
    if ((rvv_sew_bits(rv->csr_vtype) != 32) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2) || ir->vm)
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_vfmerge_vfm(rv, ir, ir->vd);
})

RVOP(vfmv_v_f, {
    if (rvv_require_operable(rv))
        return false;
    if ((rvv_sew_bits(rv->csr_vtype) != 32) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vd) || !ir->vm)
        return rvv_trap_illegal_state(rv, 0);
    rvv_exec_vfmv_v_f(rv, ir, ir->vd);
})

RVV_FP32_CMP_VV_OP(vmfeq_vv, rvv_fp_eq32);
RVV_FP32_CMP_VF_OP(vmfeq_vf, rvv_fp_eq32, false);
RVV_FP32_CMP_VV_OP(vmfle_vv, rvv_fp_le32);
RVV_FP32_CMP_VF_OP(vmfle_vf, rvv_fp_le32, false);
RVV_FP32_CMP_VV_OP(vmflt_vv, rvv_fp_lt32);
RVV_FP32_CMP_VF_OP(vmflt_vf, rvv_fp_lt32, false);
RVV_FP32_CMP_VV_OP(vmfne_vv, rvv_fp_ne32);
RVV_FP32_CMP_VF_OP(vmfne_vf, rvv_fp_ne32, false);
RVV_FP32_CMP_VF_OP(vmfgt_vf, rvv_fp_lt32, true);
RVV_FP32_CMP_VF_OP(vmfge_vf, rvv_fp_le32, true);

RVV_FP32_VV_OP(vfdiv_vv, rvv_fp_div32, true);
RVV_FP32_VF_OP(vfdiv_vf, rvv_fp_div32, true);

RVOP(vfrdiv_vf, {
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    if (rvv_require_operable(rv))
        return false;
    if ((rvv_sew_bits(rv->csr_vtype) != 32) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_fp_begin_round(rv);
    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, ir->vd, elem, 32, 0xFFFFFFFFU);
            continue;
        }
        rvv_set_elem(rv, ir->vd, elem, 32,
                     rvv_fp_div32(rv->F[ir->rs1].v,
                                  rvv_get_elem(rv, ir->vs2, elem, 32)));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, ir->vd, elem, 32, 0xFFFFFFFFU);
    }
    rv->csr_vstart = 0;
    set_fflag(rv);
})

RVV_FP32_VV_OP(vfmul_vv, rvv_fp_mul32, true);
RVV_FP32_VF_OP(vfmul_vf, rvv_fp_mul32, true);

RVOP(vfrsub_vf, {
    uint32_t vlmax = rvv_vlmax(rv->csr_vtype);
    uint8_t vma = (rv->csr_vtype >> 7) & 0x1;
    uint8_t vta = (rv->csr_vtype >> 6) & 0x1;
    if (rvv_require_operable(rv))
        return false;
    if ((rvv_sew_bits(rv->csr_vtype) != 32) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vd) ||
        !rvv_validate_data_reg(rv->csr_vtype, ir->vs2))
        return rvv_trap_illegal_state(rv, 0);
    rvv_fp_begin_round(rv);
    for (uint32_t elem = rv->csr_vstart; elem < rv->csr_vl; elem++) {
        if (!rvv_mask_enabled_for_elem(rv, ir, elem)) {
            if (vma)
                rvv_set_elem(rv, ir->vd, elem, 32, 0xFFFFFFFFU);
            continue;
        }
        rvv_set_elem(rv, ir->vd, elem, 32,
                     rvv_fp_sub32(rv->F[ir->rs1].v,
                                  rvv_get_elem(rv, ir->vs2, elem, 32)));
    }
    if (vta) {
        for (uint32_t elem = rv->csr_vl; elem < vlmax; elem++)
            rvv_set_elem(rv, ir->vd, elem, 32, 0xFFFFFFFFU);
    }
    rv->csr_vstart = 0;
    set_fflag(rv);
})

RVV_FP32_MAC_VV_OP(vfmadd_vv, rvv_fp_fmadd32);
RVV_FP32_MAC_VF_OP(vfmadd_vf, rvv_fp_fmadd32);
RVV_FP32_MAC_VV_OP(vfnmadd_vv, rvv_fp_fnmadd32);
RVV_FP32_MAC_VF_OP(vfnmadd_vf, rvv_fp_fnmadd32);
RVV_FP32_MAC_VV_OP(vfmsub_vv, rvv_fp_fmsub32);
RVV_FP32_MAC_VF_OP(vfmsub_vf, rvv_fp_fmsub32);
RVV_FP32_MAC_VV_OP(vfnmsub_vv, rvv_fp_fnmsub32);
RVV_FP32_MAC_VF_OP(vfnmsub_vf, rvv_fp_fnmsub32);
RVV_FP32_MAC_VV_OP(vfmacc_vv, rvv_fp_fmacc32);
RVV_FP32_MAC_VF_OP(vfmacc_vf, rvv_fp_fmacc32);
RVV_FP32_MAC_VV_OP(vfnmacc_vv, rvv_fp_fnmacc32);
RVV_FP32_MAC_VF_OP(vfnmacc_vf, rvv_fp_fnmacc32);
RVV_FP32_MAC_VV_OP(vfmsac_vv, rvv_fp_fmsac32);
RVV_FP32_MAC_VF_OP(vfmsac_vf, rvv_fp_fmsac32);
RVV_FP32_MAC_VV_OP(vfnmsac_vv, rvv_fp_fnmsac32);
RVV_FP32_MAC_VF_OP(vfnmsac_vf, rvv_fp_fnmsac32);

RVV_FP64_WIDEN_VV_OP(vfwadd_vv, rvv_fp_wadd64);
RVV_FP64_WIDEN_VF_OP(vfwadd_vf, rvv_fp_wadd64);
RVV_FP64_RED_OP(vfwredusum_vs, rvv_fp_wadd_mixed64);
RVV_FP64_WIDEN_VV_OP(vfwsub_vv, rvv_fp_wsub64);
RVV_FP64_WIDEN_VF_OP(vfwsub_vf, rvv_fp_wsub64);
RVV_FP64_RED_OP(vfwredosum_vs, rvv_fp_wadd_mixed64);
RVV_FP64_MIXED_VV_OP(vfwadd_wv, rvv_fp_wadd_mixed64);
RVV_FP64_MIXED_VF_OP(vfwadd_wf, rvv_fp_wadd_mixed64);
RVV_FP64_MIXED_VV_OP(vfwsub_wv, rvv_fp_wsub_mixed64);
RVV_FP64_MIXED_VF_OP(vfwsub_wf, rvv_fp_wsub_mixed64);
RVV_FP64_WIDEN_VV_OP(vfwmul_vv, rvv_fp_wmul64);
RVV_FP64_WIDEN_VF_OP(vfwmul_vf, rvv_fp_wmul64);
RVV_FP64_MAC_VV_OP(vfwmacc_vv, rvv_fp_wmacc64);
RVV_FP64_MAC_VF_OP(vfwmacc_vf, rvv_fp_wmacc64);
RVV_FP64_MAC_VV_OP(vfwnmacc_vv, rvv_fp_wnmacc64);
RVV_FP64_MAC_VF_OP(vfwnmacc_vf, rvv_fp_wnmacc64);
RVV_FP64_MAC_VV_OP(vfwmsac_vv, rvv_fp_wmsac64);
RVV_FP64_MAC_VF_OP(vfwmsac_vf, rvv_fp_wmsac64);
RVV_FP64_MAC_VV_OP(vfwnmsac_vv, rvv_fp_wnmsac64);
RVV_FP64_MAC_VF_OP(vfwnmsac_vf, rvv_fp_wnmsac64);
#else
RVOP(vfadd_vv, { V_NOP; })
RVOP(vfadd_vf, { V_NOP; })
RVOP(vfredusum_vs, { V_NOP; })
RVOP(vfsub_vv, { V_NOP; })
RVOP(vfsub_vf, { V_NOP; })
RVOP(vfredosum_vs, { V_NOP; })
RVOP(vfmin_vv, { V_NOP; })
RVOP(vfmin_vf, { V_NOP; })
RVOP(vfredmin_vs, { V_NOP; })
RVOP(vfmax_vv, { V_NOP; })
RVOP(vfmax_vf, { V_NOP; })
RVOP(vfredmax_vs, { V_NOP; })
RVOP(vfsgnj_vv, { V_NOP; })
RVOP(vfsgnj_vf, { V_NOP; })
RVOP(vfsgnjn_vv, { V_NOP; })
RVOP(vfsgnjn_vf, { V_NOP; })
RVOP(vfsgnjx_vv, { V_NOP; })
RVOP(vfsgnjx_vf, { V_NOP; })
RVOP(vfslide1up_vf, { V_NOP; })
RVOP(vfslide1down_vf, { V_NOP; })
RVOP(vfmerge_vfm, { V_NOP; })
RVOP(vfmv_v_f, { V_NOP; })
RVOP(vmfeq_vv, { V_NOP; })
RVOP(vmfeq_vf, { V_NOP; })
RVOP(vmfle_vv, { V_NOP; })
RVOP(vmfle_vf, { V_NOP; })
RVOP(vmflt_vv, { V_NOP; })
RVOP(vmflt_vf, { V_NOP; })
RVOP(vmfne_vv, { V_NOP; })
RVOP(vmfne_vf, { V_NOP; })
RVOP(vmfgt_vf, { V_NOP; })
RVOP(vmfge_vf, { V_NOP; })
RVOP(vfdiv_vv, { V_NOP; })
RVOP(vfdiv_vf, { V_NOP; })
RVOP(vfrdiv_vf, { V_NOP; })
RVOP(vfmul_vv, { V_NOP; })
RVOP(vfmul_vf, { V_NOP; })
RVOP(vfrsub_vf, { V_NOP; })
RVOP(vfmadd_vv, { V_NOP; })
RVOP(vfmadd_vf, { V_NOP; })
RVOP(vfnmadd_vv, { V_NOP; })
RVOP(vfnmadd_vf, { V_NOP; })
RVOP(vfmsub_vv, { V_NOP; })
RVOP(vfmsub_vf, { V_NOP; })
RVOP(vfnmsub_vv, { V_NOP; })
RVOP(vfnmsub_vf, { V_NOP; })
RVOP(vfmacc_vv, { V_NOP; })
RVOP(vfmacc_vf, { V_NOP; })
RVOP(vfnmacc_vv, { V_NOP; })
RVOP(vfnmacc_vf, { V_NOP; })
RVOP(vfmsac_vv, { V_NOP; })
RVOP(vfmsac_vf, { V_NOP; })
RVOP(vfnmsac_vv, { V_NOP; })
RVOP(vfnmsac_vf, { V_NOP; })
RVOP(vfwadd_vv, { V_NOP; })
RVOP(vfwadd_vf, { V_NOP; })
RVOP(vfwredusum_vs, { V_NOP; })
RVOP(vfwsub_vv, { V_NOP; })
RVOP(vfwsub_vf, { V_NOP; })
RVOP(vfwredosum_vs, { V_NOP; })
RVOP(vfwadd_wv, { V_NOP; })
RVOP(vfwadd_wf, { V_NOP; })
RVOP(vfwsub_wv, { V_NOP; })
RVOP(vfwsub_wf, { V_NOP; })
RVOP(vfwmul_vv, { V_NOP; })
RVOP(vfwmul_vf, { V_NOP; })
RVOP(vfwmacc_vv, { V_NOP; })
RVOP(vfwmacc_vf, { V_NOP; })
RVOP(vfwnmacc_vv, { V_NOP; })
RVOP(vfwnmacc_vf, { V_NOP; })
RVOP(vfwmsac_vv, { V_NOP; })
RVOP(vfwmsac_vf, { V_NOP; })
RVOP(vfwnmsac_vv, { V_NOP; })
RVOP(vfwnmsac_vf, { V_NOP; })
#endif
