#include <stdio.h>
#include <string.h>

#include "decode.h"

static int expect_decode(uint32_t insn, bool expected, uint16_t opcode)
{
    rv_insn_t ir;
    memset(&ir, 0, sizeof(ir));

    if (rv_decode(&ir, insn) != expected) {
        fprintf(stderr, "unexpected decode result for 0x%08x\n", insn);
        return 1;
    }
    if (expected && ir.opcode != opcode) {
        fprintf(stderr, "unexpected opcode for 0x%08x\n", insn);
        return 1;
    }
    return 0;
}

int main(void)
{
#if RV32_HAS(JIT)
    branch_history_table_t bt = {0};
    /* Keep both observed targets inside the table: a direct-mapped index is
     * taken from PC bits, so pc and pc + 4 must not straddle the wrap.
     */
    const uint32_t pc = 0x1000;
    const uint32_t idx = (pc >> 2) & (HISTORY_SIZE - 1);
    const uint32_t next_idx = ((pc + 4) >> 2) & (HISTORY_SIZE - 1);
    uint32_t satp = 1;
    if (next_idx != idx + 1) {
        fprintf(stderr, "BHT test PC must not wrap the history table\n");
        return 1;
    }
    for (unsigned i = 0; i < 512; i++)
        bht_record_target(&bt, pc, satp);
    if (bt.PC[idx] != pc || bt.times[idx] != 512) {
        fprintf(stderr, "BHT warm-up hits must accumulate\n");
        return 1;
    }
    for (unsigned i = 0; i < 7; i++)
        bht_record_target(&bt, pc + 4, satp);
    if (bt.times[idx] != 512 || bt.times[next_idx] != 7) {
        fprintf(stderr, "BHT targets must keep independent counts\n");
        return 1;
    }
    bht_record_target(&bt, pc + 4 * HISTORY_SIZE, satp);
    if (bt.PC[idx] != pc + 4 * HISTORY_SIZE || bt.times[idx] != 1) {
        fprintf(stderr, "BHT collision must replace the old observation\n");
        return 1;
    }
#if RV32_HAS(SYSTEM)
    satp = 2;
    bht_record_target(&bt, pc + 4 * HISTORY_SIZE, satp);
    if (bt.satp[idx] != satp || bt.times[idx] != 1) {
        fprintf(stderr, "BHT context change must reset the count\n");
        return 1;
    }
#endif
    bt.times[idx] = UINT32_MAX;
    bht_record_target(&bt, bt.PC[idx], satp);
    if (bt.times[idx] != UINT32_MAX) {
        fprintf(stderr, "BHT counts must not wrap\n");
        return 1;
    }
#endif
    /* SLLI accepts funct7=0 only.  Test both a normal destination and x0,
     * which must not bypass encoding validation as a NOP. */
    if (expect_decode(0x00109093, true, rv_insn_slli) ||
        expect_decode(0x04009093, false, 0) ||
        expect_decode(0x04001013, false, 0))
        return 1;

    /* On RV32, shamt[5] set is reserved for every immediate shift. */
    if (expect_decode(0x02009093, false, 0) ||
        expect_decode(0x0200d093, false, 0) ||
        expect_decode(0x4200d093, false, 0))
        return 1;

    /* SRLI and SRAI accept only funct7=0 and funct7=0x20 respectively. */
    if (expect_decode(0x0010d093, true, rv_insn_srli) ||
        expect_decode(0x4010d093, true, rv_insn_srai) ||
        expect_decode(0x0410d093, false, 0) ||
        expect_decode(0x0410d013, false, 0))
        return 1;

#if RV32_HAS(Zbb)
    /* Valid extension encodings still take the shared x0 NOP path, and decode
     * to their own operations for any other destination. */
    if (expect_decode(0x60001013, true, rv_insn_nop) ||
        expect_decode(0x60009093, true, rv_insn_clz) ||
        expect_decode(0x60109093, true, rv_insn_ctz) ||
        expect_decode(0x60209093, true, rv_insn_cpop) ||
        expect_decode(0x60409093, true, rv_insn_sextb) ||
        expect_decode(0x60509093, true, rv_insn_sexth) ||
        expect_decode(0x6010d093, true, rv_insn_rori) ||
        expect_decode(0x2870d093, true, rv_insn_orcb) ||
        expect_decode(0x6980d093, true, rv_insn_rev8))
        return 1;
#endif
#if RV32_HAS(Zbs)
    if (expect_decode(0x48001013, true, rv_insn_nop) ||
        expect_decode(0x48109093, true, rv_insn_bclri) ||
        expect_decode(0x28109093, true, rv_insn_bseti) ||
        expect_decode(0x68109093, true, rv_insn_binvi) ||
        expect_decode(0x4810d093, true, rv_insn_bexti))
        return 1;
#endif

    return 0;
}
