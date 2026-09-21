# Instruction Decoder Generator

RISC-V instructions are described in a human-readable format in
`src/instructions.in`, and `scripts/gen-decoder.py` converts this descriptor
into the corresponding C implementation. Adding a new instruction or extension
requires editing only `src/instructions.in`; `make` regenerates `src/decode.c`
automatically.

This document describes the format of the ISA descriptor file and how to extend
it with new instructions.

## Supported Extensions

The RISC-V ISA organizes optional functionality into named extensions.
rv32emu supports the following extensions, each mapped to a compile-time
`RV32_HAS()` guard and an `@extension` tag in `src/instructions.in`.

**Base integer ISA**

| Extension | Tag in instructions.in | Description |
|-----------|------------------------|-------------|
| I | _(no tag, always included)_ | Base 32-bit integer instruction set |

**Standard unprivileged extensions**

| Extension | Tag in instructions.in | Description |
|-----------|------------------------|-------------|
| M | `EXT_M` | Integer multiplication and division |
| A | `EXT_A` | Atomic memory operations |
| F | `EXT_F` | Single-precision floating-point |
| C | `EXT_C` | Compressed (16-bit) instructions |
| V | `EXT_V` | Vector extension (RVV v1.0) |

**Standard unprivileged Z-extensions**

| Extension | Tag in instructions.in | Description |
|-----------|------------------------|-------------|
| Zicsr     | `Zicsr`    | Control and status register (CSR) instructions |
| Zifencei  | `Zifencei` | Instruction-fetch fence |
| Zba       | `Zba`      | Address generation bit manipulation |
| Zbb       | `Zbb`      | Basic bit manipulation |
| Zbc       | `Zbc`      | Carry-less multiplication |
| Zbs       | `Zbs`      | Single-bit instructions |

Zba, Zbb, Zbc, and Zbs are the ratified subsets of the B (bit-manipulation)
extension.

**Privileged instructions**

| Group | Tag in instructions.in | Description |
|-------|------------------------|-------------|
| Machine mode | _(no tag, always included)_ | `ecall`, `ebreak`, `wfi`, `mret`, `sfence.vma` |
| Supervisor mode | `SYSTEM` | `sret`, guarded by the emulator's full-system build |

`SYSTEM` is not an ISA extension name; it is the rv32emu build option that
selects full-system emulation. The `@extension` tag is passed through to
`RV32_HAS()` unchanged, so any build option spelled that way can gate a
group of instructions.

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
| `arg=value` | A fixed-position field must equal `value` | `rd=0` |

Values can be decimal (`3`), hexadecimal (`0x1C`), or binary (`0b0010000`).

The `arg=value` form is shorthand for the field's bit range, accepted
for `rd` (`11..7`), `rs1` (`19..15`), `rs2` (`24..20`), `rs3` (`31..27`)
and `rm` (`14..12`), so `rd=0` and `11..7=0` mean the same thing.

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
| `crs1n0` | `[11:7]` | `ir->rs1` | as `crs1`, but rs1 = x0 is reserved |
| `crs1rd` | `[11:7]` | `ir->rd`, `ir->rs1 = ir->rd` | 5-bit, rd and rs1 share field |
| `crs2` | `[6:2]` | `ir->rs2` | 5-bit, full range |
| `crdq` | `[4:2]` | `ir->rd` | 3-bit, maps to x8-x15 (`\| 0x08`) |
| `crs1q` | `[9:7]` | `ir->rs1` | 3-bit, maps to x8-x15 |
| `crs2q` | `[4:2]` | `ir->rs2` | 3-bit, maps to x8-x15 |

### RVC Immediate Operands

| Operand | Used by | Bits | Signed | Alignment |
|---------|---------|------|--------|-----------|
| `cnzuimm4spn` | c.addi4spn | `[12:5]` | no | 4-byte |
| `cimmw` | c.lw, c.sw, c.flw, c.fsw | `[12:10,6:5]` | no | 4-byte |
| `cimmi` | c.li, c.addi, c.andi | `[12,6:2]` | yes | — |
| `cnzimmi` | c.lui | `[12,6:2]` | yes | — |
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

## Adding a New RVV Instruction

Vector (RVV v1.0) instructions use a different set of operand names and
bit-field conventions from scalar instructions.

### RVV Operand Names

| Operand | Bits | Decoded field | Notes |
|---------|------|---------------|-------|
| `vd` | `[11:7]` | `ir->vd` | Vector destination register |
| `vs1` | `[19:15]` | `ir->vs1` | Vector source 1 |
| `vs2` | `[24:20]` | `ir->vs2` | Vector source 2 |
| `vs3` | `[11:7]` | `ir->vs3` | Vector source 3 (stores, same position as `rd`) |
| `vm` | `[25]` | `ir->vm` | Vector mask bit |
| `rd` | `[11:7]` | `ir->rd` | Scalar destination (mask reductions: vmv.x.s, vcpop.m …) |
| `rs1` | `[19:15]` | `ir->rs1` | Scalar integer or float source |
| `simm5` | `[19:15]` | `ir->imm` | 5-bit sign-extended immediate (OPIVI) |
| `zimm5` | `[19:15]` | `ir->rs1` | 5-bit unsigned immediate (vsetivli avl) |
| `zimm10` | `[29:20]` | `ir->zimm` | 10-bit unsigned immediate (vsetivli vtypei) |
| `zimm11` | `[30:20]` | `ir->zimm` | 11-bit unsigned immediate (vsetvli vtypei) |
| `veew` | `[14:12]` | `ir->eew` | Effective element width (V-load/store only) |

### RVV Encoding Conventions

**OP-V instructions** (`opcode[6:2]=0x15`) use a 6-bit `funct6` field
(`bits[31:26]`) instead of the 7-bit `funct7` used by scalar OP/OP-32.
The funct3 field (`bits[14:12]`) selects the operation class:

| funct3 | Class | Typical operands |
|--------|-------|-----------------|
| 0 (OPIVV) | Integer vector×vector | `vd vs2 vs1 vm` |
| 1 (OPFVV) | FP vector×vector | `vd vs2 vs1 vm` |
| 2 (OPMVV) | Mask/integer vector×vector | `vd vs2 vs1 vm` |
| 3 (OPIVI) | Integer vector×immediate | `vd vs2 simm5 vm` |
| 4 (OPIVX) | Integer vector×scalar | `vd vs2 rs1 vm` |
| 5 (OPFVF) | FP vector×scalar | `vd vs2 rs1 vm` |
| 6 (OPMVX) | Mask/integer vector×scalar | `vd vs2 rs1 vm` |

**Constraint ordering**: the generator reorders constraints so that
`1..0=` becomes the root switch and `6..2=` sits directly below it,
whatever order a line is written in; the remaining constraints keep
their written order, which decides the nesting of the inner switches.
Writing lines as `... 6..2=0x15 1..0=3` keeps them consistent with the
rest of the file and with riscv-opcodes.

**V-load/store instructions** (`opcode=LOAD-FP` / `STORE-FP`) add the
`mop` (`bits[27:26]`), `mew` (`bit[28]`), and `nf` (`bits[31:29]`)
fields; the EEW is encoded in `bits[14:12]`.

### Special Dispatch Patterns

Some OP-V groups dispatch on a sub-field in addition to funct6/funct3:

- **VWXUNARY0** (`funct6=0x10, funct3=2`): dispatched by `bits[19:15]`
  (the `vs1` slot).  Use the `19..15=N` constraint *before* the funct6
  and funct3 constraints so it becomes an inner switch.

  ```
  vmv_x_s   rd vs2 vm   19..15=0   31..26=16  14..12=2  6..2=0x15 1..0=3
  vcpop_m   rd vs2 vm   19..15=16  31..26=16  14..12=2  6..2=0x15 1..0=3
  ```

- **VMUNARY0** (`funct6=0x14, funct3=2`): same pattern as VWXUNARY0.

- **vmv_v_v / vmerge_vvm** (`funct6=0x17, funct3=0`): differentiated by
  the `vm` bit (`bit[25]`).  Add a `25=1` / `25=0` constraint.

  ```
  vmv_v_v    vd vs2 vs1 vm  25=1  31..26=23  14..12=0  6..2=0x15 1..0=3
  vmerge_vvm vd vs2 vs1 vm  25=0  31..26=23  14..12=0  6..2=0x15 1..0=3
  ```

### Example: Adding a Hypothetical RVV Instruction

```
@extension EXT_V
# vadd.vv — vector-vector add, funct6=0x00, funct3=0 (OPIVV)
vadd_vv   vd vs2 vs1 vm  31..26=0  14..12=0  6..2=0x15 1..0=3
```

Run `make` to regenerate `src/decode.c`.

## Reserved Code Points

Most reserved encodings are expressed directly as bit constraints: for
example `12=0` on the compressed shifts rejects `shamt[5] = 1`, which
RV32 reserves.  Prefer that form, because it needs no generator change
and it keeps `instructions.in` the single description of the encoding.

A few reserved code points cannot be written as a fixed bit pattern,
because the field is scattered across the word and the reserved value
is "all zero" rather than a constant.  `c.addi4spn` with `nzuimm = 0`
(which is the all-zero word, the canonical illegal instruction),
`c.addi16sp` and `c.lui` with `nzimm = 0`, and `c.jr` with `rs1 = x0`
are the cases in RV32.

These live in `_RVC_OPERAND_GUARDS` in `gen-decoder.py`, which maps an
*operand* name to the C condition that makes the encoding illegal, and
the generator emits the guard after the operand decode.  Keying on the
operand rather than the instruction keeps the rule where the field is:
the descriptor names the nonzero form explicitly, following the
riscv-opcodes `nz`/`n0` convention (`cnzuimm4spn`, `cnzimm16sp`,
`cnzimmi`, `crs1n0`).  An instruction that reuses one of those operands
inherits the guard with no generator change; a genuinely new rule needs
a new operand name and one entry in that table.

## Descriptor Validation

The generator rejects a descriptor it cannot represent faithfully,
with the offending `instructions.in` line in the message:

- a malformed or out-of-range constraint (`14..12=9` does not fit in
  three bits; `14..12=x` is not an integer),
- a line missing its `1..0=` quadrant, or a 32-bit line missing its
  `6..2=` opcode,
- the same bit range constrained twice on one line,
- a repeated mnemonic — one name maps to one `rv_insn_*` opcode, so a
  duplicate means two encodings were given the same opcode,
- two instructions whose encodings collide.

It also checks that the number of leaves in the decision tree equals
the number of lines parsed, so a line can never be silently dropped.

## Verification

```bash
# Check that every instruction in instructions.in is reachable in the
# decision tree:
python3 scripts/verify-tree.py src/instructions.in

# Rebuild and run basic tests:
make clean && make defconfig && make
build/rv32emu build/hello.elf
build/rv32emu build/coro.elf
```

## Build Integration

The `src/decode.c` rule in the Makefile regenerates the decoder when
`src/instructions.in` or `scripts/gen-decoder.py` changes.

The output is written to a temporary file and moved into place only
after the generator and the formatter both succeed, so a failed run
never leaves a truncated `src/decode.c` behind.  Formatting uses the
clang-format version the project pins in `.ci/llvm-version` and is
skipped when that version is not installed; `make format` and the CI
format check remain the enforcement points.

`src/decode.c` is committed to the repository so that a checkout can be
built without a clang-format dependency, and so that decoder changes are
reviewable as a diff.
