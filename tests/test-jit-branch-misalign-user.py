#!/usr/bin/env python3
"""Check taken/untaken misaligned branches after native warmup without C."""

import argparse
from pathlib import Path
import tempfile

from jit_profile import (
    aot_options,
    build_user_guest,
    read_symbols,
    read_t1c_stats,
    run_guest_state,
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--nm", required=True)
    parser.add_argument("--require-stats", action="store_true")
    parser.add_argument("--aot", metavar="LLVM_CONFIG")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="branch-misalign-") as directory:
        work = Path(directory)
        cases = (
            ("beq", 0, 0, 1, 0),
            ("bne", 1, 0, 0, 1),
            ("blt", 4, 0, 0, -1),
            ("bge", 5, 0, -1, 0),
            ("bltu", 6, 6, 1, 0),
            ("bgeu", 7, 6, 0, 1),
        )
        for name, funct, rs2, initial, final in cases:
            guest = work / "guest.elf"
            build_user_guest(
                args.cc,
                "tests/jit-branch-misalign-user.S",
                guest,
                f"-DFUNCT={funct}",
                f"-DRS2={rs2}",
                f"-DINITIAL={initial}",
                f"-DFINAL={final}",
            )
            symbols = read_symbols(args.nm, guest)
            pc = symbols["fault_branch"]
            actual = work / "actual.json"
            result, state = run_guest_state(
                args.emulator.resolve(), guest, actual
            )
            assert state["x18"] == pc, (name, state)
            assert state["x19"] == pc + 2, (name, state)
            assert state["x20"] == 0 and state["x9"] == (
                final & 0xFFFFFFFF
            ), state
            if args.require_stats:
                stats = read_t1c_stats(result.stderr, True)
                assert stats["misaligned_branch_traps"] > 0, (name, stats)
            if args.aot:
                # The same program through an offline tier-2 artifact of it.
                aot = work / f"{name}-aot"
                aot.mkdir()
                replay = aot_options(
                    args.emulator.resolve(), [guest], args.aot, aot
                )
                _, replayed = run_guest_state(
                    args.emulator.resolve(), guest, actual, *replay
                )
                assert replayed == state, name
    print("user misaligned branch native checks passed")


if __name__ == "__main__":
    main()
