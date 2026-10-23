#!/usr/bin/env python3
"""Check a flat kernel's result and, where system T1C compiles AMOs and CSR
instructions, that none of them kept a block interpreted."""

import argparse
import subprocess

from jit_profile import read_t1c_stats

HELPER_OPS = {
    "amoswapw",
    "amoaddw",
    "amoxorw",
    "amoandw",
    "amoorw",
    "amominw",
    "amomaxw",
    "amominuw",
    "amomaxuw",
    "csrrw",
    "csrrs",
    "csrrc",
    "csrrwi",
    "csrrsi",
    "csrrci",
}

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("emulator")
parser.add_argument("guest")
parser.add_argument("expected")
parser.add_argument("--require-stats", action="store_true")
parser.add_argument("--compiled-helpers", action="store_true")
args = parser.parse_args()
result = subprocess.run(
    [args.emulator, "-k", args.guest],
    capture_output=True,
    text=True,
    check=True,
    timeout=60,
)
lines = [
    line for line in result.stdout.splitlines() if line.startswith("SYSTEM ")
]
assert lines == [args.expected], result
stats = read_t1c_stats(result.stderr, args.require_stats)
if stats and args.compiled_helpers:
    # The setup before the loop sets stvec with CSRRW, interpreted while cold;
    # the compiled loop itself must leave no AMO or CSR blocker behind.
    blocked = {
        op: info["entries"]
        for op, info in stats["blockers"].items()
        if op in HELPER_OPS and info["entries"] > 16
    }
    assert not blocked, stats
    assert stats["native_entries"] > 64, stats
print(args.expected)
