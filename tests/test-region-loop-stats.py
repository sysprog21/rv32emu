#!/usr/bin/env python3
"""Require that AArch64 T1C compiles the region-loop guest as region loops.

The guest's hash only proves the loops compute the right result. A build that
rejected every loop would still pass it, so read the compile counter too.
"""

import argparse
import platform
import subprocess

from jit_profile import read_t1c_stats

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("emulator")
parser.add_argument("guest")
args = parser.parse_args()
result = subprocess.run(
    [args.emulator, args.guest],
    capture_output=True,
    text=True,
    check=True,
    timeout=300,
)
stats = read_t1c_stats(result.stderr, True)
if platform.machine() in ("aarch64", "arm64"):
    assert stats["region_loops"] > 0, stats
else:
    # Region loops are AArch64 only.
    assert stats["region_loops"] == 0, stats
print(f"region loops compiled: {stats['region_loops']}")
