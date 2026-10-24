#!/usr/bin/env python3
"""Check the Zba spill loop's result and, with diagnostics, compiled execution."""

import argparse
import subprocess

from jit_profile import (
    ZBA_OPS,
    interpreted_entries,
    read_define,
    read_t1c_stats,
)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("emulator")
parser.add_argument("guest")
parser.add_argument("--blocks-per-iteration", type=int, default=1)
parser.add_argument("--require-stats", action="store_true")
args = parser.parse_args()
result = subprocess.run(
    [args.emulator, args.guest],
    capture_output=True,
    text=True,
    check=True,
    timeout=120,
)
assert result.stdout.strip() == "JIT spill loop OK", result
stats = read_t1c_stats(result.stderr, args.require_stats)
if stats:
    assert not ZBA_OPS.intersection(stats["blockers"]), stats
    assert stats["native_entries"] + stats["t2c_entries"] > 0, stats
    # Output alone cannot tell a spilled Zba lowering from an interpreted run.
    # Each interpreted iteration enters every loop block, while compiled code
    # iterates inside itself, so most iterations must have run compiled.
    iterations = read_define("tests/jit-spill-loop.S", "ITERATIONS")
    interpreted = interpreted_entries(stats) / args.blocks_per_iteration
    assert interpreted < iterations // 2, stats
print("JIT Zba spill loop OK")
