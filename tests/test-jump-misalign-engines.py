#!/usr/bin/env python3
"""Require JAL and JALR to a halfword target to trap identically in every
engine without C: mcause 0, mepc the jump, mtval the target, rd unchanged and
the same instruction count. Each emulator build runs the guest, which leaves
what its handler observed in registers; the final states must be identical.
"""

import argparse
from pathlib import Path
import tempfile

from jit_profile import (
    aot_options,
    build_user_guest,
    read_symbols,
    run_guest_state,
)

# Instructions retired between the first and the last trap, measured on the
# interpreter. The guest is deterministic, so every engine must match it even
# when no reference build is given. Change it together with the guest.
RETIRED = {
    "jal": 137978,
    "jalr": 137978,
    "jalr-nolink": 137978,
    "jalr-dirty": 149976,
    "jalr-alias-dirty": 143977,
    "jalr-alias": 137978,
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--reference", type=Path, help="interpreter build")
    parser.add_argument("--cc", required=True)
    parser.add_argument("--nm", required=True)
    parser.add_argument("--aot", metavar="LLVM_CONFIG")
    args = parser.parse_args()
    emulator = args.emulator.resolve()
    with tempfile.TemporaryDirectory(prefix="jump-engines-") as directory:
        work = Path(directory)
        for name, flags in (
            ("jal", []),
            ("jalr", ["-DJALR"]),
            ("jalr-nolink", ["-DJALR_NOLINK"]),
            ("jalr-dirty", ["-DJALR_DIRTY"]),
            ("jalr-alias-dirty", ["-DJALR_ALIAS_DIRTY"]),
            ("jalr-alias", ["-DJALR_ALIAS"]),
        ):
            guest = work / f"{name}.elf"
            build_user_guest(
                args.cc, "tests/jump-misalign-state.S", guest, *flags
            )
            symbols = read_symbols(args.nm, guest)
            runs = {"emulator": [emulator]}
            if args.reference:
                runs["reference"] = [args.reference.resolve()]
            if args.aot:
                aot = work / f"{name}-aot"
                aot.mkdir()
                runs["t2c"] = [
                    emulator,
                    *aot_options(emulator, [guest], args.aot, aot),
                ]
            states = {}
            for label, command in runs.items():
                dump = work / f"{name}-{label}.json"
                _, states[label] = run_guest_state(
                    command[0], guest, dump, *command[1:], timeout=300
                )
            jump = symbols["trapping_jump"]
            link = jump + 2 if name.startswith("jalr-alias") else 0x1234
            for label, state in states.items():
                where = (name, label, state)
                assert state["x9"] == 6000, where  # traps
                assert state["x18"] == 0, where  # mcause
                assert state["x19"] == jump, where  # mepc
                assert state["x20"] == jump + 2, where  # mtval
                assert state["x21"] == link, where  # rd unchanged
                assert (
                    state["x22"] == RETIRED[name]
                ), where  # instructions retired
                assert state["x24"] == 12, where  # unrelated dirty values
                assert state["x25"] == 23, where
                assert state["x26"] == 34, where
                assert state == states["emulator"], where
    print("misaligned JAL and JALR trap identically in every engine")


if __name__ == "__main__":
    main()
