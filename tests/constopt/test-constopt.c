#include <stdio.h>

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

#define CONSTOPT(inst, code)                                         \
    static void UNUSED constopt_##inst(rv_insn_t *ir UNUSED,         \
                                       constopt_info_t *info UNUSED) \
    {                                                                \
        code;                                                        \
    }
#include "rv32_constopt.c"
#undef CONSTOPT

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

#endif

int main(void)
{
#if RV32_HAS(EXT_A)
    if (check_scw())
        return 1;
#endif
#if RV32_HAS(EXT_M)
    return check_mulhu();
#else
    return 0;
#endif
}
