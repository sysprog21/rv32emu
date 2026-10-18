#include <stdio.h>
#include <string.h>

#include "decode.h"

static const char dispatch_tokens[] = {
#define _(inst, ...) [rv_insn_##inst] = 0,
    RV_INSN_LIST
#undef _
};
static const void *dispatch_table[] = {
#define _(inst, ...) [rv_insn_##inst] = &dispatch_tokens[rv_insn_##inst],
    RV_INSN_LIST
#undef _
};

static const unsigned char insn_len[] = {
#define _(inst, can_branch, len, ...) [rv_insn_##inst] = len,
    RV_INSN_LIST
#undef _
};

#define CONSTOPT(inst, code)                                         \
    static void UNUSED constopt_##inst(rv_insn_t *ir UNUSED,         \
                                       constopt_info_t *info UNUSED) \
    {                                                                \
        code;                                                        \
    }
#include "rv32_constopt.c"
#undef CONSTOPT

static int check_shifts(void)
{
    const uint32_t counts[] = {0, 1, 31, 32, 33, 63, 0x80000000, 0xffffffff};
    const struct {
        unsigned opcode, immediate_opcode;
        void (*optimize)(rv_insn_t *, constopt_info_t *);
        uint32_t funct; /* decode keeps SRAI's funct7 bit in imm */
    } shifts[] = {{rv_insn_sll, rv_insn_slli, constopt_sll, 0},
                  {rv_insn_srl, rv_insn_srli, constopt_srl, 0},
                  {rv_insn_sra, rv_insn_srai, constopt_sra, 0x400}};
    for (unsigned op = 0; op < ARRAY_SIZE(shifts); op++) {
        for (unsigned i = 0; i < ARRAY_SIZE(counts); i++) {
            for (unsigned rd = 1; rd <= 3; rd++) {
                constopt_info_t info = {0};
                info.is_constant[2] = info.is_constant[3] = true;
                info.const_val[2] = counts[i];
                rv_insn_t ir = {
                    .opcode = shifts[op].opcode, .rs1 = 1, .rs2 = 2, .rd = rd};
                shifts[op].optimize(&ir, &info);
                if (ir.opcode != shifts[op].immediate_opcode ||
                    (uint32_t) ir.imm !=
                        ((counts[i] & 31) | shifts[op].funct) ||
                    ir.rs1 != 1 || ir.rs2 != 0 || ir.rd != rd ||
                    info.is_constant[rd] ||
                    ir.impl != dispatch_table[ir.opcode]) {
                    fprintf(stderr, "known shift count was not reduced\n");
                    return 1;
                }
            }
        }
        constopt_info_t info = {0};
        info.is_constant[3] = true;
        info.const_val[2] = 5;
        rv_insn_t ir = {
            .opcode = shifts[op].opcode, .rs1 = 1, .rs2 = 2, .rd = 3};
        shifts[op].optimize(&ir, &info);
        if (ir.opcode != shifts[op].opcode || info.is_constant[3])
            return 1;
    }
    return 0;
}

/* Execute one IR record on a register file. Covers the operations a known
 * operand can turn a register-register instruction into.
 */
static bool eval_insn(const rv_insn_t *ir, uint32_t *x)
{
    const uint32_t a = x[ir->rs1], b = x[ir->rs2], imm = ir->imm;
    uint32_t v;
    switch (ir->opcode) {
    case rv_insn_lui:
        v = imm;
        break;
    case rv_insn_addi:
        v = a + imm;
        break;
    case rv_insn_xori:
        v = a ^ imm;
        break;
    case rv_insn_ori:
        v = a | imm;
        break;
    case rv_insn_andi:
        v = a & imm;
        break;
    case rv_insn_slti:
        v = (int32_t) a < (int32_t) imm;
        break;
    case rv_insn_sltiu:
        v = a < imm;
        break;
    case rv_insn_slli:
        v = a << (imm & 31);
        break;
    case rv_insn_add:
        v = a + b;
        break;
    case rv_insn_sub:
        v = a - b;
        break;
    case rv_insn_xor:
        v = a ^ b;
        break;
    case rv_insn_or:
        v = a | b;
        break;
    case rv_insn_and:
        v = a & b;
        break;
    case rv_insn_slt:
        v = (int32_t) a < (int32_t) b;
        break;
    case rv_insn_sltu:
        v = a < b;
        break;
#if RV32_HAS(EXT_M)
    case rv_insn_mul:
        v = a * b;
        break;
#endif
#if RV32_HAS(EXT_C)
    case rv_insn_caddi16sp:
        v = x[ir->rd] + imm;
        break;
    case rv_insn_candi:
        if (ir->rd != ir->rs1)
            return false;
        v = a & imm;
        break;
    case rv_insn_cadd:
        v = a + b;
        break;
    case rv_insn_csub:
        v = a - b;
        break;
    case rv_insn_cxor:
        v = a ^ b;
        break;
    case rv_insn_cor:
        v = a | b;
        break;
    case rv_insn_cand:
        v = a & b;
        break;
#endif
    default:
        return false;
    }
    if (ir->rd)
        x[ir->rd] = v;
    return true;
}

/* A register-register operation with one known operand may be rewritten into an
 * immediate form or a constant; either must compute what it did before.
 */
static int check_known_operand(void)
{
    /* sides: the operands (1 = rs1, 2 = rs2) whose known value must trigger a
     * rewrite. MUL is rewritten only for a factor of zero or a power of two.
     */
    const struct {
        unsigned opcode;
        void (*optimize)(rv_insn_t *, constopt_info_t *);
        unsigned sides;
    } ops[] = {
        {rv_insn_add, constopt_add, 3},   {rv_insn_sub, constopt_sub, 2},
        {rv_insn_xor, constopt_xor, 3},   {rv_insn_or, constopt_or, 3},
        {rv_insn_and, constopt_and, 3},   {rv_insn_slt, constopt_slt, 2},
        {rv_insn_sltu, constopt_sltu, 2},
#if RV32_HAS(EXT_M)
        {rv_insn_mul, constopt_mul, 3},
#endif
    };
    const uint32_t values[] = {0,          1,          2,          3,
                               0x7ff,      0x800,      0x7fffffff, 0x80000000,
                               0xfffff800, 0xfffffffe, 0xffffffff};
    for (unsigned op = 0; op < ARRAY_SIZE(ops); op++) {
        for (unsigned known = 1; known <= 2; known++) {
            for (unsigned rd = 1; rd <= 3; rd++) {
                for (unsigned k = 0; k < ARRAY_SIZE(values); k++) {
                    constopt_info_t info = {.is_constant[0] = true};
                    info.is_constant[known] = true;
                    info.const_val[known] = values[k];
                    info.is_constant[3] = true;
                    info.const_val[3] = 42;
                    rv_insn_t ir = {
                        .opcode = ops[op].opcode, .rs1 = 1, .rs2 = 2, .rd = rd};
                    const rv_insn_t orig = ir;
                    ops[op].optimize(&ir, &info);
                    bool rewrite = ops[op].sides & known;
#if RV32_HAS(EXT_M)
                    if (orig.opcode == rv_insn_mul)
                        rewrite = !values[k] || is_power_of_two(values[k]);
#endif
                    if ((ir.opcode != orig.opcode) != rewrite ||
                        (rewrite && ir.impl != dispatch_table[ir.opcode])) {
                        fprintf(stderr,
                                "opcode %u with x%u = %08x was %srewritten\n",
                                orig.opcode, known, values[k],
                                rewrite ? "not " : "");
                        return 1;
                    }
                    for (unsigned u = 0; u < ARRAY_SIZE(values); u++) {
                        uint32_t want[N_RV_REGS] = {0}, got[N_RV_REGS] = {0};
                        want[known] = got[known] = values[k];
                        want[3 - known] = got[3 - known] = values[u];
                        want[3] = got[3] = 42;
                        if (!eval_insn(&orig, want) || !eval_insn(&ir, got) ||
                            memcmp(want, got, sizeof(want)) ||
                            (info.is_constant[rd] &&
                             info.const_val[rd] != want[rd])) {
                            fprintf(stderr,
                                    "opcode %u with x%u = %08x, rd = x%u "
                                    "became opcode %u\n",
                                    orig.opcode, known, values[k], rd,
                                    ir.opcode);
                            return 1;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

#if RV32_HAS(EXT_C)
/* The compressed forms update rd = rs1 in place. A rewrite must keep their
 * 2-byte length, since each handler advances the PC by its opcode's length.
 */
static int check_compressed_known_operand(void)
{
    /* sides: as in check_known_operand(). Only a known rs2 has an in-place
     * immediate form, and C.OR and C.XOR have none.
     */
    const struct {
        unsigned opcode;
        void (*optimize)(rv_insn_t *, constopt_info_t *);
        unsigned sides;
    } ops[] = {
        {rv_insn_cadd, constopt_cadd, 2}, {rv_insn_csub, constopt_csub, 2},
        {rv_insn_cxor, constopt_cxor, 0}, {rv_insn_cor, constopt_cor, 0},
        {rv_insn_cand, constopt_cand, 2},
    };
    const uint32_t values[] = {0,          1,          0x7ff,      0x800,
                               0x7fffffff, 0x80000000, 0xfffff800, 0xffffffff};
    for (unsigned op = 0; op < ARRAY_SIZE(ops); op++) {
        for (unsigned known = 1; known <= 2; known++) {
            for (unsigned k = 0; k < ARRAY_SIZE(values); k++) {
                constopt_info_t info = {.is_constant[0] = true};
                info.is_constant[known] = true;
                info.const_val[known] = values[k];
                rv_insn_t ir = {
                    .opcode = ops[op].opcode, .rs1 = 1, .rs2 = 2, .rd = 1};
                const rv_insn_t orig = ir;
                ops[op].optimize(&ir, &info);
                const bool rewrite = ops[op].sides & known;
                if ((ir.opcode != orig.opcode) != rewrite ||
                    (rewrite && ir.impl != dispatch_table[ir.opcode]) ||
                    insn_len[ir.opcode] != 2 ||
                    (ir.opcode != orig.opcode && ir.rs1 != ir.rd))
                    return 1;
                for (unsigned u = 0; u < ARRAY_SIZE(values); u++) {
                    uint32_t want[N_RV_REGS] = {0}, got[N_RV_REGS] = {0};
                    want[known] = got[known] = values[k];
                    want[3 - known] = got[3 - known] = values[u];
                    if (!eval_insn(&orig, want) || !eval_insn(&ir, got) ||
                        memcmp(want, got, sizeof(want)) ||
                        (info.is_constant[1] && info.const_val[1] != want[1])) {
                        fprintf(stderr,
                                "opcode %u with x%u = %08x became opcode %u\n",
                                orig.opcode, known, values[k], ir.opcode);
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}
#endif

#if RV32_HAS(EXT_A)
static int check_scw(void)
{
    for (unsigned rd = 0; rd <= 3; rd++) {
        constopt_info_t info = {0};
        info.is_constant[0] = info.is_constant[rd] = true;
        info.const_val[rd] = rd ? 42 : 0;
        rv_insn_t ir = {.opcode = rv_insn_scw, .rs1 = 1, .rs2 = 2, .rd = rd};
        constopt_scw(&ir, &info);
        if (info.is_constant[rd] != (rd == 0) || !info.is_constant[0] ||
            info.const_val[0] != 0 || ir.opcode != rv_insn_scw) {
            fprintf(stderr, "SC.W retained a stale destination constant\n");
            return 1;
        }
        if (rd) {
            ir = (rv_insn_t) {
                .opcode = rv_insn_addi, .rs1 = rd, .rd = 4, .imm = 1};
            constopt_addi(&ir, &info);
            if (ir.opcode != rv_insn_addi || info.is_constant[4])
                return 1;
        }
    }
    return 0;
}
#endif

#if RV32_HAS(EXT_M)
static int check_mulhu(void)
{
    const uint32_t values[] = {0,          1,          0x7fffffff,
                               0x80000000, 0xfffffffe, 0xffffffff};
    for (unsigned a = 0; a < ARRAY_SIZE(values); a++) {
        for (unsigned b = 0; b < ARRAY_SIZE(values); b++) {
            for (unsigned rd = 1; rd <= 3; rd++) {
                constopt_info_t info = {0};
                info.is_constant[1] = info.is_constant[2] = true;
                info.const_val[1] = values[a];
                info.const_val[2] = values[b];
                rv_insn_t ir = {
                    .opcode = rv_insn_mulhu, .rs1 = 1, .rs2 = 2, .rd = rd};
                constopt_mulhu(&ir, &info);
                uint32_t want = ((uint64_t) values[a] * values[b]) >> 32;
                if (ir.opcode != rv_insn_lui || (uint32_t) ir.imm != want ||
                    !info.is_constant[rd] || info.const_val[rd] != want ||
                    ir.impl != dispatch_table[rv_insn_lui]) {
                    fprintf(stderr, "MULHU folding failed: %08x * %08x\n",
                            values[a], values[b]);
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int check_unsigned_division(void)
{
    const uint32_t dividends[] = {0,          1,          31,        32,
                                  0x7fffffff, 0x80000000, 0xffffffff};
    for (unsigned shift = 0; shift < 32; shift++) {
        uint32_t divisor = UINT32_C(1) << shift;
        for (unsigned rem = 0; rem <= 1; rem++) {
            for (unsigned rd = 1; rd <= 3; rd++) {
                constopt_info_t info = {0};
                info.is_constant[2] = true;
                info.const_val[2] = divisor;
                info.is_constant[3] = true;
                info.const_val[3] = 42;
                rv_insn_t ir = {.opcode = rem ? rv_insn_remu : rv_insn_divu,
                                .rs1 = 1,
                                .rs2 = 2,
                                .rd = rd};
                if (rem)
                    constopt_remu(&ir, &info);
                else
                    constopt_divu(&ir, &info);
                unsigned opcode = rem ? rv_insn_andi : rv_insn_srli;
                if (ir.opcode != opcode || ir.rs1 != 1 || ir.rd != rd ||
                    info.is_constant[rd] || ir.impl != dispatch_table[opcode]) {
                    fprintf(stderr, "unsigned division was not reduced\n");
                    return 1;
                }
                for (unsigned i = 0; i < ARRAY_SIZE(dividends); i++) {
                    uint32_t n = dividends[i];
                    uint32_t got = rem ? n & (uint32_t) ir.imm : n >> ir.imm;
                    uint32_t want = rem ? n % divisor : n / divisor;
                    if (got != want) {
                        fprintf(stderr, "unsigned division reduction failed\n");
                        return 1;
                    }
                }
            }
        }
    }
    const uint32_t divisors[] = {0, 3, 0x7fffffff, 0xffffffff};
    for (unsigned i = 0; i < ARRAY_SIZE(divisors); i++) {
        constopt_info_t info = {0};
        info.is_constant[2] = true;
        info.const_val[2] = divisors[i];
        rv_insn_t ir = {.opcode = rv_insn_divu, .rs1 = 1, .rs2 = 2, .rd = 3};
        constopt_divu(&ir, &info);
        if (ir.opcode != rv_insn_divu || info.is_constant[3])
            return 1;
        ir.opcode = rv_insn_remu;
        constopt_remu(&ir, &info);
        if (ir.opcode != rv_insn_remu || info.is_constant[3])
            return 1;
    }
    return 0;
}
#endif

#if RV32_HAS(EXT_M)
/* A wider signed oracle also represents INT32_MIN / -1. */
static int check_signed_division(void)
{
    const uint32_t values[] = {0,          1,          2,          3,
                               0x7fffffff, 0x80000000, 0x80000001, 0xfffffffd,
                               0xfffffffe, 0xffffffff};
    for (unsigned a = 0; a < ARRAY_SIZE(values); a++) {
        for (unsigned b = 0; b < ARRAY_SIZE(values); b++) {
            const int64_t dividend = (int32_t) values[a];
            const int64_t divisor = (int32_t) values[b];
            const uint32_t quotient = divisor ? dividend / divisor : UINT32_MAX;
            const uint32_t remainder = divisor ? dividend % divisor : values[a];
            if (rv_div(dividend, values[b]) != quotient ||
                rv_rem(dividend, values[b]) != remainder)
                return 1;
            for (unsigned rd = 1; rd <= 3; rd++) {
                for (unsigned rem = 0; rem < 2; rem++) {
                    constopt_info_t info = {0};
                    info.is_constant[1] = info.is_constant[2] = true;
                    info.const_val[1] = values[a];
                    info.const_val[2] = values[b];
                    rv_insn_t ir = {.rs1 = 1, .rs2 = 2, .rd = rd};
                    if (rem)
                        constopt_rem(&ir, &info);
                    else
                        constopt_div(&ir, &info);
                    const uint32_t expected = rem ? remainder : quotient;
                    if (ir.opcode != rv_insn_lui ||
                        ir.impl != dispatch_table[rv_insn_lui] ||
                        !info.is_constant[rd] ||
                        info.const_val[rd] != expected ||
                        (uint32_t) ir.imm != expected) {
                        fprintf(stderr, "signed division folding failed\n");
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}
#endif

int main(void)
{
    if (check_shifts() || check_known_operand())
        return 1;
#if RV32_HAS(EXT_C)
    if (check_compressed_known_operand())
        return 1;
#endif
#if RV32_HAS(EXT_A)
    if (check_scw())
        return 1;
#endif
#if RV32_HAS(EXT_M)
    return check_mulhu() || check_unsigned_division() ||
           check_signed_division();
#else
    return 0;
#endif
}
