/* Declarative trace matcher acceptance and near-miss corpus. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "trace_match.h"

static void set_operand(rv_insn_t *ir, uint8_t operand, uint8_t value)
{
    switch (operand) {
    case TRACE_OPERAND_RD:
        ir->rd = value;
        break;
    case TRACE_OPERAND_RS1:
        ir->rs1 = value;
        break;
    case TRACE_OPERAND_RS2:
        ir->rs2 = value;
        break;
    default:
        assert(false);
    }
}

static void make_trace(rv_insn_t *records, const trace_match_spec_t *spec)
{
    assert(spec->count <= TRACE_MATCH_MAX_LENGTH);
    memset(records, 0, TRACE_MATCH_MAX_LENGTH * sizeof(*records));

    for (size_t i = 0; i < spec->count; i++) {
        const trace_match_insn_t *match = &spec->insns[i];
        records[i].opcode = match->opcode;
        records[i].rd = match->rd;
        records[i].rs1 = match->rs1;
        records[i].rs2 = match->rs2;
        records[i].imm = match->imm;
        records[i].next = i + 1 < spec->count ? &records[i + 1] : NULL;
    }
}

static void check_trace(const trace_match_spec_t *spec)
{
    rv_insn_t records[TRACE_MATCH_MAX_LENGTH];
    const rv_insn_t *terminal;

    make_trace(records, spec);
    assert(trace_match(records, spec, &terminal));
    assert(terminal == &records[spec->count - 1]);

    make_trace(records, spec);
    records[0].opcode = rv_insn_nop;
    assert(!trace_match(records, spec, NULL));

    if (spec->count > 1) {
        make_trace(records, spec);
        records[spec->count - 2].next = NULL;
        assert(!trace_match(records, spec, NULL));
    }

    for (size_t i = 0; i < spec->count; i++) {
        const trace_match_insn_t *match = &spec->insns[i];

        if (match->fields & TRACE_MATCH_RD) {
            make_trace(records, spec);
            records[i].rd ^= 1;
            assert(!trace_match(records, spec, NULL));
        }
        if (match->fields & TRACE_MATCH_RS1) {
            make_trace(records, spec);
            records[i].rs1 ^= 1;
            assert(!trace_match(records, spec, NULL));
        }
        if (match->fields & TRACE_MATCH_RS2) {
            make_trace(records, spec);
            records[i].rs2 ^= 1;
            assert(!trace_match(records, spec, NULL));
        }
        if (match->fields & TRACE_MATCH_IMM) {
            make_trace(records, spec);
            records[i].imm ^= 1;
            assert(!trace_match(records, spec, NULL));
        }
    }

    for (size_t i = 0; i < spec->relation_count; i++) {
        const trace_match_relation_t *relation = &spec->relations[i];

        make_trace(records, spec);
        set_operand(&records[relation->lhs_index], relation->lhs_operand, 1);
        assert(!trace_match(records, spec, NULL));
    }
}

int main(void)
{
    for (size_t i = 0; i < trace_match_count; i++)
        check_trace(&trace_match_specs[i]);

    /* A specification with no instructions has no terminal record to check
     * or report, so it must never match. */
    rv_insn_t empty_records[TRACE_MATCH_MAX_LENGTH];
    trace_match_spec_t empty = trace_match_specs[trace_byte_copy];
    make_trace(empty_records, &empty);
    empty.count = 0;
    empty.relations = NULL;
    empty.relation_count = 0;
    empty.terminal_branch = false;
    assert(!trace_match(empty_records, &empty, NULL));

    puts("trace matcher corpus passed");
    return 0;
}
