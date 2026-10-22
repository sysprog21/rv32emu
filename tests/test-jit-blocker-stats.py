#!/usr/bin/env python3
"""Check that opcode attribution counts entries hidden by interpreter chains."""

import argparse
import subprocess
import pathlib

from jit_profile import read_profile, read_t1c_stats

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("emulator")
parser.add_argument("guest")
parser.add_argument("--fused-head", choices=("load", "store"))
parser.add_argument("--expect-fusion", action="store_true")
args = parser.parse_args()
# The emulator writes the profile beside itself, named after the guest.
profile = pathlib.Path(args.emulator).parent / (
    pathlib.Path(args.guest).name + ".prof"
)
profile.unlink(missing_ok=True)
result = subprocess.run(
    [args.emulator, "-p", args.guest], check=True, capture_output=True, text=True, timeout=30
)
stats = read_t1c_stats(result.stderr, True)
blocked = stats["blockers"]["amoaddw"]
# The first pass shares the startup block; later passes enter the three-insn
# loop directly. Include the setup once, never on every iteration.
assert blocked["entries"] == 100000, blocked
assert blocked["insns"] == (600005 if args.fused_head else 300005), blocked
rows = read_profile(profile)
if args.expect_fusion:
    opcode = "fuse4" if args.fused_head == "load" else "fuse3"
    assert any(row["IR list"].startswith(opcode) for row in rows.values()), opcode
assert blocked["decoded"] == 2, blocked
# Tiered builds probe their interpreted chains back into the dispatcher;
# packed T1 builds keep the loop chained for almost all iterations.
if "t2c" not in next(iter(rows.values())):
    assert stats["dispatches"] < 1000, stats["dispatches"]
assert sum(v["insns"] for v in stats["blockers"].values()) == stats["guest_cycles"]
print("JIT blocked-opcode attribution OK")
