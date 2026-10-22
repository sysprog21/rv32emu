"""Require guest correctness and substantial actual tier-2 probe execution."""

import argparse
import pathlib
import subprocess

from jit_profile import read_profile, read_symbols, t2c_threshold

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("emulator")
parser.add_argument("guest")
parser.add_argument("nm")
parser.add_argument("--expect-fusion", action="store_true")
args = parser.parse_args()
emulator, guest = args.emulator, args.guest
symbols = read_symbols(args.nm, guest)

profile = pathlib.Path(emulator).parent / (pathlib.Path(guest).name + ".prof")
profile.unlink(missing_ok=True)
result = subprocess.run(
    [emulator, "-p", guest],
    capture_output=True,
    text=True,
    check=True,
    timeout=120,
)
assert result.stdout.strip() == "tiered load into x0 OK", result
rows = read_profile(profile)
if args.expect_fusion:
    opcodes = {
        opcode.strip()
        for row in rows.values()
        for opcode in row["IR list"].split("-")
    }
    assert {"fuse4", "fuse9", "fuse11"} <= opcodes, opcodes
row = rows[symbols["probe"]]
assert row["t2c"] == "true", row
# ECALL dispatches directly to probe each pass. Before promotion, cache
# frequency can also count interpreter branch probes; those end when T1
# starts, within two warmup thresholds. Once T2C is published,
# dispatcher accesses continue while t1_reentries stops. This conservative
# bound cannot be met by warmup or publication without executing tier 2.
assert (
    int(row["frequency"]) - int(row["t1_reentries"]) > 16 * t2c_threshold()
), row
print("Tier-2 x0 load execution OK")
