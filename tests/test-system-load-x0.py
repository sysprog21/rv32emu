"""Check system zero-load probes and diagnostic fusion/tier coverage."""

import argparse
import json
import subprocess

from jit_profile import t2c_threshold

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("emulator")
parser.add_argument("guest")
parser.add_argument("--require-t2c", action="store_true")
parser.add_argument("--expect-fusion", action="store_true")
args = parser.parse_args()
result = subprocess.run([args.emulator, "-k", args.guest], capture_output=True,
                        text=True, check=True, timeout=120)
lines = [line for line in result.stdout.splitlines() if line.startswith("SYSTEM ")]
assert lines == ["SYSTEM load x0 OK"], result
stats = [line.removeprefix("T1C_STATS ")
         for line in result.stderr.splitlines() if line.startswith("T1C_STATS ")]
if stats:
    counters = json.loads(stats[-1])
    if args.expect_fusion:
        assert counters["lazy_fused_loads"] > 0, counters
    if args.require_t2c:
        assert counters["t2c_entries"] > 16 * t2c_threshold(), counters
elif args.require_t2c:
    raise AssertionError("Tier-2 coverage requires T1C_STATS=1")
print("System x0 loads OK")
