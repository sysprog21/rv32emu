#!/usr/bin/env python3
"""Check Sv32 Zba arithmetic, high virtual addresses and native execution."""

import argparse
import subprocess

from jit_profile import (
    ZBA_OPS,
    interpreted_entries,
    read_define,
    read_t1c_stats,
    t2c_threshold,
)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("emulator")
parser.add_argument("guest")
parser.add_argument("--require-stats", action="store_true")
parser.add_argument("--require-t2c", action="store_true")
args = parser.parse_args()
result = subprocess.run(
    [args.emulator, "-k", args.guest],
    capture_output=True,
    text=True,
    check=True,
    timeout=120,
)
lines = [
    line for line in result.stdout.splitlines() if line.startswith("SYSTEM ")
]
assert lines == ["SYSTEM Zba arithmetic OK"], result
stats = read_t1c_stats(result.stderr, args.require_stats)
if stats:
    assert not ZBA_OPS.intersection(stats["blockers"]), stats
    # Every translatable block holds one Zba form and is entered through the
    # dispatcher once per iteration. A form left interpreted for half of the
    # iterations alone exceeds this bound, however many others run natively.
    iterations = read_define("tests/jit-zba.S", "ZBA_ITERATIONS")
    assert interpreted_entries(stats) < iterations // 2, stats
    if args.require_t2c:
        assert stats["t2c_entries"] > 16 * t2c_threshold(), stats
elif args.require_t2c:
    raise AssertionError("Tier-2 coverage requires T1C_STATS=1")
print("System Zba arithmetic OK")
