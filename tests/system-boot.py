#!/usr/bin/env python3
"""Interleave host wall-clock boot-to-login measurements (POSIX hosts).

Build rv32emu with system_defconfig and ENABLE_ROOTFS_EXT4=0. Example:
  python3 tests/system-boot.py --emulator before=build/rv32emu-before \
      --emulator after=build/rv32emu --qemu /path/to/qemu-system-riscv32 \
      --cpu 6 --runs 5 --output build/system-boot.json

QEMU includes its firmware startup; both engines boot the same kernel/initrd.
This measures warm host file caches, including runtime translation costs.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import pty
import re
import select
import signal
import statistics
import subprocess
import time


def read_chunk(master, output, wait):
    """Append one pty read to output; return False at EOF or on no data."""
    if not select.select([master], [], [], wait)[0]:
        return False
    try:
        chunk = os.read(master, 65536)
    except OSError:
        return False
    output.extend(chunk)
    return bool(chunk)


def stop(process, master):
    """Quit through the serial monitor, escalating to signals if needed."""
    try:
        os.write(master, b"\x01x")  # Both serial monitors accept Ctrl-a x.
    except OSError:
        pass  # The child may already have closed its terminal.
    for sig in (None, signal.SIGTERM, signal.SIGKILL):
        if sig is not None:
            try:
                os.killpg(process.pid, sig)
            except ProcessLookupError:
                pass  # The emulator exited on its own; reap it below.
        try:
            process.wait(timeout=None if sig == signal.SIGKILL else 2)
            return
        except subprocess.TimeoutExpired:
            pass


def boot(command, timeout):
    master, slave = pty.openpty()
    start = time.monotonic()
    process = None
    output = bytearray()
    sample = {}
    try:
        process = subprocess.Popen(
            command,
            stdin=slave,
            stdout=slave,
            stderr=slave,
            start_new_session=True,
        )
        os.close(slave)
        slave = None
        while time.monotonic() - start < timeout:
            if not select.select([master], [], [], 0.1)[0]:
                continue
            if not read_chunk(master, output, 0):
                break
            if b"buildroot login:" in output:
                sample["seconds"] = time.monotonic() - start
                return sample
            if b"Kernel panic" in output:
                break
        raise RuntimeError(
            f"Boot failed: {command!r}\n"
            + output[-4096:].decode(errors="replace")
        )
    finally:
        if slave is not None:
            os.close(slave)
        if process is not None:
            if process.poll() is None:
                stop(process, master)
            # Diagnostics are emitted at serial-monitor exit, outside timing.
            while read_chunk(master, output, 0):
                pass
            match = re.search(rb"T1C_STATS (\{[^\r\n]*\})", output)
            if match:
                sample["stats"] = json.loads(match.group(1))
        os.close(master)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--emulator", action="append", default=[], metavar="NAME=PATH"
    )
    parser.add_argument("--qemu", type=Path)
    parser.add_argument("--image", type=Path, default=Path("build/linux-image"))
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--require-stats",
        action="store_true",
        help="require T1C_STATS=1 exit counters from each rv32emu engine",
    )
    args = parser.parse_args()
    if args.runs < 1 or args.warmup < 0 or args.timeout <= 0:
        parser.error("runs/timeout must be positive and warmup nonnegative")
    if args.cpu is not None:
        if not hasattr(os, "sched_setaffinity"):
            parser.error("--cpu needs sched_setaffinity, which this host lacks")
        os.sched_setaffinity(0, {args.cpu})
    kernel = str((args.image / "Image").resolve(strict=True))
    initrd = str((args.image / "rootfs.cpio").resolve(strict=True))
    commands = {}
    for spec in args.emulator or ["T1C=build/rv32emu"]:
        name, path = spec.split("=", 1)
        if name in commands:
            parser.error(f"duplicate emulator name: {name}")
        commands[name] = [
            str(Path(path).resolve(strict=True)),
            "-k",
            kernel,
            "-i",
            initrd,
        ]
    if args.qemu:
        if "QEMU" in commands:
            parser.error("QEMU is reserved when --qemu is supplied")
        commands["QEMU"] = [
            str(args.qemu.resolve(strict=True)),
            "-M",
            "virt",
            "-accel",
            "tcg",
            "-m",
            "512M",
            "-nographic",
            "-kernel",
            kernel,
            "-initrd",
            initrd,
            "-append",
            "console=ttyS0 root=/dev/ram rw",
        ]
    samples = {name: [] for name in commands}
    diagnostics = {name: [] for name in commands}
    for iteration in range(args.warmup + args.runs):
        names = list(commands)
        if iteration % 2:
            names.reverse()
        for name in names:
            result = boot(commands[name], args.timeout)
            elapsed = result["seconds"]
            # QEMU never prints T1C_STATS; require them only from rv32emu.
            is_qemu = args.qemu and name == "QEMU"
            if args.require_stats and not is_qemu and "stats" not in result:
                raise RuntimeError(
                    f"{name}: missing T1C_STATS exit diagnostics"
                )
            if iteration >= args.warmup:
                samples[name].append(elapsed)
                diagnostics[name].append(result.get("stats"))
                print(f"{name}: {elapsed:.4f} s", flush=True)
    report = {
        "host": platform.platform(),
        "cpu": args.cpu,
        "commands": commands,
        "sha256": {
            path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
            for path in [kernel, initrd] + [cmd[0] for cmd in commands.values()]
        },
        "seconds": samples,
        "diagnostics": diagnostics,
        "median_seconds": {
            name: statistics.median(vals) for name, vals in samples.items()
        },
    }
    print(json.dumps(report["median_seconds"], indent=2))
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
