#!/usr/bin/env python3
"""Check the optional per-attempt T1C compile trace format and opt-in."""

import os
import re
import subprocess
import sys

emulator, guest = sys.argv[1:3]
LINE = re.compile(
    r"^T1C_COMPILE pc=[0-9a-f]{8} status=(ok|retry|failed) "
    r"insns=\d+ blocks=\d+ ns=\d+$"
)


def run(trace):
    env = dict(os.environ)
    env.pop("RV32EMU_T1C_COMPILE_TRACE", None)
    if trace:
        env["RV32EMU_T1C_COMPILE_TRACE"] = "1"
    result = subprocess.run(
        [emulator, guest],
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
        env=env,
    )
    return [l for l in result.stderr.splitlines() if "T1C_COMPILE" in l]


lines = run(True)
assert lines, "no T1C_COMPILE line"
for line in lines:
    assert LINE.match(line), line
assert any(" status=ok " in l for l in lines)
assert not run(False), "trace printed without the environment variable"
print("T1C compile trace OK")
