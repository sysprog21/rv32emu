#!/usr/bin/env python3
"""Check that instruction page faults retry their fetch, in place inside a
nested handler or from the trap vector, and that the blocks decoded afterwards
keep JIT eligibility and unsupported blockers."""

import argparse
import re
import subprocess

from jit_profile import ROOT, interpreted_entries, read_t1c_stats

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("emulator")
parser.add_argument("guest")
parser.add_argument("--require-stats", action="store_true")
parser.add_argument("--nested", action="store_true")
args = parser.parse_args()
result = subprocess.run(
    [args.emulator, "-k", args.guest],
    capture_output=True,
    text=True,
    check=True,
    timeout=30,
)
lines = [
    line for line in result.stdout.splitlines() if line.startswith("SYSTEM ")
]
assert lines == ["SYSTEM fetch retry OK"], result
stats = read_t1c_stats(result.stderr, args.require_stats)
if stats and args.nested:
    # Each loop is called from an illegal-instruction handler run nested, and
    # faults twice on its first fetch, in place.
    assert stats["traps"][2] == 2, stats
    assert stats["traps"][12] == 4, stats
    assert stats["trap_instructions"] > 0, stats
elif stats:
    # Each loop faults twice on its first fetch. Faults outside a nested
    # handler enter the trap vector through the dispatcher, so the decoder
    # never retries a fetch in place, and the blocks decoded afterwards keep
    # JIT eligibility.
    assert stats["traps"][12] == 4, stats
    assert stats["fetch_retranslations"] == 0, stats
    assert stats["native_entries"] > 0, stats
    blockers = stats["blockers"]
    assert interpreted_entries(stats) < 65536 // 4, stats
    # Keep this regression useful when CSR lowering is implemented later.
    decode = (ROOT / "src/decode.h").read_text()
    flag = re.search(r"_\(csrrs,\s*\d+,\s*\d+,\s*(\d+),", decode)
    assert flag, "Cannot find csrrs translation flag"
    if flag[1] == "0":
        assert blockers["csrrs"]["entries"] >= 65536, stats
print("System fetch retry OK")
