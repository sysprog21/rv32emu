#!/usr/bin/env python3
"""Generate RISC-V instruction decoder from ISA descriptor.

Reads src/instructions.in and generates src/decode.c.

Usage:
    python3 scripts/gen-decoder.py src/instructions.in
"""

import sys
from dataclasses import dataclass, field


@dataclass
class Instruction:
    """One parsed instruction from instructions.in."""

    name: str
    operands: list[str] = field(default_factory=list)
    constraints: list[tuple[int, int, int]] = field(
        default_factory=list
    )  # [(hi, lo, value), ...]
    extension: str | None = None  # None = always enabled


def infer_insn_type(operands: list[str]) -> str:
    """Infer instruction type from operand names.

    The type determines which decode_*type() function to call,
    which extracts the right fields (rd, rs1, rs2, imm) from
    the raw instruction word into the rv_insn_t struct.

    Returns one of: rtype, itype, stype, btype, utype, jtype,
                     r4type, or empty string for no decode needed.
    """
    names = set(operands)
    if "bimm12hi" in names:
        return "btype"
    if "imm12hi" in names:
        return "stype"
    if "jimm20" in names:
        return "jtype"
    if "imm20" in names:
        return "utype"
    if "rs3" in names:
        return "r4type"
    if "imm12" in names:
        return "itype"
    if "shamtw" in names or "shamt" in names:
        return "itype"
    if "rs2" in names or "rs1" in names:
        return "rtype"
    if "rd" in names:
        return "itype"
    # Compressed or no-operand instructions
    return ""


def parse_constraint(token: str) -> tuple[int, int, int]:
    """Parse a bit constraint like '31..25=0' or '6..2=0x0C'.

    Returns (hi, lo, value) tuple.

    Examples:
        '31..25=0'         -> (31, 25, 0)
        '14..12=0'         -> (14, 12, 0)
        '6..2=0x0C'        -> (6, 2, 12)
        '1..0=3'           -> (1, 0, 3)
        '12=1'             -> (12, 12, 1)
        '31..25=0b0010000' -> (31, 25, 16)
    """
    bit_range, value_str = token.split("=")
    value = int(value_str, 0)  # auto-detect base (dec/hex/bin)

    if ".." in bit_range:
        hi, lo = bit_range.split("..")
        return (int(hi), int(lo), value)
    else:
        bit = int(bit_range)
        return (bit, bit, value)


def parse_instructions(filepath: str) -> list[Instruction]:
    """Parse an instructions.in file into a list of Instructions.

    Handles:
        - Comment lines (# ...)
        - Empty lines
        - @extension directives
        - Instruction lines: <name> <operands...> <constraints...>
    """
    instructions = []
    current_extension = None

    with open(filepath) as f:
        for line in f:
            line = line.strip()

            # Skip comments and empty lines
            if not line or line.startswith("#"):
                continue

            # Handle @extension directive
            if line.startswith("@extension"):
                current_extension = line.split()[1]
                continue

            # Parse instruction line
            tokens = line.split()
            name = tokens[0]
            operands = []
            constraints = []

            for token in tokens[1:]:
                if "=" in token:
                    constraints.append(parse_constraint(token))
                else:
                    operands.append(token)

            instructions.append(
                Instruction(
                    name=name,
                    operands=operands,
                    constraints=constraints,
                    extension=current_extension,
                )
            )

    return instructions


# Sentinel key used inside a value-level subtree dict to represent the
# "default" (least-specific) instruction when more specific sibling
# constraints also exist.  Using a string avoids collision with the
# integer values that normal constraints produce.
_DEFAULT_KEY = "default"


def merge_trees(a: dict, b: dict, path: str = "") -> dict:
    """Merge two decision trees recursively.

    Both trees are nested dicts. When two branches lead to the
    same bit-range key and same value, they are merged deeper.

    NEW: When one side is an Instruction leaf (less specific) and
    the other side is a dict with further constraints (more specific),
    the leaf becomes the _DEFAULT_KEY entry of the merged subtree.
    This models "instruction X matches here unless a more-specific
    sibling constraint matches first", which is required for RVC
    instructions like c.add (no rs2 constraint) vs c.jalr (rs2=0).

    Conflicts (two different instructions at the same leaf with no
    further disambiguation) still raise an error.
    """
    for key in b:
        if key in a:
            if isinstance(a[key], dict) and isinstance(b[key], dict):
                merge_trees(a[key], b[key], f"{path}.{key}")
            elif a[key] == b[key]:
                pass  # same leaf, no conflict
            elif isinstance(a[key], Instruction) and isinstance(b[key], dict):
                # a has a less-specific (default) instruction;
                # b has more-specific cases.  Wrap a's instruction as
                # the default and merge b's cases in.
                new_dict: dict = {_DEFAULT_KEY: a[key]}
                merge_trees(new_dict, b[key], f"{path}.{key}")
                a[key] = new_dict
            elif isinstance(a[key], dict) and isinstance(b[key], Instruction):
                # a already has specific cases; b is less specific → default.
                if _DEFAULT_KEY in a[key]:
                    raise ValueError(
                        f"Multiple defaults at {path}.{key}: "
                        f"{a[key][_DEFAULT_KEY].name} vs {b[key].name}"
                    )
                a[key][_DEFAULT_KEY] = b[key]
            else:
                raise ValueError(
                    f"Conflict at {path}.{key}: " f"{a[key]} vs {b[key]}"
                )
        else:
            a[key] = b[key]
    return a


def build_decision_tree(
    instructions: list[Instruction],
) -> dict:
    """Build a decision tree from parsed instructions.

    Each instruction's constraints form a path from root to leaf.
    All paths are merged into a single tree.

    The tree structure is:
        { (hi, lo): { value: subtree_or_leaf, ... }, ... }

    where a leaf is the Instruction object itself, and the special
    _DEFAULT_KEY entry (string) holds the default Instruction for
    that value-level dict when more-specific siblings exist.
    """
    tree = {}

    for insn in instructions:
        # Build a single-path tree for this instruction,
        # starting from the leaf and wrapping outward.
        node = insn  # leaf node = the Instruction itself

        for hi, lo, value in insn.constraints:
            node = {(hi, lo): {value: node}}

        # Merge into the main tree
        merge_trees(tree, node)

    return tree


def print_tree(tree: dict, indent: int = 0) -> None:
    """Print a decision tree for debugging."""
    prefix = "  " * indent

    for key, subtree in sorted(
        ((k, v) for k, v in tree.items() if isinstance(k, tuple)),
        key=lambda x: x[0],
    ):
        hi, lo = key
        if hi == lo:
            label = f"bit {hi}"
        else:
            label = f"bits {hi}..{lo}"

        print(f"{prefix}switch ({label}):")

        for value, branch in sorted(
            ((v, b) for v, b in subtree.items() if v != _DEFAULT_KEY),
            key=lambda x: x[0],
        ):
            if isinstance(branch, Instruction):
                ext = f"  [{branch.extension}]" if branch.extension else ""
                print(f"{prefix}  case {value}: " f"-> {branch.name}{ext}")
            elif isinstance(branch, dict):
                print(f"{prefix}  case {value}:")
                print_tree(branch, indent + 2)
            else:
                print(f"{prefix}  case {value}: -> {branch}")

        if _DEFAULT_KEY in subtree:
            d = subtree[_DEFAULT_KEY]
            ext = f"  [{d.extension}]" if d.extension else ""
            print(f"{prefix}  default: -> {d.name}{ext}")


def bits_expr(hi: int, lo: int) -> str:
    """Generate C expression to extract bits [hi:lo] from insn.

    Examples:
        bits_expr(6, 2)   -> '(insn >> 2) & 0x1f'
        bits_expr(14, 12) -> '(insn >> 12) & 0x7'
        bits_expr(12, 12) -> '(insn >> 12) & 0x1'
    """
    width = hi - lo + 1
    mask = (1 << width) - 1
    return f"(insn >> {lo}) & {hex(mask)}"


def generate_c_switch(
    tree: dict, indent: int = 1, rvc: bool = False
) -> list[str]:
    """Generate C switch/case code from a decision tree.

    Returns a list of C code lines (without newlines).

    The tree may have multiple (hi,lo) keys at the same level.
    Each becomes a separate switch statement.  When a specific path
    matches, it returns early.  If it doesn't match, execution falls
    through to the next switch.

    When a value-level dict contains a _DEFAULT_KEY entry, that
    Instruction is emitted as the C ``default:`` case rather than
    the bare ``default: break``.  This handles instructions like
    c.add that have no rs2 constraint (default) vs c.jalr (rs2=0).

    When rvc=True, operand decode lines (register extraction,
    immediate reassembly) are emitted before the opcode
    assignment for each leaf instruction.
    """
    lines = []
    pad = "    " * indent

    # Only iterate over bit-range tuple keys (skip _DEFAULT_KEY etc.)
    bit_keys = sorted(
        ((k, v) for k, v in tree.items() if isinstance(k, tuple)),
        key=lambda x: x[0],
    )

    for key, subtree in bit_keys:
        hi, lo = key

        lines.append(f"{pad}switch ({bits_expr(hi, lo)}) {{")

        for value, branch in sorted(
            ((v, b) for v, b in subtree.items() if v != _DEFAULT_KEY),
            key=lambda x: x[0],
        ):
            if isinstance(branch, Instruction):
                ext_open, ext_close = _ext_guard(branch.extension, pad)
                if ext_open:
                    lines.append(ext_open)
                lines.append(f"{pad}case {_hex_value(value, hi - lo + 1)}:")
                if rvc:
                    lines.extend(_rvc_operand_lines(branch, pad))
                lines.append(
                    f"{pad}    ir->opcode = " f"rv_insn_{branch.name};"
                )
                lines.append(f"{pad}    return true;")
                if ext_close:
                    lines.append(ext_close)
            elif isinstance(branch, dict):
                lines.append(f"{pad}case {_hex_value(value, hi - lo + 1)}:")
                lines.extend(generate_c_switch(branch, indent + 1, rvc=rvc))
                lines.append(f"{pad}    break;")

        # --- default case ---
        lines.append(f"{pad}default:")
        if _DEFAULT_KEY in subtree:
            # Emit the less-specific instruction as the default handler.
            default_insn = subtree[_DEFAULT_KEY]
            ext_open, ext_close = _ext_guard(default_insn.extension, pad)
            if ext_open:
                lines.append(ext_open)
            if rvc:
                lines.extend(_rvc_operand_lines(default_insn, pad))
            lines.append(f"{pad}    ir->opcode = rv_insn_{default_insn.name};")
            lines.append(f"{pad}    return true;")
            if ext_close:
                lines.append(ext_close)
        else:
            lines.append(f"{pad}    break;")

        lines.append(f"{pad}}}")

    return lines


def _hex_value(value: int, width_bits: int) -> str:
    """Format a value for use in a C case label.

    Small values (< 8) stay decimal for readability.
    Larger values use hex with appropriate width.
    """
    if value < 8:
        return str(value)
    return hex(value)


def _ext_guard(
    extension: str | None, pad: str
) -> tuple[str | None, str | None]:
    """Generate #if / #endif guards for an extension.

    Returns (open_line, close_line) or (None, None).
    """
    if extension is None:
        return (None, None)
    return (
        f"#if RV32_HAS({extension})",
        f"#endif /* RV32_HAS({extension}) */",
    )


def generate_c_code(instructions: list[Instruction], tree: dict) -> str:
    """Generate the complete decode.c from instructions and tree."""
    lines = []

    # Split tree into 32-bit (1..0=3) and 16-bit sub-trees
    root = tree.get((1, 0), {})
    tree_32 = root.get(3, {})
    tree_16_q0 = root.get(0, {})
    tree_16_q1 = root.get(1, {})
    tree_16_q2 = root.get(2, {})

    # Collect 32-bit opcode groups from bits 6..2
    groups_32 = {}
    if (6, 2) in tree_32:
        for val, subtree in tree_32[(6, 2)].items():
            groups_32[val] = subtree

    # --- File header ---
    lines.append("/*")
    lines.append(
        " * rv32emu is freely redistributable under the "
        "MIT License. See the file"
    )
    lines.append(
        ' * "LICENSE" for information on usage and '
        "redistribution of this file."
    )
    lines.append(" */")
    lines.append("")
    lines.append(
        "/* AUTO-GENERATED by gen-decoder.py "
        "from instructions.in — DO NOT EDIT */"
    )
    lines.append("")
    lines.append("#include <assert.h>")
    lines.append("#include <stdlib.h>")
    lines.append("")
    lines.append('#include "decode.h"')
    lines.append('#include "riscv_private.h"')
    lines.append("")

    # --- Bit-field helpers (fixed templates) ---
    lines.append(_HELPERS_TEMPLATE)

    # --- Per-group decode functions for 32-bit ---
    group_names = _build_group_name_map()
    for opcode_val in sorted(groups_32.keys()):
        gname = group_names.get(opcode_val, f"group_{opcode_val}")
        subtree = groups_32[opcode_val]
        insn_type = _infer_group_type(subtree)
        if gname in _EXT_F_GROUPS:
            lines.append("#if RV32_HAS(EXT_F)")
        lines.extend(_generate_group_func(gname, subtree, insn_type))
        if gname in _EXT_F_GROUPS:
            lines.append(f"#else /* !RV32_HAS(EXT_F) */")
            lines.append(f"#define op_{gname} op_unimp")
            lines.append(f"#endif /* RV32_HAS(EXT_F) */")
        lines.append("")

    # --- RVC decode functions for 16-bit ---
    lines.append("#if RV32_HAS(EXT_C)")
    lines.append("")
    lines.append(_RVC_HELPERS_TEMPLATE)
    for qnum, qtree in [
        (0, tree_16_q0),
        (1, tree_16_q1),
        (2, tree_16_q2),
    ]:
        if qtree:
            lines.extend(_generate_rvc_quadrant(qnum, qtree))
            lines.append("")

    lines.append("#endif /* RV32_HAS(EXT_C) */")
    lines.append("")

    # --- Unimplemented handler ---
    lines.append(
        "static inline bool op_unimp("
        "rv_insn_t *ir UNUSED, "
        "uint32_t insn UNUSED)"
    )
    lines.append("{")
    lines.append("    return false;")
    lines.append("}")
    lines.append("")

    # --- Jump tables ---
    lines.extend(_generate_jump_tables(groups_32))
    lines.append("")

    # --- rv_decode entry point ---
    lines.extend(_generate_rv_decode())

    return "\n".join(lines)


def _build_group_name_map() -> dict[int, str]:
    """Map opcode[6:2] values to function names."""
    return {
        0x00: "load",
        0x01: "load_fp",
        0x03: "misc_mem",
        0x04: "op_imm",
        0x05: "auipc",
        0x08: "store",
        0x09: "store_fp",
        0x0B: "amo",
        0x0C: "op",
        0x0D: "lui",
        0x10: "madd",
        0x11: "msub",
        0x12: "nmsub",
        0x13: "nmadd",
        0x14: "op_fp",
        0x18: "branch",
        0x19: "jalr",
        0x1B: "jal",
        0x1C: "system",
    }


def _infer_group_type(subtree) -> str:
    """Find the most common instruction type in a group subtree."""
    if isinstance(subtree, Instruction):
        return infer_insn_type(subtree.operands)

    types = []

    def _collect(node):
        if isinstance(node, Instruction):
            t = infer_insn_type(node.operands)
            if t:
                types.append(t)
        elif isinstance(node, dict):
            for v in node.values():
                if isinstance(v, dict):
                    for vv in v.values():
                        _collect(vv)
                else:
                    _collect(v)

    _collect(subtree)
    if not types:
        return ""
    from collections import Counter

    return Counter(types).most_common(1)[0][0]


# Groups where rd==x0 should be treated as NOP.
# These are integer computational instructions where writing
# to x0 (hardwired zero) is functionally a no-op.
_NOP_RD0_GROUPS = {"op_imm", "auipc", "op", "lui"}

# Groups with a post-decode return check.
# op_system needs to verify CSR writability after decoding.
_POST_CHECKS = {
    "system": (
        "return csr_is_writable(ir->imm) " "|| (ir->rs1 == rv_reg_zero);"
    ),
}

# F extension groups that use funct3 as rounding mode (ir->rm).
# decode_rtype only sets rd/rs1/rs2; these groups need ir->rm too.
_RM_GROUPS = {"op_fp", "madd", "msub", "nmsub", "nmadd"}

# All instructions in the following groups belong to EXT_F
# When generating, wrap them with #if RV32_HAS(EXT_F)
_EXT_F_GROUPS = {
    "load_fp",
    "store_fp",
    "madd",
    "msub",
    "nmsub",
    "nmadd",
    "op_fp",
}

# --- RVC operand decode tables ---
# Each operand name from instructions.in maps to C statements
# that extract the field into the rv_insn_t struct.

_RVC_OPERAND_DECODERS = {
    # 5-bit full-range registers
    "crd": ["ir->rd = c_decode_rd(insn);"],
    "crs1": ["ir->rs1 = c_decode_rs1(insn);"],
    "crs1rd": [
        "ir->rd = c_decode_rd(insn);",
        "ir->rs1 = ir->rd;",
    ],
    "crs2": ["ir->rs2 = c_decode_rs2(insn);"],
    # 3-bit compressed registers (x8-x15)
    "crdq": ["ir->rd = c_decode_rdc(insn) | 0x08;"],
    "crs1q": ["ir->rs1 = c_decode_rs1c(insn) | 0x08;"],
    "crs2q": ["ir->rs2 = c_decode_rs2c(insn) | 0x08;"],
    # --- Immediates ---
    # caddi4spn: nzuimm[5:4|9:6|2|3]
    "cimm4spn": [
        "ir->imm = ((insn & 0x1800) >> 7) | ((insn & 0x0780) >> 1)"
        " | ((insn & 0x0040) >> 4) | ((insn & 0x0020) >> 2);",
    ],
    # c.lw/c.sw/c.flw/c.fsw: uimm[5:3|2|6]
    "cimmw": [
        "ir->imm = ((insn & 0x1C00) >> 7)"
        " | ((insn & 0x0040) >> 4) | ((insn & 0x0020) << 1);",
    ],
    # c.li/c.addi/c.andi: imm[5|4:0] sign-extended
    "cimmi": [
        "ir->imm = ((insn >> 2) & 0x1f)" " | ((insn & 0x1000) >> 7);",
        "ir->imm |= -(ir->imm & 0x20);",
    ],
    "cnzimmi": [
        "ir->imm = ((insn >> 2) & 0x1f)" " | ((insn & 0x1000) >> 7);",
        "ir->imm |= -(ir->imm & 0x20);",
    ],
    # c.slli/c.srli/c.srai: shamt[5|4:0] unsigned
    # cslli reads ir->imm, csrli/csrai read ir->shamt
    "cshamt": [
        "ir->shamt = ((insn >> 7) & 0x20) | ((insn >> 2) & 0x1f);",
        "ir->imm = ir->shamt;",
    ],
    # c.beqz/c.bnez: offset[8|4:3|7:6|2:1|5] sign-extended
    "cimmb": [
        "ir->imm = ((insn & 0x0C00) >> 7) | ((insn & 0x0060) << 1)"
        " | ((insn & 0x0018) >> 2) | ((insn & 0x0004) << 3)"
        " | ((insn & 0x1000) >> 4);",
        "ir->imm |= -(ir->imm & 0x100);",
    ],
    # c.j/c.jal: offset[11|4|9:8|10|6|7|3:1|5] sign-extended
    "cimmj": [
        "ir->imm = ((insn & 0x0800) >> 7) | ((insn & 0x0600) >> 1)"
        " | ((insn & 0x0100) << 2) | ((insn & 0x0080) >> 1)"
        " | ((insn & 0x0040) << 1) | ((insn & 0x0038) >> 2)"
        " | ((insn & 0x0004) << 3) | ((insn & 0x1000) >> 1);",
        "ir->imm |= -(ir->imm & 0x800);",
    ],
    # c.lwsp/c.flwsp: uimm[5|4:2|7:6]
    "cimmlwsp": [
        "ir->imm = ((insn & 0x1000) >> 7)"
        " | ((insn & 0x0070) >> 2) | ((insn & 0x000C) << 4);",
    ],
    # c.swsp/c.fswsp: uimm[5:2|7:6]
    "cimmswsp": [
        "ir->imm = ((insn & 0x1E00) >> 7)" " | ((insn & 0x0180) >> 1);",
    ],
    # c.addi16sp: nzimm[9|4|6|8:7|5] sign-extended
    "cnzimm16sp": [
        "ir->imm = ((insn & 0x0040) >> 2) | ((insn & 0x0020) << 1)"
        " | ((insn & 0x0018) << 4) | ((insn & 0x0004) << 3)"
        " | ((insn & 0x1000) >> 3);",
        "ir->imm |= -(ir->imm & 0x200);",
    ],
}

# Per-instruction implicit registers or fixups not captured
# by operand names alone.
_RVC_INSN_IMPLICIT = {
    # sp-relative instructions: rs1 = x2 (sp)
    "caddi4spn": ["ir->rs1 = rv_reg_sp;"],
    "caddi16sp": ["ir->rd = rv_reg_sp;", "ir->rs1 = rv_reg_sp;"],
    "clwsp": ["ir->rs1 = rv_reg_sp;"],
    "cswsp": ["ir->rs1 = rv_reg_sp;"],
    "cflwsp": ["ir->rs1 = rv_reg_sp;"],
    "cfswsp": ["ir->rs1 = rv_reg_sp;"],
    # instructions that expand with rs1 = x0
    "cli": ["ir->rs1 = rv_reg_zero;"],
    "cmv": ["ir->rs1 = rv_reg_zero;"],
    "cnop": ["ir->rd = rv_reg_zero;", "ir->rs1 = rv_reg_zero;"],
    # link register instructions
    "cjal": ["ir->rd = rv_reg_ra;"],
    "cjalr": ["ir->rd = rv_reg_ra;"],
    "cjr": ["ir->rd = rv_reg_zero;"],
    # c.j expands to jal x0, offset
    "cj": ["ir->rd = rv_reg_zero;"],
    # c.beqz/c.bnez expand to beq/bne rs1', x0, offset
    "cbeqz": ["ir->rs2 = rv_reg_zero;"],
    "cbnez": ["ir->rs2 = rv_reg_zero;"],
    # rd'/rs1' format: crs1q field is both source and dest
    "csrli": ["ir->rd = ir->rs1;"],
    "csrai": ["ir->rd = ir->rs1;"],
    "candi": ["ir->rd = ir->rs1;"],
    "csub": ["ir->rd = ir->rs1;"],
    "cxor": ["ir->rd = ir->rs1;"],
    "cor": ["ir->rd = ir->rs1;"],
    "cand": ["ir->rd = ir->rs1;"],
    # c.lui: imm is upper immediate (shifted left by 12)
    "clui": ["ir->imm <<= 12;"],
}


# RVC operands that decode a 5-bit rd field (which can be x0).
# Instructions using these need a NOP/reserved check when rd==0.
_RVC_RD_5BIT_OPERANDS = {"crd", "crs1rd"}

# Instructions where rd=x0 is RESERVED (return false), not a HINT.
# Per RISC-V spec §16.3: c.lwsp with rd=x0 is reserved.
_RVC_RD0_RESERVED = {"clwsp"}

# Instructions whose rd field addresses floating-point registers (f0-f31).
# rd=0 means f0 which is a valid destination — skip the x0 check entirely.
_RVC_FLOAT_DEST = {"cflwsp"}


def _rvc_operand_lines(insn: Instruction, pad: str) -> list[str]:
    """Generate operand decode lines for one RVC instruction.

    Looks up each operand in _RVC_OPERAND_DECODERS, then applies
    any per-instruction fixups from _RVC_INSN_IMPLICIT.

    For instructions with a 5-bit integer rd field:
      - clwsp:  rd==x0 is RESERVED → emit ``return false``
      - others: rd==x0 is a HINT   → emit cnop (not nop)

    Float-destination instructions (cflwsp) are excluded: rd=0 refers
    to f0, a valid floating-point register, so no zero-check is emitted.

    Using rv_insn_cnop (not rv_insn_nop) matches the original
    decode.c.bak and the RISC-V C extension spec §16.8.
    """
    lines = []
    for op in insn.operands:
        if op in _RVC_OPERAND_DECODERS:
            for stmt in _RVC_OPERAND_DECODERS[op]:
                lines.append(f"{pad}    {stmt}")
    if insn.name in _RVC_INSN_IMPLICIT:
        for stmt in _RVC_INSN_IMPLICIT[insn.name]:
            lines.append(f"{pad}    {stmt}")

    # NOP / reserved check for 5-bit rd instructions
    # Skip entirely for float-destination instructions (rd=0 → f0, valid).
    has_5bit_rd = any(op in _RVC_RD_5BIT_OPERANDS for op in insn.operands)
    if has_5bit_rd and insn.name not in _RVC_FLOAT_DEST:
        if insn.name in _RVC_RD0_RESERVED:
            # rd=x0 is a RESERVED encoding → illegal instruction
            lines.append(f"{pad}    if (unlikely(ir->rd == rv_reg_zero))")
            lines.append(f"{pad}        return false;")
        else:
            # rd=x0 is a HINT → convert to cnop (NOT rv_insn_nop)
            lines.append(f"{pad}    if (unlikely(ir->rd == rv_reg_zero)) {{")
            lines.append(f"{pad}        ir->opcode = rv_insn_cnop;")
            lines.append(f"{pad}        return true;")
            lines.append(f"{pad}    }}")
    return lines


def _generate_group_func(name: str, subtree, insn_type: str) -> list[str]:
    """Generate one op_xxx() function for a 32-bit opcode group."""
    lines = []
    lines.append(
        f"static inline bool op_{name}(" f"rv_insn_t *ir, const uint32_t insn)"
    )
    lines.append("{")
    if insn_type:
        lines.append(f"    decode_{insn_type}(ir, insn);")

    # F extension: funct3 encodes rounding mode (ir->rm).
    # decode_rtype only sets rd/rs1/rs2, so ir->rm must be set separately.
    if name in _RM_GROUPS:
        lines.append("    ir->rm = decode_funct3(insn);  /* rounding mode */")

    # NOP optimization: rd == x0 → nop
    if name in _NOP_RD0_GROUPS:
        lines.append("")
        lines.append(
            "    /* Any integer computational instruction "
            'writing into "x0" is NOP. */'
        )
        lines.append("    if (unlikely(ir->rd == rv_reg_zero)) {")
        lines.append("        ir->opcode = rv_insn_nop;")
        lines.append("        return true;")
        lines.append("    }")

    if isinstance(subtree, Instruction):
        lines.append(f"    ir->opcode = rv_insn_{subtree.name};")
        # Use post-check if this group has one
        if name in _POST_CHECKS:
            lines.append(f"    {_POST_CHECKS[name]}")
        else:
            lines.append("    return true;")
    else:
        lines.append("")
        lines.extend(generate_c_switch(subtree, 1))
        # Use post-check if this group has one
        if name in _POST_CHECKS:
            lines.append(f"    {_POST_CHECKS[name]}")
        else:
            lines.append("    return false;")
    lines.append("}")
    return lines


def _generate_rvc_quadrant(qnum: int, qtree: dict) -> list[str]:
    """Generate decode function for one RVC quadrant."""
    lines = []
    lines.append(
        f"static inline bool op_rvc_q{qnum}("
        f"rv_insn_t *ir, const uint32_t insn)"
    )
    lines.append("{")
    lines.extend(generate_c_switch(qtree, 1, rvc=True))
    lines.append("    return false;")
    lines.append("}")
    return lines


def _generate_jump_tables(
    groups_32: dict[int, object],
) -> list[str]:
    """Generate the rv_jump_table and rvc_jump_table."""
    lines = []
    lines.append("typedef bool (*decode_t)" "(rv_insn_t *ir, uint32_t insn);")
    lines.append("")

    group_names = _build_group_name_map()

    # 32-bit jump table: 32 entries for opcode[6:2]
    lines.append("static const decode_t rv_jump_table[] = {")
    for i in range(32):
        if i in group_names:
            fname = f"op_{group_names[i]}"
        else:
            fname = "op_unimp"
        lines.append(f"    {fname},  /* {i} */")
    lines.append("};")
    lines.append("")

    # 16-bit jump table: 32 entries for (funct3 << 2) | quadrant
    lines.append("#if RV32_HAS(EXT_C)")
    lines.append("static const decode_t rvc_jump_table[] = {")
    for funct3 in range(8):
        entries = []
        for q in range(4):
            if q < 3:
                entries.append(f"op_rvc_q{q}")
            else:
                entries.append("op_unimp")
        lines.append(f"    {', '.join(entries)},  " f"/* funct3={funct3} */")
    lines.append("};")
    lines.append("#endif /* RV32_HAS(EXT_C) */")

    return lines


def _generate_rv_decode() -> list[str]:
    """Generate the rv_decode() entry point."""
    lines = []
    lines.append("bool rv_decode(rv_insn_t *ir, uint32_t insn)")
    lines.append("{")
    lines.append("    bool ret;")
    lines.append("    assert(ir);")
    lines.append("")
    lines.append("#if RV32_HAS(EXT_C)")
    lines.append("    if (is_compressed(insn)) {")
    lines.append("        insn &= 0x0000FFFF;")
    lines.append(
        "        const uint16_t c_index = "
        "((insn & 0xE000) >> 11) | (insn & 0x3);"
    )
    lines.append("        ret = rvc_jump_table[c_index](ir, insn);")
    lines.append("        goto end;")
    lines.append("    }")
    lines.append("#endif")
    lines.append("")
    lines.append("    const uint32_t index = (insn >> 2) & 0x1f;")
    lines.append("    ret = rv_jump_table[index](ir, insn);")
    lines.append("")
    lines.append("end:")
    lines.append("")
    lines.append("#if RV32_HAS(RV32E)")
    lines.append(
        "    if (unlikely(ir->rd > 15 || " "ir->rs1 > 15 || ir->rs2 > 15))"
    )
    lines.append("        ret = false;")
    lines.append("#endif")
    lines.append("")
    lines.append("    return ret;")
    lines.append("}")
    return lines


# --- Template code for decode helpers ---
# These are fixed by the RISC-V ISA and don't depend on
# which instructions are defined in instructions.in.

_HELPERS_TEMPLATE = """\
/* decode rd field: insn[11:7] */
static inline uint32_t decode_rd(const uint32_t insn)
{
    return (insn >> 7) & 0x1f;
}

/* decode rs1 field: insn[19:15] */
static inline uint32_t decode_rs1(const uint32_t insn)
{
    return (insn >> 15) & 0x1f;
}

/* decode rs2 field: insn[24:20] */
static inline uint32_t decode_rs2(const uint32_t insn)
{
    return (insn >> 20) & 0x1f;
}

/* decode funct3 field: insn[14:12] — only used by F-extension (rounding mode) */
#if RV32_HAS(EXT_F)
static inline uint32_t decode_funct3(const uint32_t insn)
{
    return (insn >> 12) & 0x7;
}
#endif /* RV32_HAS(EXT_F) */


/* I-type immediate: insn[31:20] sign-extended */
static inline int32_t decode_itype_imm(const uint32_t insn)
{
    return ((int32_t) (insn & 0xFFF00000)) >> 20;
}

/* S-type immediate: insn[31:25|11:7] sign-extended */
static inline int32_t decode_stype_imm(const uint32_t insn)
{
    uint32_t dst = 0;
    dst |= (insn & 0xFE000000);
    dst |= (insn & 0x00000F80) << 13;
    return ((int32_t) dst) >> 20;
}

/* B-type immediate: insn[31|7|30:25|11:8] sign-extended */
static inline int32_t decode_btype_imm(const uint32_t insn)
{
    uint32_t dst = 0;
    dst |= (insn & 0x80000000);
    dst |= (insn & 0x00000080) << 23;
    dst |= (insn & 0x7E000000) >> 1;
    dst |= (insn & 0x00000F00) << 12;
    return ((int32_t) dst) >> 19;
}

/* U-type immediate: insn[31:12] */
static inline uint32_t decode_utype_imm(const uint32_t insn)
{
    return insn & 0xFFFFF000;
}

/* J-type immediate: insn[31|19:12|20|30:21] sign-extended */
static inline int32_t decode_jtype_imm(const uint32_t insn)
{
    uint32_t dst = 0;
    dst |= (insn & 0x80000000);
    dst |= (insn & 0x000FF000) << 11;
    dst |= (insn & 0x00100000) << 2;
    dst |= (insn & 0x7FE00000) >> 9;
    return ((int32_t) dst) >> 11;
}

#if RV32_HAS(EXT_F)
/* R4-type rs3 field: insn[31:27] */
static inline uint32_t decode_r4type_rs3(const uint32_t insn)
{
    return (insn >> 27) & 0x1f;
}
#endif

/* decode I-type */
static inline void decode_itype(rv_insn_t *ir, const uint32_t insn)
{
    ir->imm = decode_itype_imm(insn);
    ir->rs1 = decode_rs1(insn);
    ir->rd = decode_rd(insn);
}

/* decode U-type */
static inline void decode_utype(rv_insn_t *ir, const uint32_t insn)
{
    ir->imm = decode_utype_imm(insn);
    ir->rd = decode_rd(insn);
}

/* decode S-type */
static inline void decode_stype(rv_insn_t *ir, const uint32_t insn)
{
    ir->imm = decode_stype_imm(insn);
    ir->rs2 = decode_rs2(insn);
    ir->rs1 = decode_rs1(insn);
}

/* decode R-type */
static inline void decode_rtype(rv_insn_t *ir, const uint32_t insn)
{
    ir->rs2 = decode_rs2(insn);
    ir->rs1 = decode_rs1(insn);
    ir->rd = decode_rd(insn);
}

/* decode B-type */
static inline void decode_btype(rv_insn_t *ir, const uint32_t insn)
{
    ir->imm = decode_btype_imm(insn);
    ir->rs2 = decode_rs2(insn);
    ir->rs1 = decode_rs1(insn);
}

/* decode J-type */
static inline void decode_jtype(rv_insn_t *ir, const uint32_t insn)
{
    ir->imm = decode_jtype_imm(insn);
    ir->rd = decode_rd(insn);
}

#if RV32_HAS(EXT_F)
/* decode R4-type */
static inline void decode_r4type(rv_insn_t *ir, const uint32_t insn)
{
    ir->rd = decode_rd(insn);
    ir->rs1 = decode_rs1(insn);
    ir->rs2 = decode_rs2(insn);
    ir->rs3 = decode_r4type_rs3(insn);
    ir->rm = decode_funct3(insn);
}
#endif

FORCE_INLINE bool csr_is_writable(const uint32_t csr)
{
    return csr < 0xc00;
}
"""

_RVC_HELPERS_TEMPLATE = """\
/* RVC field decoders */
static inline uint16_t c_decode_rs1(const uint16_t insn) { return (insn >> 7) & 0x1f; }
static inline uint16_t c_decode_rs2(const uint16_t insn) { return (insn >> 2) & 0x1f; }
static inline uint16_t c_decode_rd(const uint16_t insn) { return (insn >> 7) & 0x1f; }
static inline uint16_t c_decode_rs1c(const uint16_t insn) { return (insn >> 7) & 0x7; }
static inline uint16_t c_decode_rs2c(const uint16_t insn) { return (insn >> 2) & 0x7; }
static inline uint16_t c_decode_rdc(const uint16_t insn) { return (insn >> 2) & 0x7; }
"""


def main():
    if len(sys.argv) < 2:
        print(
            f"Usage: {sys.argv[0]} <instructions.in> [--tree]",
            file=sys.stderr,
        )
        sys.exit(1)

    filepath = sys.argv[1]
    show_tree = "--tree" in sys.argv

    instructions = parse_instructions(filepath)

    tree = build_decision_tree(instructions)

    if show_tree:
        print_tree(tree)
    else:
        print(generate_c_code(instructions, tree))


if __name__ == "__main__":
    main()
