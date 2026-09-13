#!/usr/bin/env python3
"""Compare interpreter-only performance on the documented benchmark suite."""

import argparse
import hashlib
import json
import os
import platform
import re
import shlex
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Pattern, Sequence, Tuple

NUMBER = r"[-+]?[0-9]+(?:\.[0-9]*)?(?:e[-+]?[0-9]+)?"
# Each workload names the timing fields its output carries. They are redacted
# before the two engines' outputs are compared; every other number, such as
# Dhrystone's pass count, stays part of the comparison.
Redaction = Tuple[Pattern[str], str]
# A number followed by its unit: "138004392 microseconds", "2062 DMIPS".
DHRYSTONE_TIMING: Tuple[Redaction, ...] = (
    (
        re.compile(
            rf"(?<![\w.]){NUMBER}(?=\s*(?:(?:micro|milli|nano)?seconds|dmips)\b)",
            re.IGNORECASE,
        ),
        "#",
    ),
)
# NBench's run date, its rate label values ("Iterations/sec. : 123.45"), its
# result rows ("NAME : iterations/sec : old index : new index") and its index
# summaries are all derived from the clock.
NBENCH_TIMING: Tuple[Redaction, ...] = (
    (re.compile(r"^(.*date and time[^:]*:).*$", re.IGNORECASE), r"\1 #"),
    (
        re.compile(rf"((?:/\s*sec\.?)\s*:\s*){NUMBER}", re.IGNORECASE),
        r"\1#",
    ),
    (
        re.compile(
            rf"^([A-Z][A-Z0-9 -]*?\s*:)\s*{NUMBER}\s*:\s*{NUMBER}\s*:\s*{NUMBER}\s*$"
        ),
        r"\1 # : # : #",
    ),
    (re.compile(rf"^([A-Z][A-Z -]*INDEX\s*:)\s*{NUMBER}\s*$"), r"\1 #"),
)


@dataclass(frozen=True)
class Workload:
    name: str
    binary: str
    args: Sequence[str]
    timing: Tuple[Redaction, ...] = ()
    slow: bool = False


def nbench(name: str, test: str, slow: bool = False) -> Workload:
    return Workload(name, "build/riscv32/nbench", (test,), NBENCH_TIMING, slow)


WORKLOADS = (
    nbench("numeric_sort", "0"),
    nbench("string_sort", "1"),
    nbench("bitfield", "2", slow=True),
    nbench("emfloat", "3"),
    nbench("assignment", "5"),
    nbench("idea", "6", slow=True),
    nbench("huffman", "7"),
    Workload("dhrystone", "build/riscv32/dhrystone", (), DHRYSTONE_TIMING),
    Workload("primes", "build/riscv32/primes", ()),
    Workload("sha512", "build/riscv32/sha512", ()),
)
ROOT = Path(__file__).resolve().parent.parent


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git(path: Path, *args: str) -> Optional[str]:
    try:
        return subprocess.check_output(
            ["git", "-C", str(path), *args],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def repository(path: Path) -> Optional[Path]:
    top = git(path, "rev-parse", "--show-toplevel")
    return Path(top) if top else None


def repository_state(path: Optional[Path]) -> Dict[str, object]:
    if path is None:
        return {"revision": "unknown", "dirty": "unknown"}
    status = git(path, "status", "--porcelain")
    return {
        "revision": git(path, "rev-parse", "HEAD") or "unknown",
        "dirty": "unknown" if status is None else bool(status),
    }


def run(command: Sequence[str], timeout: float) -> tuple[float, str]:
    start = time.perf_counter_ns()
    completed = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        errors="replace",
        timeout=timeout,
        check=False,
    )
    elapsed = (time.perf_counter_ns() - start) / 1_000_000_000
    if completed.returncode:
        error = completed.stderr.strip()[-2000:]
        raise RuntimeError(
            f"{' '.join(command)} exited {completed.returncode}: {error}"
        )
    return elapsed, completed.stdout


def normalized_output(output: str, timing: Sequence[Redaction]) -> str:
    """Redact only benchmark timing fields, never computed result values."""
    normalized = []
    for line in output.splitlines():
        for pattern, replacement in timing:
            line = pattern.sub(replacement, line)
        normalized.append(line)
    return "\n".join(normalized)


def alternating_order(index: int) -> Sequence[str]:
    """Swap which emulator runs first on odd iterations, so neither engine
    systematically benefits from caches the other one warmed."""
    order = ("rv32emu", "libriscv")
    return tuple(reversed(order)) if index % 2 else order


def summarize(samples: List[float]) -> Dict[str, object]:
    return {
        "samples_s": samples,
        "median_s": statistics.median(samples),
        "mean_s": statistics.mean(samples),
        "stdev_s": statistics.stdev(samples) if len(samples) > 1 else 0.0,
    }


def commands(
    args: argparse.Namespace, workload: Workload
) -> Dict[str, List[str]]:
    binary = str(ROOT / workload.binary)
    return {
        "rv32emu": [args.rv32emu, "-q", binary, *workload.args],
        "libriscv": [
            args.libriscv,
            "-n",
            *shlex.split(args.libriscv_args),
            "-s",
            binary,
            *workload.args,
        ],
    }


def selected_workloads(names: str) -> Sequence[Workload]:
    if names == "default":
        return tuple(workload for workload in WORKLOADS if not workload.slow)
    if names == "all":
        return WORKLOADS
    requested = set(names.split(","))
    known = {workload.name for workload in WORKLOADS}
    unknown = requested - known
    if unknown:
        raise ValueError(f"unknown workloads: {', '.join(sorted(unknown))}")
    return tuple(
        workload for workload in WORKLOADS if workload.name in requested
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rv32emu", default="build/rv32emu")
    parser.add_argument("--libriscv", required=True)
    parser.add_argument(
        "--libriscv-args",
        default="",
        help="additional arguments passed to the libriscv runner",
    )
    parser.add_argument(
        "--libriscv-build-flags",
        default="",
        help="libriscv build flags retained in the JSON provenance",
    )
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=900)
    parser.add_argument("--min-speedup", type=float, default=1.02)
    parser.add_argument("--max-relative-stdev", type=float, default=0.05)
    parser.add_argument(
        "--config",
        help="effective configuration of the rv32emu build (default: "
        ".effective-config next to --rv32emu)",
    )
    parser.add_argument(
        "--workloads",
        default="default",
        help="comma-separated names, 'all', or 'default' (all but the slow "
        "bitfield and idea workloads)",
    )
    parser.add_argument("--json", dest="json_path")
    return parser.parse_args()


def resolve_path(path: str) -> Path:
    candidate = Path(path)
    return candidate if candidate.is_absolute() else ROOT / candidate


def require_interpreter_only(config: Path, executable: Path) -> None:
    if not config.is_file():
        raise FileNotFoundError(f"missing effective configuration: {config}")
    settings = dict(
        line.strip().split("=", 1)
        for line in config.read_text().splitlines()
        if "=" in line
    )
    if (
        settings.get("CONFIG_INTERPRETER_ONLY") != "y"
        or settings.get("CONFIG_JIT") == "y"
    ):
        raise ValueError(
            "rv32emu must be built interpreter-only; run make defconfig and "
            "pass that build's .effective-config"
        )
    if config.stat().st_mtime > executable.stat().st_mtime:
        raise ValueError(
            "effective configuration is newer than rv32emu; rebuild before "
            "benchmarking"
        )


def host_metadata() -> Dict[str, object]:
    metadata: Dict[str, object] = {
        "machine": platform.machine(),
        "platform": platform.platform(),
        "python": platform.python_version(),
    }
    if hasattr(os, "sched_getaffinity"):
        metadata["cpu_affinity"] = sorted(os.sched_getaffinity(0))
    governor = Path("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor")
    if governor.is_file():
        metadata["cpu0_governor"] = governor.read_text().strip()
    return metadata


def main() -> int:
    args = parse_args()
    if (
        args.runs < 2
        or args.warmup < 0
        or args.timeout <= 0
        or args.min_speedup <= 1
        or args.max_relative_stdev < 0
    ):
        raise ValueError(
            "invalid run count (at least 2 for a deviation), warmup, "
            "timeout, speedup, or deviation limit"
        )

    workloads = selected_workloads(args.workloads)
    args.rv32emu = str(resolve_path(args.rv32emu))
    args.libriscv = str(resolve_path(args.libriscv))
    config = (
        resolve_path(args.config)
        if args.config
        else Path(args.rv32emu).parent / ".effective-config"
    )
    rv32emu_path = Path(args.rv32emu)
    libriscv_path = Path(args.libriscv)
    missing = [
        str(path)
        for path in (rv32emu_path, libriscv_path)
        if not path.is_file() or not os.access(path, os.X_OK)
    ]
    if missing:
        raise FileNotFoundError(
            f"missing or non-executable emulator(s): {', '.join(missing)}"
        )
    require_interpreter_only(config, rv32emu_path)
    missing_guests = [
        str(ROOT / workload.binary)
        for workload in workloads
        if not (ROOT / workload.binary).is_file()
    ]
    if missing_guests:
        raise FileNotFoundError(
            f"missing guest binary(s): {', '.join(missing_guests)}"
        )

    results = []
    all_win = True
    for workload in workloads:
        workload_commands = commands(args, workload)
        samples = {"rv32emu": [], "libriscv": []}
        try:
            outputs = {}
            for engine in ("rv32emu", "libriscv"):
                _, output = run(workload_commands[engine], args.timeout)
                outputs[engine] = output
            if normalized_output(
                outputs["rv32emu"], workload.timing
            ) != normalized_output(outputs["libriscv"], workload.timing):
                raise RuntimeError("guest output differs between emulators")
            for warmup_index in range(args.warmup):
                for engine in alternating_order(warmup_index):
                    run(workload_commands[engine], args.timeout)
            for run_index in range(args.runs):
                for engine in alternating_order(run_index):
                    elapsed, _ = run(workload_commands[engine], args.timeout)
                    samples[engine].append(elapsed)
            rv = summarize(samples["rv32emu"])
            lib = summarize(samples["libriscv"])
            ratio = lib["median_s"] / rv["median_s"]
            rv_deviation = rv["stdev_s"] / rv["mean_s"]
            lib_deviation = lib["stdev_s"] / lib["mean_s"]
            wins = (
                ratio >= args.min_speedup
                and rv_deviation <= args.max_relative_stdev
                and lib_deviation <= args.max_relative_stdev
            )
            all_win &= wins
            print(
                f"{workload.name:14} rv32emu={rv['median_s']:.3f}s "
                f"libriscv={lib['median_s']:.3f}s ratio={ratio:.3f} "
                f"{'WIN' if wins else 'LOSS'}",
                flush=True,
            )
            results.append(
                {
                    "name": workload.name,
                    "binary": workload.binary,
                    "args": list(workload.args),
                    "rv32emu": rv,
                    "libriscv": lib,
                    "rv32emu_speedup_over_libriscv": ratio,
                    "rv32emu_wins": wins,
                    "output_sha256": {
                        engine: hashlib.sha256(
                            outputs[engine].encode()
                        ).hexdigest()
                        for engine in outputs
                    },
                    "relative_stdev": {
                        "rv32emu": rv_deviation,
                        "libriscv": lib_deviation,
                    },
                    "failure": None,
                }
            )
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
            all_win = False
            print(f"{workload.name:14} ERROR: {error}", file=sys.stderr)
            results.append(
                {
                    "name": workload.name,
                    "binary": workload.binary,
                    "args": list(workload.args),
                    "rv32emu": {"samples_s": samples["rv32emu"]},
                    "libriscv": {"samples_s": samples["libriscv"]},
                    "failure": str(error),
                }
            )

    cmake_cache = libriscv_path.parent / "CMakeCache.txt"
    rv32emu_repo = repository(ROOT)
    libriscv_repo = repository(libriscv_path.parent)
    report = {
        "schema": 1,
        "host": host_metadata(),
        "executables": {
            "rv32emu": {
                "path": args.rv32emu,
                "sha256": sha256(rv32emu_path),
                **repository_state(rv32emu_repo),
                "effective_config": config.read_text(),
            },
            "libriscv": {
                "path": args.libriscv,
                "sha256": sha256(libriscv_path),
                # A runner outside any repository, or one built inside this
                # checkout, has no libriscv revision of its own to record.
                **repository_state(
                    None if libriscv_repo == rv32emu_repo else libriscv_repo
                ),
                "cmake_cache_sha256": (
                    sha256(cmake_cache)
                    if cmake_cache.is_file()
                    else "unavailable"
                ),
                "build_flags": args.libriscv_build_flags,
            },
        },
        "runs": args.runs,
        "warmup": args.warmup,
        "timeout_s": args.timeout,
        "min_speedup": args.min_speedup,
        "max_relative_stdev": args.max_relative_stdev,
        "workloads": results,
    }
    if args.json_path:
        output = resolve_path(args.json_path)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2) + "\n")
    return 0 if all_win else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
