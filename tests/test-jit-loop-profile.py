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
rows = {}
for line in profile.read_text().splitlines():
    fields = [field.strip() for field in line.split("|")]
    if fields[0].startswith("0x"):
        rows[int(fields[0], 16)] = fields

for name in ("self_loop", "cycle_head"):
    row = rows[symbols[name]]
    assert row[3:5] == ["true", "true"], (name, row)
    assert int(row[2]) < 4096, (name, row)
assert rows[symbols["linear"]][3:5] == ["false", "false"]
print("JIT loop profiling OK")
