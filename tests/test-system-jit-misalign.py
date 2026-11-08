#!/usr/bin/env python3
"""Exercise repeated misaligned branch, JAL and JALR traps in system mode
without C. Each case boots a guest that must print its marker, and a T1C_STATS
build must also count the trap in the named statistic.
"""

import argparse
from pathlib import Path
import tempfile

from jit_profile import build_system_image, read_t1c_stats, run_system_image

CASES = {
    "branch": (
        "tests/system-jit-branch-misalign.S",
        "SYSTEM branch misalignment OK",
        "misaligned_branch_traps",
    ),
    "jal": (
        "tests/system-jit-jal-misalign.S",
        "SYSTEM JAL misalignment OK",
        "misaligned_jal_traps",
    ),
    "jalr": (
        "tests/jit-system-jalr.S",
        # The guest prints the instructions retired across a trap in each of
        # its two phases (8, measured on the interpreter and T1C alike), so
        # a miscounted trap fails here instead of only differing between runs.
        "AAAAAAAI\nAAAAAAAI\nSYSTEM JALR misalignment OK",
        "misaligned_jal_traps",
    ),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", choices=CASES)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--objcopy", required=True)
    parser.add_argument("--require-stats", action="store_true")
    args = parser.parse_args()
    source, marker, stat = CASES[args.case]
    with tempfile.TemporaryDirectory(prefix="system-misalign-") as directory:
        image = build_system_image(args.cc, args.objcopy, source, directory)
        result = run_system_image(args.emulator.resolve(), image)
        assert marker in result.stdout, result.stdout
        if args.require_stats:
            stats = read_t1c_stats(result.stderr, True)
            assert stats[stat] > 0, stats
    print(f"system {args.case} misalignment trap passed")


if __name__ == "__main__":
    main()
