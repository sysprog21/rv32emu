#!/usr/bin/env python3
"""Boot Linux through a real AOT artifact and compare login latency with T1."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import pty
import re
import random
import select
import shutil
import statistics
import subprocess
import time


def boot(
    emulator, kernel, initrd, log, extra=(), timeout=300, cpu=None, heldout=None
):
    command = [str(emulator), "-k", str(kernel), "-i", str(initrd), *extra]
    if cpu is not None:
        command = ["taskset", "-c", str(cpu), *command]
    master, slave = pty.openpty()
    started = time.perf_counter()
    process = subprocess.Popen(
        command, stdin=slave, stdout=slave, stderr=slave, start_new_session=True
    )
    os.close(slave)
    output = bytearray()
    pending = bytearray()
    stage, ready = 0, None
    if heldout is None:
        heldout = "-C" not in extra
    try:
        with log.open("wb") as stream:
            while process.poll() is None:
                if time.perf_counter() - started > timeout:
                    raise TimeoutError(
                        f"Linux boot timed out at stage {stage}: {log}"
                    )
                if not select.select([master], [], [], 0.2)[0]:
                    continue
                try:
                    chunk = os.read(master, 65536)
                except OSError:
                    break
                if not chunk:
                    break
                stream.write(chunk)
                stream.flush()
                output.extend(chunk)
                pending.extend(chunk)
                if stage == 0 and b"buildroot login:" in pending:
                    ready = time.perf_counter() - started
                    os.write(master, b"root\n")
                    stage = 1
                    pending.clear()
                elif stage == 1 and b"# " in pending:
                    if heldout:
                        os.write(
                            master,
                            b"dd if=/dev/zero of=/tmp/aot-probe bs=4096 count=64 "
                            b"2>/dev/null && test -s /tmp/aot-probe && "
                            b"rm /tmp/aot-probe && echo AOT_HELDOUT_OK\n",
                        )
                    os.write(
                        master,
                        b"uname -m; cat /proc/sys/kernel/osrelease; "
                        b"echo AOT_BOOT_OK; poweroff\n",
                    )
                    stage = 2
                    pending.clear()
                elif stage == 2 and b"\nAOT_BOOT_OK\n" in pending.replace(
                    b"\r", b""
                ):
                    stage = 3
            process.wait(timeout=30)
            while select.select([master], [], [], 0)[0]:
                try:
                    chunk = os.read(master, 65536)
                except OSError:
                    break
                if not chunk:
                    break
                stream.write(chunk)
                output.extend(chunk)
        clean = output.replace(b"\r", b"")
        # The marker can arrive in the final drain, after the emulator exits.
        if stage == 2 and b"\nAOT_BOOT_OK\n" in clean:
            stage = 3
        if stage != 3 or process.returncode or b"\nriscv32\n" not in clean:
            raise RuntimeError(f"Linux boot/commands/shutdown failed: {log}")
        if heldout and b"\nAOT_HELDOUT_OK\n" not in clean:
            raise RuntimeError(f"Linux held-out workload failed: {log}")
        entries = re.search(rb"AOT: entries=(\d+)", output)
        entries = int(entries[1]) if entries else 0
        if "-A" in extra and not entries:
            raise RuntimeError(f"Linux boot did not execute AOT regions: {log}")
        return {
            "ready_seconds": ready,
            "aot_entries": entries,
            "aot_counters": {
                key.decode(): int(value)
                for key, value in re.findall(
                    rb"(native_insns|slow_calls|slow_resumes|hash_hits|"
                    rb"hash_misses|bindings|fences|"
                    rb"hash_rechecks|hash_reuses|t1_flushes)=(\d+)",
                    output,
                )
            },
            "command": command,
        }
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", required=True, type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument(
        "--library", type=Path, help="Reuse an existing AOT artifact"
    )
    parser.add_argument(
        "--kernel", default="build/linux-image/Image", type=Path
    )
    parser.add_argument(
        "--initrd", default="build/linux-image/rootfs.cpio", type=Path
    )
    parser.add_argument("--llvm-config", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--cycles", type=int, default=4096)
    parser.add_argument("--cache-bits", type=int)
    parser.add_argument("--training-timeout", type=int, default=900)
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 8))
    parser.add_argument("--min-speedup", type=float, default=0.0)
    args = parser.parse_args()
    if args.runs < 2 or args.warmups < 0 or args.jobs < 1:
        # One pair gives no spread, so the interval and the speedup gate
        # below would rest on a single measurement.
        parser.error(
            "--runs must be at least 2, --jobs positive, --warmups nonnegative"
        )
    if args.cpu is not None and args.cpu not in os.sched_getaffinity(0):
        parser.error("--cpu must be allowed by the current CPU affinity")
    args.output.mkdir(parents=True, exist_ok=True)
    policy = ("-s", str(args.cycles))
    if args.cache_bits:
        policy += ("-B", str(args.cache_bits))
    library = (args.library or args.output / "linux.so").resolve()
    if args.library is None:
        profile = args.output / "profile"
        # Collection requires a new directory; this one belongs to the script.
        shutil.rmtree(profile, ignore_errors=True)
        boot(
            args.emulator,
            args.kernel,
            args.initrd,
            args.output / "collect.log",
            (*policy, "-C", str(profile)),
            timeout=args.training_timeout,
            cpu=args.cpu,
        )
        subprocess.run(
            [
                "python3",
                "tools/aot.py",
                str(profile),
                "-o",
                str(library),
                "--llvm-config",
                args.llvm_config,
                "-j",
                str(args.jobs),
                # Outside the profile, so reruns reuse unchanged objects.
                "--cache-dir",
                str(args.output / "objects"),
            ],
            check=True,
        )
    for warmup in range(args.warmups):
        for mode in ("t1", "aot"):
            boot(
                (
                    (args.baseline or args.emulator)
                    if mode == "t1"
                    else args.emulator
                ),
                args.kernel,
                args.initrd,
                args.output / f"warmup-{mode}-{warmup}.log",
                policy if mode == "t1" else (*policy, "-A", str(library)),
                cpu=args.cpu,
            )
    samples = {"t1": [], "aot": []}
    for run in range(args.runs):
        # Alternate order to limit drift and filesystem-cache bias.
        for mode in ("t1", "aot") if run % 2 == 0 else ("aot", "t1"):
            samples[mode].append(
                boot(
                    (
                        (args.baseline or args.emulator)
                        if mode == "t1"
                        else args.emulator
                    ),
                    args.kernel,
                    args.initrd,
                    args.output / f"{mode}-{run}.log",
                    policy if mode == "t1" else (*policy, "-A", str(library)),
                    cpu=args.cpu,
                )
            )
    medians = {
        mode: statistics.median(row["ready_seconds"] for row in rows)
        for mode, rows in samples.items()
    }

    def digest(path):
        return hashlib.sha256(Path(path).read_bytes()).hexdigest()

    assert len(samples["t1"]) == len(samples["aot"])
    ratios = [
        left["ready_seconds"] / right["ready_seconds"]
        for left, right in zip(samples["t1"], samples["aot"])
    ]
    rng = random.Random(0)
    bootstrap = sorted(
        statistics.median(rng.choices(ratios, k=len(ratios)))
        for _ in range(10000)
    )
    result = {
        "host": platform.platform(),
        "cpu": args.cpu,
        "warmups_per_mode": args.warmups,
        "paired_median_speedup": statistics.median(ratios),
        "paired_speedup_95pct_bootstrap": [bootstrap[249], bootstrap[9749]],
        "artifact_bytes": library.stat().st_size,
        "policy": {
            "cycles": args.cycles,
            "cache_bits": args.cache_bits,
        },
        "llvm": subprocess.check_output(
            [args.llvm_config, "--version"], text=True
        ).strip(),
        "sha256": {
            "emulator": digest(args.emulator),
            "baseline": digest(args.baseline or args.emulator),
            "kernel": digest(args.kernel),
            "initrd": digest(args.initrd),
            "artifact": digest(library),
        },
        "samples": samples,
        "median_seconds": medians,
        "speedup": medians["t1"] / medians["aot"],
    }
    (args.output / "results.json").write_text(
        json.dumps(result, indent=2) + "\n"
    )
    print(json.dumps(result, indent=2))
    if result["speedup"] < args.min_speedup:
        parser.exit(
            1, f"AOT speedup below {args.min_speedup}: see results.json\n"
        )


if __name__ == "__main__":
    main()
