#!/usr/bin/env python3
"""Check a taken misaligned branch ending a loop-fed T1C region without C."""

import argparse
from pathlib import Path
import tempfile

from jit_profile import (
    build_user_guest,
    read_symbols,
    read_t1c_stats,
    run_guest_state,
)

ITERATIONS = 6000


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--nm", required=True)
    parser.add_argument("--require-stats", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="branch-region-") as directory:
        work = Path(directory)
        for name, flags in (("caller", ["-DCALLER"]), ("loop", [])):
            guest = work / "guest.elf"
            build_user_guest(
                args.cc,
                "tests/jit-branch-region-misalign-user.S",
                guest,
                *flags,
            )
            symbols = read_symbols(args.nm, guest)
            pc = symbols["fault_branch"]
            actual = work / "actual.json"
            result, state = run_guest_state(
                args.emulator.resolve(), guest, actual
            )
            # mcause 0 is an instruction address misaligned exception.
            assert state["x20"] == 0, (name, state)
            # The exception PC is the branch, the trap value its target.
            assert state["x18"] == pc, (name, state)
            assert state["x19"] == pc + 2, (name, state)
            # The loop stops at the last iteration, before s0 is decremented.
            assert state["x8"] == 1 and state["x9"] == 1, (name, state)
            if name == "caller":
                # The JAL that preceded the branch must have linked.
                assert state["x1"] == symbols["link_point"], (name, state)
            else:
                assert state["x1"] == 0x1234, (name, state)
                assert state["x23"] == ITERATIONS - 1, (name, state)
            # Every iteration retires at least two instructions.
            cycles = (state["x21"] - state["x22"]) & 0xFFFFFFFF
            assert 2 * ITERATIONS <= cycles <= 16 * ITERATIONS, (name, cycles)
            if args.require_stats:
                stats = read_t1c_stats(result.stderr, True)
                assert stats["misaligned_branch_traps"] > 0, (name, stats)
    print("user misaligned region branch checks passed")


if __name__ == "__main__":
    main()
