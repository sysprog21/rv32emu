/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include "trace_match.h"

#define MATCH(op, fields_, rd_, rs1_, rs2_, imm_) \
    {rv_insn_##op, fields_, rd_, rs1_, rs2_, imm_}
#define RD TRACE_MATCH_RD
#define RS1 TRACE_MATCH_RS1
#define RS2 TRACE_MATCH_RS2
#define IMM TRACE_MATCH_IMM
#define NEXT_RD(index) {index, TRACE_OPERAND_RD, index, TRACE_OPERAND_RS1}
#define EQUAL(lhs_index_, lhs_, rhs_index_, rhs_) \
    {lhs_index_, TRACE_OPERAND_##lhs_, rhs_index_, TRACE_OPERAND_##rhs_}

#if !RV32_HAS(RV32E)
static const trace_match_insn_t numsift_index[] = {
    MATCH(addi, RD | RS1 | IMM, rv_reg_a6, rv_reg_a5, 0, 1),
    MATCH(slli, RD | RS1 | IMM, rv_reg_a4, rv_reg_a1, 0, 3),
    MATCH(slli, RD | RS1 | IMM, rv_reg_a3, rv_reg_a6, 0, 2),
    MATCH(add, RD | RS1 | RS2, rv_reg_a3, rv_reg_a0, rv_reg_a3, 0),
    MATCH(add, RD | RS1 | RS2, rv_reg_a4, rv_reg_a0, rv_reg_a4, 0),
    MATCH(slli, RD | RS1 | IMM, rv_reg_a7, rv_reg_a5, 0, 2),
    MATCH(bgeu, RS1 | RS2, 0, rv_reg_a5, rv_reg_a2, 0),
};

static const trace_match_insn_t bitfield_set_bit[] = {
    MATCH(srli, RD | RS1 | IMM, rv_reg_a4, rv_reg_a5, 0, 5),
    MATCH(slli, RD | RS1 | IMM, rv_reg_a4, rv_reg_a4, 0, 2),
    MATCH(add, RD | RS1 | RS2, rv_reg_a4, rv_reg_s0, rv_reg_a4, 0),
    MATCH(lw, RD | RS1 | IMM, rv_reg_a3, rv_reg_a4, 0, 0),
    MATCH(sll, RD | RS1 | RS2, rv_reg_a2, rv_reg_a6, rv_reg_a5, 0),
    MATCH(addi, RD | RS1 | IMM, rv_reg_a5, rv_reg_a5, 0, 1),
    MATCH(or, RD | RS1 | RS2, rv_reg_a3, rv_reg_a3, rv_reg_a2, 0),
    MATCH(sw, RS1 | RS2 | IMM, 0, rv_reg_a4, rv_reg_a3, 0),
    MATCH(bne, RS1 | RS2 | IMM, 0, rv_reg_a5, rv_reg_t4, -32),
};

static const trace_match_insn_t bitfield_invert_bit[] = {
    MATCH(srli, RD | RS1 | IMM, rv_reg_a4, rv_reg_a5, 0, 5),
    MATCH(slli, RD | RS1 | IMM, rv_reg_a4, rv_reg_a4, 0, 2),
    MATCH(add, RD | RS1 | RS2, rv_reg_a4, rv_reg_s0, rv_reg_a4, 0),
    MATCH(lw, RD | RS1 | IMM, rv_reg_a3, rv_reg_a4, 0, 0),
    MATCH(sll, RD | RS1 | RS2, rv_reg_a2, rv_reg_a6, rv_reg_a5, 0),
    MATCH(addi, RD | RS1 | IMM, rv_reg_a5, rv_reg_a5, 0, 1),
    MATCH(xor, RD | RS1 | RS2, rv_reg_a3, rv_reg_a3, rv_reg_a2, 0),
    MATCH(sw, RS1 | RS2 | IMM, 0, rv_reg_a4, rv_reg_a3, 0),
    MATCH(bne, RS1 | RS2 | IMM, 0, rv_reg_a5, rv_reg_t4, -32),
};
#endif

static const trace_match_insn_t emfloat_halfword_shift[] = {
    MATCH(lhu, 0, 0, 0, 0, 0),  MATCH(slli, 0, 0, 0, 0, 0),
    MATCH(addi, 0, 0, 0, 0, 0), MATCH(srli, 0, 0, 0, 0, 0),
    MATCH(or, 0, 0, 0, 0, 0),   MATCH(sh, 0, 0, 0, 0, 0),
    MATCH(andi, 0, 0, 0, 0, 0), MATCH(bne, 0, 0, 0, 0, 0),
};

#if !RV32_HAS(RV32E)
#define RECORD_LOAD(dst, src, offset) \
    MATCH(lw, RD | RS1 | IMM, dst, src, 0, offset)
#define RECORD_STORE(base, src, offset) \
    MATCH(sw, RS1 | RS2 | IMM, 0, base, src, offset)
static const trace_match_insn_t record_copy[] = {
    RECORD_LOAD(rv_reg_a4, rv_reg_gp, -1880),
    RECORD_LOAD(rv_reg_a5, rv_reg_a0, 0),
    RECORD_LOAD(rv_reg_a2, rv_reg_a4, 40),
    RECORD_LOAD(rv_reg_t2, rv_reg_a4, 0),
    RECORD_LOAD(rv_reg_t0, rv_reg_a4, 4),
    RECORD_LOAD(rv_reg_t6, rv_reg_a4, 8),
    RECORD_LOAD(rv_reg_t5, rv_reg_a4, 12),
    RECORD_LOAD(rv_reg_t4, rv_reg_a4, 16),
    RECORD_LOAD(rv_reg_t3, rv_reg_a4, 20),
    RECORD_LOAD(rv_reg_t1, rv_reg_a4, 24),
    RECORD_LOAD(rv_reg_a7, rv_reg_a4, 28),
    RECORD_LOAD(rv_reg_a6, rv_reg_a4, 32),
    RECORD_LOAD(rv_reg_a1, rv_reg_a4, 36),
    RECORD_STORE(rv_reg_a5, rv_reg_t2, 0),
    RECORD_STORE(rv_reg_a5, rv_reg_t5, 12),
    RECORD_STORE(rv_reg_a5, rv_reg_a2, 40),
    RECORD_STORE(rv_reg_a5, rv_reg_t0, 4),
    RECORD_STORE(rv_reg_a5, rv_reg_t6, 8),
    RECORD_STORE(rv_reg_a5, rv_reg_t4, 16),
    RECORD_STORE(rv_reg_a5, rv_reg_t3, 20),
    RECORD_STORE(rv_reg_a5, rv_reg_t1, 24),
    RECORD_STORE(rv_reg_a5, rv_reg_a7, 28),
    RECORD_STORE(rv_reg_a5, rv_reg_a6, 32),
    RECORD_STORE(rv_reg_a5, rv_reg_a1, 36),
};
#undef RECORD_STORE
#undef RECORD_LOAD
#endif

static const trace_match_insn_t byte_copy[] = {
    MATCH(lbu, 0, 0, 0, 0, 0),  MATCH(addi, 0, 0, 0, 0, 0),
    MATCH(addi, 0, 0, 0, 0, 0), MATCH(sb, 0, 0, 0, 0, 0),
    MATCH(bne, 0, 0, 0, 0, 0),
};

static const trace_match_relation_t byte_copy_relations[] = {
    NEXT_RD(1),
    NEXT_RD(2),
    EQUAL(4, RS1, 1, RD),
};

/* The overlapping backwards arm of newlib memmove.  Unlike byte_copy, its
 * terminal compares the original destination against the decremented source
 * register, so the branch must use its late architectural register reads. */
static const trace_match_insn_t backward_byte_copy[] = {
    MATCH(lbu, RD | RS1 | IMM, rv_reg_a4, rv_reg_a5, 0, -1),
    MATCH(addi, RD | RS1 | IMM, rv_reg_a2, rv_reg_a2, 0, -1),
    MATCH(addi, RD | RS1 | IMM, rv_reg_a5, rv_reg_a5, 0, -1),
    MATCH(sb, RS1 | RS2 | IMM, 0, rv_reg_a2, rv_reg_a4, 0),
    MATCH(bne, RS1 | RS2 | IMM, 0, rv_reg_a0, rv_reg_a2, -16),
};

/* Primes' sieve probe: shifts, two word loads, bit operations and a BNE. */
#define EXACT(op, rd_, rs1_, rs2_, imm_)                         \
    MATCH(op, RD | RS1 | RS2 | IMM, rv_reg_##rd_, rv_reg_##rs1_, \
          rv_reg_##rs2_, imm_)
#if !RV32_HAS(RV32E)
static const trace_match_insn_t primes_probe[] = {
    EXACT(slli, a5, a5, zero, 3), EXACT(add, a5, a0, a5, 0),
    EXACT(lw, a7, a5, zero, 0),   EXACT(lw, a6, a5, zero, 4),
    EXACT(sll, t0, t1, a3, 0),    EXACT(srai, s0, t0, zero, 0x41f),
    EXACT(and, t6, a7, t0, 0),    EXACT(and, s1, a6, s0, 0),
    EXACT(add, t2, a3, a1, 0),    EXACT(sltu, a3, t2, a3, 0),
    EXACT(add, a4, a4, a2, 0),    EXACT(or, t6, t6, s1, 0),
    EXACT(add, a4, a3, a4, 0),    EXACT(or, a7, a7, t0, 0),
    EXACT(or, a6, a6, s0, 0),     EXACT(bne, zero, t6, zero, 12),
};
#endif

#if !RV32_HAS(RV32E) && RV32_HAS(EXT_M)
/* The two IDEA round runs.  Each fuses its first 16 records; the final BEQ
 * stays a separate generic record and is matched only to anchor the run. */
static const trace_match_insn_t idea_round_prefix[] = {
    EXACT(mul, a4, a4, a6, 0),
    EXACT(slli, a6, a4, zero, 16),
    EXACT(srli, a6, a6, zero, 16),
    EXACT(srli, a4, a4, zero, 16),
    EXACT(sub, a5, a6, a4, 0),
    EXACT(sltu, a6, a6, a4, 0),
    EXACT(add, a6, a6, a5, 0),
    EXACT(slli, a6, a6, zero, 16),
    EXACT(srli, a6, a6, zero, 16),
    EXACT(lhu, t1, a0, zero, 4),
    EXACT(lhu, a5, a0, zero, 2),
    EXACT(lhu, a7, a0, zero, 6),
    EXACT(add, t1, t5, t1, 0),
    EXACT(slli, t1, t1, zero, 16),
    EXACT(srli, t1, t1, zero, 16),
    EXACT(add, t5, t0, a5, 0),
    MATCH(beq, RS1 | RS2, 0, rv_reg_t6, rv_reg_zero, 0),
};

static const trace_match_insn_t idea_alu_suffix[] = {
    EXACT(mul, a4, a4, a5, 0),
    EXACT(slli, a5, a4, zero, 16),
    EXACT(srli, a5, a5, zero, 16),
    EXACT(srli, a4, a4, zero, 16),
    EXACT(sub, t6, a5, a4, 0),
    EXACT(sltu, a5, a5, a4, 0),
    EXACT(add, a5, a5, t6, 0),
    EXACT(slli, a5, a5, zero, 16),
    EXACT(srli, a5, a5, zero, 16),
    EXACT(add, a3, a3, a5, 0),
    EXACT(slli, a3, a3, zero, 16),
    EXACT(srli, a3, a3, zero, 16),
    EXACT(xor, t0, t1, a5, 0),
    EXACT(xor, a4, a6, a5, 0),
    EXACT(xor, t5, t5, a3, 0),
    EXACT(xor, t6, a7, a3, 0),
    MATCH(beq, RS1 | RS2 | IMM, 0, rv_reg_t4, rv_reg_a0, 116),
};
#endif

/* newlib strlen's word-at-a-time zero-byte scan. */
static const trace_match_insn_t strlen_word[] = {
    EXACT(lw, a2, a4, zero, 0),    EXACT(addi, a4, a4, zero, 4),
    EXACT(and, a5, a2, a3, 0),     EXACT(add, a5, a5, a3, 0),
    EXACT(or, a5, a5, a2, 0),      EXACT(or, a5, a5, a3, 0),
    EXACT(beq, zero, a5, a1, -24),
};
#undef EXACT

const trace_match_spec_t trace_match_specs[trace_match_count] = {
    [trace_byte_copy] = {byte_copy, ARRAY_SIZE(byte_copy), byte_copy_relations,
                         ARRAY_SIZE(byte_copy_relations), true},
    [trace_backward_byte_copy] = {backward_byte_copy,
                                  ARRAY_SIZE(backward_byte_copy), NULL, 0,
                                  true},
    [trace_emfloat_halfword_shift] = {emfloat_halfword_shift,
                                      ARRAY_SIZE(emfloat_halfword_shift), NULL,
                                      0, true},
    [trace_strlen_word] = {strlen_word, ARRAY_SIZE(strlen_word), NULL, 0, true},
#if !RV32_HAS(RV32E)
    [trace_numsift_index] = {numsift_index, ARRAY_SIZE(numsift_index), NULL, 0,
                             true},
    [trace_bitfield_set_bit] = {bitfield_set_bit, ARRAY_SIZE(bitfield_set_bit),
                                NULL, 0, true},
    [trace_bitfield_invert_bit] = {bitfield_invert_bit,
                                   ARRAY_SIZE(bitfield_invert_bit), NULL, 0,
                                   true},
    [trace_record_copy] = {record_copy, ARRAY_SIZE(record_copy), NULL, 0,
                           false},
    [trace_primes_probe] = {primes_probe, ARRAY_SIZE(primes_probe), NULL, 0,
                            true},
#endif
#if !RV32_HAS(RV32E) && RV32_HAS(EXT_M)
    [trace_idea_round_prefix] = {idea_round_prefix,
                                 ARRAY_SIZE(idea_round_prefix), NULL, 0, true},
    [trace_idea_alu_suffix] = {idea_alu_suffix, ARRAY_SIZE(idea_alu_suffix),
                               NULL, 0, true},
#endif
};

static uint8_t operand(const rv_insn_t *ir, uint8_t field)
{
    switch (field) {
    case TRACE_OPERAND_RD:
        return ir->rd;
    case TRACE_OPERAND_RS1:
        return ir->rs1;
    case TRACE_OPERAND_RS2:
        return ir->rs2;
    default:
        assert(false);
        return 0;
    }
}

static bool is_terminal_branch(uint16_t opcode)
{
    switch (opcode) {
    case rv_insn_beq:
    case rv_insn_bne:
    case rv_insn_blt:
    case rv_insn_bge:
    case rv_insn_bltu:
    case rv_insn_bgeu:
        return true;
    default:
        return false;
    }
}

bool trace_match(const rv_insn_t *ir,
                 const trace_match_spec_t *spec,
                 const rv_insn_t **terminal)
{
    const rv_insn_t *records[TRACE_MATCH_MAX_LENGTH];

    if (!spec->count || spec->count > ARRAY_SIZE(records))
        return false;

    for (size_t i = 0; i < spec->count; i++, ir = ir->next) {
        const trace_match_insn_t *match = &spec->insns[i];
        if (!ir || ir->opcode != match->opcode ||
            ((match->fields & TRACE_MATCH_RD) && ir->rd != match->rd) ||
            ((match->fields & TRACE_MATCH_RS1) && ir->rs1 != match->rs1) ||
            ((match->fields & TRACE_MATCH_RS2) && ir->rs2 != match->rs2) ||
            ((match->fields & TRACE_MATCH_IMM) && ir->imm != match->imm))
            return false;
        records[i] = ir;
    }

    for (size_t i = 0; i < spec->relation_count; i++) {
        const trace_match_relation_t *relation = &spec->relations[i];
        if (relation->lhs_index >= spec->count ||
            relation->rhs_index >= spec->count ||
            operand(records[relation->lhs_index], relation->lhs_operand) !=
                operand(records[relation->rhs_index], relation->rhs_operand))
            return false;
    }

    if (spec->terminal_branch &&
        !is_terminal_branch(records[spec->count - 1]->opcode))
        return false;

    if (terminal)
        *terminal = records[spec->count - 1];
    return true;
}

#undef EQUAL
#undef NEXT_RD
#undef IMM
#undef RS2
#undef RS1
#undef RD
#undef MATCH
