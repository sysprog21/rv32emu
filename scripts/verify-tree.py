"""Verify the decision tree against instructions.in.

For each instruction, builds a test word from its constraints,
then walks the decision tree to confirm it reaches the correct
instruction.

Usage:
    python3 scripts/verify-tree.py src/instructions.in
"""

import sys
import os
import importlib.util

# Import gen-decoder.py (has hyphen, can't use normal import)
_script_dir = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "gen_decoder",
    os.path.join(_script_dir, "gen-decoder.py"),
)
_mod = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_mod)

Instruction = _mod.Instruction
build_decision_tree = _mod.build_decision_tree
parse_instructions = _mod.parse_instructions


def build_test_word(insn: Instruction) -> int:
    """Build a test machine code word from an instruction's constraints.

    Sets each constrained bit range to the required value.
    Unconstrained bits are set to 1 (not 0) to avoid accidentally
    matching more-specific instructions. For example, caddi has no
    constraint on bits 11..7 (rd), but cnop requires 11..7=0. If we
    leave rd=0 in caddi's test word, the tree correctly matches cnop
    (the more specific instruction) instead of caddi.  By setting
    unconstrained bits to 1, we ensure the test word only matches
    through this instruction's own constraint path.
    """
    # Start with all bits set to 1
    is_compressed = any(
        hi == 1 and lo == 0 and val != 3 for hi, lo, val in insn.constraints
    )
    if is_compressed:
        word = 0xFFFF  # 16-bit
    else:
        word = 0xFFFFFFFF  # 32-bit

    # Clear and set each constrained range
    for hi, lo, value in insn.constraints:
        width = hi - lo + 1
        mask = (1 << width) - 1
        word &= ~(mask << lo)  # clear the range
        word |= (value & mask) << lo  # set the value

    return word


def walk_tree(tree: dict, word: int) -> Instruction | None:
    """Walk the decision tree with a test word.

    At each node, extract the relevant bits from the word
    and follow the matching branch. Returns the Instruction
    reached, or None if no path matches.

    Handles the case where a tree level has multiple switch
    keys (multiple bit-ranges at the same level) by trying
    each one — a match in a more-specific sub-tree returns
    early, otherwise fall through to less-specific ones.
    """
    if isinstance(tree, Instruction):
        return tree

    if not isinstance(tree, dict):
        return None

    # Collect all (hi,lo) keys at this level
    bit_range_keys = [k for k in tree if isinstance(k, tuple)]

    for hi, lo in sorted(bit_range_keys):
        subtree = tree[(hi, lo)]
        width = hi - lo + 1
        mask = (1 << width) - 1
        extracted = (word >> lo) & mask

        if extracted in subtree:
            result = walk_tree(subtree[extracted], word)
            if result is not None:
                return result

    return None


def verify_all(instructions: list[Instruction], tree: dict):
    """Verify every instruction can be reached in the tree."""
    passed = 0
    failed = 0
    errors = []

    for insn in instructions:
        word = build_test_word(insn)
        result = walk_tree(tree, word)

        if result is not None and result.name == insn.name:
            passed += 1
        else:
            failed += 1
            got = result.name if result else "None"
            errors.append(
                f"  FAIL: {insn.name:<12s} "
                f"word=0x{word:08x} "
                f"-> got '{got}'"
            )

    print(f"Verified {passed + failed} instructions:")
    print(f"  PASSED: {passed}")
    print(f"  FAILED: {failed}")

    if errors:
        print()
        for err in errors:
            print(err)

    return failed == 0


def main():
    if len(sys.argv) != 2:
        print(
            f"Usage: {sys.argv[0]} <instructions.in>",
            file=sys.stderr,
        )
        sys.exit(1)

    instructions = parse_instructions(sys.argv[1])
    tree = build_decision_tree(instructions)

    ok = verify_all(instructions, tree)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
