# Instruction Decoder Generator

The original `src/decode.c` was a hand-written 2000+ line C file containing
nested switch statements for every supported RISC-V instruction. While
functional, maintenance and verification were not straightforward — adding a
new instruction required understanding the full switch structure and manually
writing bit-extraction logic.

The decoder generator replaces this approach: RISC-V instructions are described
in a human-readable format in `src/instructions.in`, and `scripts/gen-decoder.py`
converts this descriptor into the corresponding C implementation. This makes it
possible to add new instructions or extensions by editing a single line in the
descriptor file, rather than modifying generated C code by hand.

This document describes the format of the ISA descriptor file and how to extend
it with new instructions.

## Overview

The generated decoder has two responsibilities for each instruction:

1. **Identify** the instruction from its bit pattern (decision tree).
2. **Extract** the operands (registers and immediates) into `rv_insn_t`.

For 32-bit instructions, identification uses nested switch statements on
the opcode, funct3, and funct7 fields. Operand extraction is handled by
type decoders (`decode_itype`, `decode_rtype`, etc.) inferred from the
operand names.

For 16-bit compressed (RVC) instructions, identification follows the same
switch-based decision tree. Operand extraction is per-instruction because
each RVC format has a unique bit layout for immediates.

## instructions.in Format

Each instruction is described on a single line:

```
<name> <operands...> <constraints...>
```

- **name**: the instruction mnemonic (e.g. `addi`, `clw`). Must match
  the `rv_insn_<name>` enum in `decode.h`.
- **operands**: field names that tell the generator which operands to
  decode (e.g. `rd`, `rs1`, `imm12`, `crdq`, `cimmw`).
- **constraints**: bit-range/value pairs that identify the instruction
  (e.g. `14..12=0`, `6..2=0x04`, `1..0=3`).

### Constraints

Constraints specify fixed bit patterns used to build the decision tree.

| Syntax | Meaning | Example |
|--------|---------|---------|
| `hi..lo=value` | Bits `[hi:lo]` must equal `value` | `14..12=0` |
| `bit=value` | Single bit must equal `value` | `12=1` |

Values can be decimal (`3`), hexadecimal (`0x1C`), or binary (`0b0010000`).

### Extension Guards

Lines starting with `@extension` set a compile-time guard for all
subsequent instructions until the next `@extension` directive or end
of file:

```
@extension EXT_M
mul     rd rs1 rs2 31..25=1 14..12=0 6..2=0x0C 1..0=3
```

This generates `#if RV32_HAS(EXT_M)` / `#endif` around the instruction
in the output.

### Comments and Blank Lines

Lines starting with `#` and empty lines are ignored.

## Operand Reference

### 32-bit Operands

These operand names determine which type decoder is called for the
entire opcode group:

| Operand | Type | Decoded fields |
|---------|------|----------------|
| `rd` | — | `ir->rd` |
| `rs1` | — | `ir->rs1` |
| `rs2` | — | `ir->rs2` |
| `rs3` | R4-type | `ir->rs3`, `ir->rm` |
| `imm12` | I-type | `ir->imm` (sign-extended), `ir->rs1`, `ir->rd` |
| `imm20` | U-type | `ir->imm` (upper 20 bits), `ir->rd` |
| `jimm20` | J-type | `ir->imm` (sign-extended), `ir->rd` |
| `bimm12hi`, `bimm12lo` | B-type | `ir->imm` (sign-extended), `ir->rs1`, `ir->rs2` |
| `imm12hi`, `imm12lo` | S-type | `ir->imm` (sign-extended), `ir->rs1`, `ir->rs2` |
| `shamtw`, `shamt` | I-type | `ir->imm`, `ir->rs1`, `ir->rd` |

### RVC Register Operands

| Operand | Bits | Decoded field | Notes |
|---------|------|---------------|-------|
| `crd` | `[11:7]` | `ir->rd` | 5-bit, full range x0-x31 |
| `crs1` | `[11:7]` | `ir->rs1` | 5-bit, full range |
| `crs1rd` | `[11:7]` | `ir->rd`, `ir->rs1 = ir->rd` | 5-bit, rd and rs1 share field |
| `crs2` | `[6:2]` | `ir->rs2` | 5-bit, full range |
| `crdq` | `[4:2]` | `ir->rd` | 3-bit, maps to x8-x15 (`\| 0x08`) |
| `crs1q` | `[9:7]` | `ir->rs1` | 3-bit, maps to x8-x15 |
| `crs2q` | `[4:2]` | `ir->rs2` | 3-bit, maps to x8-x15 |

### RVC Immediate Operands

| Operand | Used by | Bits | Signed | Alignment |
|---------|---------|------|--------|-----------|
| `cimm4spn` | c.addi4spn | `[12:5]` | no | 4-byte |
| `cimmw` | c.lw, c.sw, c.flw, c.fsw | `[12:10,6:5]` | no | 4-byte |
| `cimmi` | c.li, c.andi | `[12,6:2]` | yes | — |
| `cnzimmi` | c.addi, c.lui | `[12,6:2]` | yes | — |
| `cshamt` | c.slli, c.srli, c.srai | `[12,6:2]` | no | — |
| `cimmb` | c.beqz, c.bnez | `[12:10,6:2]` | yes | 2-byte |
| `cimmj` | c.j, c.jal | `[12:2]` | yes | 2-byte |
| `cimmlwsp` | c.lwsp, c.flwsp | `[12,6:2]` | no | 4-byte |
| `cimmswsp` | c.swsp, c.fswsp | `[12:7]` | no | 4-byte |
| `cnzimm16sp` | c.addi16sp | `[12,6:2]` | yes | 16-byte |

## Adding a New 32-bit Instruction

Suppose a new extension adds an instruction `foo rd, rs1, rs2` with
`funct7=0b0000110`, `funct3=1`, in the OP group (`opcode[6:2]=0x0C`):

1. Add an `@extension` guard (if needed) and the instruction line:

   ```
   @extension EXT_FOO
   foo     rd rs1 rs2 31..25=0b0000110 14..12=1 6..2=0x0C 1..0=3
   ```

2. Run `make` — the Makefile rule regenerates `src/decode.c`
   automatically.

3. Verify the decision tree:

   ```
   python3 scripts/verify-tree.py src/instructions.in
   ```

No changes to `gen-decoder.py` are needed. The generator infers R-type
decoding from the `rd rs1 rs2` operands.

## Adding a New RVC Instruction

Adding a compressed instruction that uses **existing** operand types
(e.g. `crdq`, `cimmw`) works the same way — just add a line to
`instructions.in`.

If the instruction requires a **new immediate format** (a bit-scramble
layout not covered by the existing operand names), you also need to:

1. Choose a new operand name (e.g. `cimmnew`).
2. Add its bit-extraction logic to the `_RVC_OPERAND_DECODERS` dict
   in `gen-decoder.py`.
3. If the instruction has implicit registers (e.g. `rs1` is always `sp`),
   add an entry to `_RVC_INSN_IMPLICIT`.

## Verification

```bash
# Check that every instruction in instructions.in is reachable
# in the decision tree (should print 161/161 PASSED):
python3 scripts/verify-tree.py src/instructions.in

# Rebuild and run basic tests:
make clean && make defconfig && make
build/rv32emu build/hello.elf
build/rv32emu build/coro.elf
```

## Build Integration

The Makefile contains a rule to regenerate `src/decode.c` when
`src/instructions.in` or `scripts/gen-decoder.py` is modified:

```makefile
src/decode.c: src/instructions.in scripts/gen-decoder.py
	python3 scripts/gen-decoder.py src/instructions.in > $@
```

`src/decode.c` is committed to the repository so that a checkout can be
built without running the generator. The rule ensures the file stays in
sync during development.