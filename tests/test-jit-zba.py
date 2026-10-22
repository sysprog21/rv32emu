#!/usr/bin/env python3
"""Require correct Zba arithmetic and actual native execution of its probe."""

import argparse
import pathlib
import subprocess

from jit_profile import ZBA_OPS, read_profile, read_symbols, t2c_threshold

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("emulator")
parser.add_argument("guest")
parser.add_argument("nm")
args = parser.parse_args()
profile = pathlib.Path(args.emulator).parent / (pathlib.Path(args.guest).name + ".prof")
profile.unlink(missing_ok=True)
result = subprocess.run([args.emulator, "-p", args.guest], capture_output=True,
                        text=True, check=True, timeout=120)
assert result.stdout.strip() == "Zba arithmetic OK", result
rows = read_profile(profile)
symbols = read_symbols(args.nm, args.guest)
for name, opcode in (("zba_probe", "sh1add"), ("zba_probe2", "sh2add"),
                     ("zba_probe3", "sh3add")):
    probe = rows[symbols[name]]
    assert opcode in probe["IR list"].split(" - "), (name, probe)
native_minimum = 16 * t2c_threshold()
# Every alias/source form has its own forced dispatcher entry, including the
# address probe, so publication alone cannot satisfy this per-block proof.
for pc, row in rows.items():
    if not ZBA_OPS.intersection(row["IR list"].split(" - ")):
        continue
    assert row["hot"] == "true", (hex(pc), row)
    if "t2c" in row:
        assert row["t2c"] == "true", (hex(pc), row)
        assert int(row["frequency"]) - int(row["t1_reentries"]) > native_minimum, (hex(pc), row)
    else:
        assert int(row["t1_reentries"]) > native_minimum, (hex(pc), row)
print("Zba native execution OK")
