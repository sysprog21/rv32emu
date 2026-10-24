"""Require guest correctness and substantial actual tier-2 probe execution,
through an offline AOT artifact of the run.
"""

import argparse
import pathlib
import subprocess
import tempfile

from jit_profile import aot_options, read_profile, read_symbols, t2c_threshold

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("emulator")
parser.add_argument("guest")
parser.add_argument("nm")
parser.add_argument("--aot", required=True, metavar="LLVM_CONFIG")
args = parser.parse_args()
emulator, guest = args.emulator, args.guest
symbols = read_symbols(args.nm, guest)

work = tempfile.TemporaryDirectory(prefix="t2c-load-x0-")
replay = aot_options(emulator, [guest], args.aot, work.name)
profile = pathlib.Path(emulator).parent / (pathlib.Path(guest).name + ".prof")
profile.unlink(missing_ok=True)
result = subprocess.run(
    [emulator, *replay, "-p", guest],
    capture_output=True,
    text=True,
    check=True,
    timeout=120,
)
assert result.stdout.strip() == "tiered load into x0 OK", result
rows = read_profile(profile)
row = rows[symbols["probe"]]
assert row["t2c"] == "true", row
# ECALL dispatches directly to probe each pass. Its artifact binds on the
# first dispatch, after which dispatcher accesses continue while t1_reentries
# stays put. This bound cannot be met without executing tier 2.
assert (
    int(row["frequency"]) - int(row["t1_reentries"]) > 16 * t2c_threshold()
), row
print("Tier-2 x0 load execution OK")
