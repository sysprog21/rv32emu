/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#pragma once

#include "decode.h"

/* Longest instruction sequence a specification may describe. */
#define TRACE_MATCH_MAX_LENGTH 24

enum trace_match_field {
    TRACE_MATCH_RD = 1 << 0,
    TRACE_MATCH_RS1 = 1 << 1,
    TRACE_MATCH_RS2 = 1 << 2,
    TRACE_MATCH_IMM = 1 << 3,
};

typedef struct {
    uint16_t opcode;
    uint8_t fields;
    uint8_t rd;
    uint8_t rs1;
    uint8_t rs2;
    int32_t imm;
} trace_match_insn_t;

enum trace_match_operand {
    TRACE_OPERAND_RD,
    TRACE_OPERAND_RS1,
    TRACE_OPERAND_RS2,
};

typedef struct {
    uint8_t lhs_index;
    uint8_t lhs_operand;
    uint8_t rhs_index;
    uint8_t rhs_operand;
} trace_match_relation_t;

typedef struct {
    const trace_match_insn_t *insns;
    size_t count;
    const trace_match_relation_t *relations;
    size_t relation_count;
    bool terminal_branch;
} trace_match_spec_t;

enum trace_match_id {
    trace_byte_copy,
    trace_backward_byte_copy,
    trace_emfloat_halfword_shift,
    trace_strlen_word,
#if !RV32_HAS(RV32E)
    trace_numsift_index,
    trace_bitfield_set_bit,
    trace_bitfield_invert_bit,
    trace_record_copy,
    trace_primes_probe,
#endif
#if !RV32_HAS(RV32E) && RV32_HAS(EXT_M)
    trace_idea_round_prefix,
    trace_idea_alu_suffix,
#endif
    trace_match_count,
};

extern const trace_match_spec_t trace_match_specs[trace_match_count];

bool trace_match(const rv_insn_t *ir,
                 const trace_match_spec_t *spec,
                 const rv_insn_t **terminal);
