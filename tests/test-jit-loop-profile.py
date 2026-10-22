"""Check profiling behavior as well as guest output for short loops."""

import pathlib
import subprocess
import sys

from jit_profile import read_profile, read_symbols

emulator, guest, nm = sys.argv[1:]
symbols = read_symbols(nm, guest)

subprocess.run([emulator, "-p", guest], check=True)
profile = pathlib.Path(emulator).parent / (pathlib.Path(guest).name + ".prof")
# Columns depend on the build configuration, so look them up by header name.
rows = read_profile(profile)

for name in ("self_loop", "cycle_head"):
    row = rows[symbols[name]]
    assert (row["hot"], row["loop"]) == ("true", "true"), (name, row)
    assert int(row["frequency"]) < 4096, (name, row)
row = rows[symbols["linear"]]
assert (row["hot"], row["loop"]) == ("false", "false"), row
print("JIT loop profiling OK")
