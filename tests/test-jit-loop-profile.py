"""Check profiling behavior as well as guest output for short loops."""

import pathlib
import subprocess
import sys

emulator, guest, nm = sys.argv[1:]
symbols = {}
for line in subprocess.check_output([nm, guest], text=True).splitlines():
    fields = line.split()
    if len(fields) == 3:
        symbols[fields[2]] = int(fields[0], 16)

subprocess.run([emulator, "-p", guest], check=True)
profile = pathlib.Path(emulator).parent / (pathlib.Path(guest).name + ".prof")
# Columns depend on the build configuration, so look them up by header name.
header, rows = None, {}
for line in profile.read_text().splitlines():
    fields = [field.strip() for field in line.split("|")]
    if fields[0] == "PC start":
        header = fields
    elif fields[0].startswith("0x"):
        rows[int(fields[0], 16)] = dict(zip(header, fields))

for name in ("self_loop", "cycle_head"):
    row = rows[symbols[name]]
    assert (row["hot"], row["loop"]) == ("true", "true"), (name, row)
    assert int(row["frequency"]) < 4096, (name, row)
row = rows[symbols["linear"]]
assert (row["hot"], row["loop"]) == ("false", "false"), row
print("JIT loop profiling OK")
