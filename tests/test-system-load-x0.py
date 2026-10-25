"""Check system zero-load probes and diagnostic CSR coverage."""

import argparse
import subprocess

from jit_profile import read_t1c_stats

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("emulator")
parser.add_argument("guest")
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
expects = args.expect_csr_entries is not None
counters = read_t1c_stats(result.stderr, expects)
if counters:
    if args.expect_csr_entries is not None:
        assert (
            counters["blockers"]["csrrs"]["entries"] == args.expect_csr_entries
        ), counters
print("System x0 loads OK")
