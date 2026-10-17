#!/usr/bin/env python3
"""Run real no-ELF kernels; only the intended normal exit counts as a pass."""

import pathlib
import re
import struct
import subprocess
import sys
import tempfile


def run(binary, path, rootfs, words, status, diagnostic):
    path.write_bytes(struct.pack("<" + "I" * len(words), *words))
    try:
        result = subprocess.run(
            [binary, "-k", str(path), "-x", f"vblk:{rootfs},rootfs"],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=15,
            text=True,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise AssertionError(f"{path.name}: {error}") from error
    output = result.stdout
    assert result.returncode == status, (path.name, result.returncode, output)
    assert len(re.findall(diagnostic, output)) == 1, (path.name, output)
    assert not re.search(
        r"Unable to|failed|Error|Assertion|Segmentation|page fault",
        output,
        re.IGNORECASE,
    ), (path.name, output)
    if status == 0:
        assert "Exception" not in output, output
    print(f"CLI {path.name}: exit {status}, expected diagnostic")


def main():
    binary = str(pathlib.Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="rv32emu-exit-") as directory:
        path = pathlib.Path(directory)
        rootfs = path / "rootfs.img"
        # These kernels do not read the disk; one sector satisfies the ext4
        # build's marked-rootfs requirement.
        with rootfs.open("wb") as disk:
            disk.truncate(512)

        # SBI system reset, shutdown, no reason. No ELF or cross compiler.
        run(
            binary,
            path / "shutdown",
            rootfs,
            [
                0x535258B7,
                0x35488893,
                0x00000813,
                0x00000513,
                0x00000593,
                0x00000073,
            ],
            0,
            r"System reset: type=shutdown, reason=no reason",
        )
        # EBREAK raises a breakpoint exception. With no vector, this model
        # halts the kernel and reports a failing process status.
        run(
            binary,
            path / "breakpoint",
            rootfs,
            [0x00100073],
            1,
            r"Exception 3 at 0x00000000 without a trap vector",
        )
    print("CLI unhandled exit: 2 cases, 0 failures")


if __name__ == "__main__":
    main()
