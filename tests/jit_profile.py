"""Read symbols and emulator profiles for native JIT regression checks."""

import pathlib
import re
import subprocess


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


def t2c_threshold():
    header = pathlib.Path(__file__).resolve().parent.parent / "src/cache.h"
    return int(re.search(r"^#define THRESHOLD (\d+)$", header.read_text(),
                         re.MULTILINE).group(1))
