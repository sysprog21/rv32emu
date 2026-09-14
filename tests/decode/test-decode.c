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
