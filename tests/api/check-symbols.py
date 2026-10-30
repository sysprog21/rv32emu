#!/usr/bin/env python3
"""Check a host linked with --export-dynamic does not expose core internals."""

import re
import subprocess
import sys
from pathlib import Path


def symbols(nm, *args):
    result = subprocess.run(
        [nm, *args], check=True, text=True, capture_output=True
    )
    return {
        match[1]
        for line in result.stdout.splitlines()
        if (match := re.match(r"\S+\s+[A-Za-z]\s+(\w+)(?:@.*)?$", line))
    }


def main():
    nm, header, host, t2c, collector_path, *archives = sys.argv[1:]
    public = set(re.findall(r"\b(rv32emu_\w+)\s*\(", Path(header).read_text()))
    exported = symbols(nm, "-D", "--defined-only", host)
    internal = set()
    for archive in archives:
        internal |= symbols(nm, "-g", "--defined-only", archive)
    collector = {"cache_get", "log_impl", "set_init", "set_add", "set_has"}
    allowed = public | (collector if t2c == "1" else set())
    leaked = (exported & internal) - allowed
    missing = allowed - exported
    leaked |= internal & {"rv_profile", "rv_remap_stdstream"}
    if leaked or missing:
        raise SystemExit(
            f"Unexpected exports: {sorted(leaked)}; missing API: {sorted(missing)}"
        )
    if collector_path != "-":
        expected = {
            "t2c_compile",
            "rv32_aot_collector_abi",
            "rv32_aot_producer_llvm",
            "rv32_aot_call_conv",
        }
        # Some linkers add section-boundary symbols to every shared object.
        linker = {"__bss_start", "_edata", "_end", "_init", "_fini"}
        actual = symbols(nm, "-D", "--defined-only", collector_path) - linker
        if actual != expected:
            raise SystemExit(f"Unexpected collector exports: {sorted(actual)}")
    print(f"Symbol visibility: {len(public)} public APIs, no internal leaks")


if __name__ == "__main__":
    main()
