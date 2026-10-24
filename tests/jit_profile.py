"""Read symbols, profiles and diagnostics for native JIT regression checks."""

import json
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
ZBA_OPS = {"sh1add", "sh2add", "sh3add"}


def read_symbols(nm, guest):
    symbols = {}
    for line in subprocess.check_output([nm, guest], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = int(fields[0], 16)
    return symbols


def read_profile(path):
    header, rows = None, {}
    for line in pathlib.Path(path).read_text().splitlines():
        fields = [field.strip() for field in line.split("|")]
        if fields[0] == "PC start":
            header = fields
        elif fields[0].startswith("0x"):
            rows[int(fields[0], 16)] = dict(zip(header, fields))
    return rows


def read_define(path, name):
    """Return the integer a repository file #defines as name."""
    return int(
        re.search(
            rf"^#define {name} (\d+)$", (ROOT / path).read_text(), re.MULTILINE
        ).group(1)
    )


def t2c_threshold():
    return read_define("src/cache.h", "THRESHOLD")


def read_t1c_stats(stderr, required=False):
    """Return the last T1C_STATS report, or None in a build without one."""
    reports = [
        json.loads(line[len("T1C_STATS ") :])
        for line in stderr.splitlines()
        if line.startswith("T1C_STATS ")
    ]
    if required:
        assert len(reports) == 1, stderr
    return reports[-1] if reports else None


def interpreted_entries(stats):
    """Count entries to blocks T1 could compile but the interpreter ran."""
    return (
        stats["blockers"].get("translatable_interpreted", {}).get("entries", 0)
    )
