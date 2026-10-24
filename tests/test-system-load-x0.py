"""Check system zero-load probes and diagnostic fusion/tier coverage."""

import argparse
import subprocess

from jit_profile import read_t1c_stats, t2c_threshold

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("emulator")
parser.add_argument("guest")
parser.add_argument("--require-t2c", action="store_true")
parser.add_argument("--expect-fusion", action="store_true")
parser.add_argument("--expect-csr-entries", type=int)
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
assert lines == ["SYSTEM load x0 OK"], result
# Every diagnostic expectation needs the counters; never skip one silently.
expects = (
    args.require_t2c
    or args.expect_fusion
    or args.expect_csr_entries is not None
)
counters = read_t1c_stats(result.stderr, expects)
if counters:
    if args.expect_csr_entries is not None:
        assert (
            counters["blockers"]["csrrs"]["entries"] == args.expect_csr_entries
        ), counters
    if args.expect_fusion:
        assert counters["lazy_fused_loads"] > 0, counters
    if args.require_t2c:
        assert counters["t2c_entries"] > 16 * t2c_threshold(), counters
print("System x0 loads OK")
