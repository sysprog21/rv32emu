#!/usr/bin/env python3
"""Twister custom simulator with separate host launch-to-verdict timing."""
import argparse
import errno
import hashlib
import json
import os
from pathlib import Path
import pty
import select
import signal
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", choices=("qemu", "rv32emu"), required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--bin", type=Path, required=True)
    parser.add_argument(
        "--timeout",
        type=float,
        help="Optional wall limit; Twister normally owns the timeout",
    )
    args = parser.parse_args()
    output = Path(os.environ["ZEPHYR_COMPARE_OUT"]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    affinity = min(os.sched_getaffinity(0))
    os.sched_setaffinity(0, {affinity})
    if args.engine == "rv32emu":
        dummy = output / "dummy.img"
        if not dummy.exists():
            with dummy.open("wb") as handle:
                handle.truncate(1024 * 1024)
        command = [
            os.environ["RV32EMU"],
            "-k",
            str(args.bin),
            "-x",
            f"vblk:{dummy},rootfs",
        ]
    else:
        command = [
            os.environ.get("QEMU", "qemu-system-riscv32"),
            "-machine",
            "virt",
            "-cpu",
            "rv32",
            "-m",
            "256",
            "-smp",
            "1",
            "-bios",
            os.environ["OPENSBI"],
            "-kernel",
            str(args.elf),
            "-display",
            "none",
            "-monitor",
            "none",
            "-serial",
            "stdio",
            "-icount",
            "shift=6,align=off,sleep=off",
            "-rtc",
            "clock=vm",
        ]
    stopped = []
    for signum in (signal.SIGTERM, signal.SIGINT):
        signal.signal(signum, lambda number, _frame: stopped.append(number))
    stdout = sys.stdout.fileno()
    os.set_blocking(stdout, False)

    def forward(data):
        pending = memoryview(data)
        while pending and not stopped:
            _, ready, _ = select.select([], [stdout], [], 0.05)
            if ready:
                try:
                    written = os.write(stdout, pending)
                except BlockingIOError:
                    continue
                except BrokenPipeError:
                    stopped.append(signal.SIGTERM)
                    break
                pending = pending[written:]

    uid = hashlib.sha256(str(args.elf.resolve()).encode()).hexdigest()[:16]
    directory = output / args.engine
    directory.mkdir(exist_ok=True)
    master, slave = pty.openpty()
    started = time.monotonic()
    process = subprocess.Popen(
        command,
        stdin=subprocess.DEVNULL,
        stdout=slave,
        stderr=slave,
        start_new_session=True,
    )
    os.close(slave)
    verdict = None
    verdict_seconds = None
    tail = ""
    try:
        with (directory / f"{uid}.log").open("wb") as log:
            while not stopped:
                if (
                    args.timeout is not None
                    and time.monotonic() - started >= args.timeout
                ):
                    verdict = "timeout"
                    break
                ready, _, _ = select.select([master], [], [], 0.05)
                if ready:
                    try:
                        data = os.read(master, 65536)
                    except OSError as error:
                        if error.errno != errno.EIO:
                            raise
                        break
                    if not data:
                        break
                    log.write(data)
                    log.flush()
                    forward(data)
                    tail = (tail + data.decode(errors="replace"))[-65536:]
                    if "PROJECT EXECUTION FAILED" in tail:
                        verdict = "failed"
                        verdict_seconds = time.monotonic() - started
                        break
                    if "PROJECT EXECUTION SUCCESSFUL" in tail:
                        verdict = "passed"
                        verdict_seconds = time.monotonic() - started
                        break
                elif process.poll() is not None:
                    break
    finally:
        elapsed = time.monotonic() - started
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
        process.wait()
        os.close(master)
        result = {
            "engine": args.engine,
            "elf": str(args.elf.resolve()),
            "binary": str(args.bin.resolve()),
            "command": command,
            "verdict": verdict or ("harness_stop" if stopped else "no_verdict"),
            "host_launch_to_verdict_seconds": verdict_seconds,
            "host_observation_seconds": elapsed,
            "returncode": process.returncode,
            "host_cpu_affinity": [affinity],
            "elf_sha256": hashlib.sha256(args.elf.read_bytes()).hexdigest(),
            "binary_sha256": hashlib.sha256(args.bin.read_bytes()).hexdigest(),
        }
        (directory / f"{uid}.json").write_text(
            json.dumps(result, indent=2) + "\n"
        )
    return 0 if stopped or verdict == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
