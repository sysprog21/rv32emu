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

const trace_match_spec_t trace_match_specs[trace_match_count] = {
    [trace_byte_copy] = {byte_copy, ARRAY_SIZE(byte_copy), byte_copy_relations,
                         ARRAY_SIZE(byte_copy_relations), true},
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
