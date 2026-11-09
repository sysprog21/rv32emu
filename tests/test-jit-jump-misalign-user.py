#!/usr/bin/env python3
"""Check repeated misaligned JAL and JALR traps in user mode without C."""

import argparse
from pathlib import Path
import tempfile

from jit_profile import build_user_guest, read_t1c_stats, run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--require-stats", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="jump-misalign-") as directory:
        guest = Path(directory) / "guest.elf"
        for name, flags in (
            ("jal", []),
            ("jalr", ["-DJALR"]),
            ("jalr-nolink", ["-DJALR_NOLINK"]),
            ("jalr-dirty", ["-DJALR_DIRTY"]),
            ("jalr-alias-dirty", ["-DJALR_ALIAS_DIRTY"]),
            ("jalr-alias", ["-DJALR_ALIAS"]),
        ):
            build_user_guest(
                args.cc, "tests/jit-jump-misalign-user.S", guest, *flags
            )
            result = run(args.emulator.resolve(), guest)
            if args.require_stats:
                stats = read_t1c_stats(result.stderr, True)
                assert stats["misaligned_jal_traps"] > 0, (name, stats)
    print("user misaligned JAL and JALR checks passed")


if __name__ == "__main__":
    main()
