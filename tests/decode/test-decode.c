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

    /* Reserved and unallocated encodings must stay illegal.  Each case
     * below is a code point the generated decoder has to reject; they are
     * grouped by the rule in the ISA manual that makes them reserved. */

    /* SYSTEM: only the allocated funct3 values and imm code points exist,
     * and ECALL/EBREAK/MRET require rd and rs1 to be zero. */
    if (expect_decode(0x00000073, true, rv_insn_ecall) ||
        expect_decode(0x00100073, true, rv_insn_ebreak) ||
        expect_decode(0x00004073, false, 0) ||
        expect_decode(0x10014073, false, 0) ||
        expect_decode(0x000e0e73, false, 0) ||
        expect_decode(0x00200073, false, 0) ||
        expect_decode(0x20200073, false, 0))
        return 1;

#if RV32_HAS(Zicsr)
    /* A CSR instruction naming a read-only CSR (csr >= 0xc00) is illegal
     * unless rs1 is x0.  That exemption is exact for CSRRS/CSRRC, which
     * skip the write when the source is x0; CSRRW always writes, so
     * 0xc0001073 below is accepted more permissively than the ISA
     * requires.  The decoder has behaved this way since before the
     * generator, and narrowing it is a separate change -- this asserts
     * the current rule so that a decoder rewrite cannot silently drop
     * the read-only check altogether, which is what regressed here. */
    if (expect_decode(0xc00110f3, false, 0) ||
        expect_decode(0xc0001073, true, rv_insn_csrrw) ||
        expect_decode(0x30011073, true, rv_insn_csrrw))
        return 1;
#endif

    /* FENCE has no register fields: bits 24..20 are pred/succ, not rs2, so
     * the RV32E register-range check must not reject a full-barrier fence. */
    if (expect_decode(0x0ff0000f, true, rv_insn_fence) ||
        expect_decode(0x0330000f, true, rv_insn_fence))
        return 1;

#if RV32_HAS(EXT_C)
    /* RVC code points that the spec reserves.  The all-zero word is the
     * canonical illegal instruction and must never decode. */
    if (expect_decode(0x00000000, false, 0) || /* c.addi4spn nzuimm=0 */
        expect_decode(0x00006101, false, 0) || /* c.addi16sp nzimm=0 */
        expect_decode(0x00006081, false, 0) || /* c.lui rd=x1, nzimm=0 */
        expect_decode(0x00008002, false, 0))   /* c.jr rs1=x0 */
        return 1;

    /* On RV32 shamt[5] (bit 12) is reserved for every compressed shift;
     * accepting it would make the interpreter shift a uint32_t by >= 32. */
    if (expect_decode(0x00001082, false, 0) || /* c.slli shamt=32 */
        expect_decode(0x00009001, false, 0) || /* c.srli shamt=32 */
        expect_decode(0x00009401, false, 0))   /* c.srai shamt=32 */
        return 1;

    /* The valid neighbours of those code points still decode, including
     * c.lui with rd=x0, which is a HINT rather than a reserved encoding. */
    if (expect_decode(0x00000082, true, rv_insn_cslli) ||
        expect_decode(0x00008082, true, rv_insn_cjr) ||
        expect_decode(0x00006001, true, rv_insn_cnop))
        return 1;
#endif

#if RV32_HAS(EXT_V)
    /* RVV fields the spec fixes to one value: vadc/vsbc carry a mask so
     * vm must be 0, while vmv.x.s / vmv.s.x are unmasked so vm must be 1
     * (and vmv.s.x reserves vs2). */
    if (expect_decode(0x42000057, false, 0) ||
        expect_decode(0x40000057, true, rv_insn_vadc_vvm) ||
        expect_decode(0x4a000057, false, 0) ||
        expect_decode(0x48000057, true, rv_insn_vsbc_vvm) ||
        expect_decode(0x40002057, false, 0) ||
        expect_decode(0x42002057, true, rv_insn_vmv_x_s) ||
        expect_decode(0x40006057, false, 0) ||
        expect_decode(0x42006057, true, rv_insn_vmv_s_x) ||
        expect_decode(0x41f86dd7, false, 0))
        return 1;

    /* Whole-register stores are EEW=8 and unmasked only.  Without those
     * two fixed fields their pattern would also swallow scalar fsw. */
    if (expect_decode(0x02800027, true, rv_insn_vs1r_v) ||
        expect_decode(0x00800027, false, 0) ||
        expect_decode(0x0080a027, true, rv_insn_fsw))
        return 1;
#endif

    return 0;
}
