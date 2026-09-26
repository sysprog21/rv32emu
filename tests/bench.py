#!/usr/bin/env python3
"""
Unified benchmark runner for rv32emu.

Benchmarks are registered via the @register_benchmark decorator.
Supports parallel execution while preserving user-specified output order.

With --baseline, each benchmark instead runs as a paired comparison of two
emulator binaries. Every pair launches the baseline and the candidate at the
same time ("duet benchmarking"), so both see the same interference from the
host. This matters on shared CI runners, whose speed differs by more than 2x
between machines and drifts within a job; a comparison against a result stored
from another run cannot separate that noise from the change under test.
"""

import subprocess
import re
import statistics
import math
import os
import platform
import sys
import json
import argparse
import threading
import time
from abc import ABC, abstractmethod
from concurrent.futures import ThreadPoolExecutor, as_completed
from subprocess import TimeoutExpired
from typing import Callable, ClassVar, Dict, List, Optional, Tuple, Type

# Configuration
EMU_PATH = "build/rv32emu"
DEFAULT_RUNS = 5  # Balance, providing reasonable statistics
TIMEOUT_SECONDS = 600  # 10 min timeout per run (safety limit)
SLOW_THRESHOLD_SECONDS = 300  # If single run > 5 min, use only 1 run
MAX_BENCHMARK_SECONDS = 600  # 10 min max total time per benchmark
DEFAULT_THRESHOLD = 2.0  # Smallest change (%) reported as significant

# Two-sided 95% quantiles of Student's t distribution, indexed by degrees of
# freedom. t_975() extends the table past it.
T_975 = (
    12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
    2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
)  # fmt: skip


def t_975(df: int) -> float:
    """The two-sided 95% quantile of Student's t with df degrees of freedom.

    Past the table, the Cornish-Fisher expansion around the normal quantile is
    within 1e-4 of the exact value; the normal quantile alone would make the
    interval too narrow.
    """
    if df <= len(T_975):
        return T_975[df - 1]
    z = 1.959964
    return (
        z
        + (z**3 + z) / (4 * df)
        + (5 * z**5 + 16 * z**3 + 3 * z) / (96 * df**2)
        + (3 * z**7 + 19 * z**5 + 17 * z**3 - 15 * z) / (384 * df**3)
    )


# Benchmark registry
_BENCHMARK_REGISTRY: Dict[str, Type["Benchmark"]] = {}


class ProgressIndicator:
    """Thread-safe progress indicator with spinner animation."""

    SPINNER = ["◐", "◓", "◑", "◒"]  # Rotating circle animation

    def __init__(self, benchmarks: List[str], n_runs: int, quiet: bool = False):
        self.benchmarks = benchmarks
        self.n_runs = n_runs
        # Disable indicator if not a TTY to avoid log clutter
        self.quiet = quiet or not sys.stdout.isatty()
        self.lock = threading.Lock()
        # Track status: {bench_name: status}
        self.status: Dict[str, str] = {name: "pending" for name in benchmarks}
        self.start_time = time.monotonic()
        self.last_render = 0.0
        self._stop_event = threading.Event()
        self._spinner_thread: Optional[threading.Thread] = None

    def start(self) -> None:
        """Start the background spinner thread."""
        if self.quiet:
            return
        # Reserve terminal space to avoid overwriting history
        # (1 line for elapsed + 1 line per benchmark)
        sys.stdout.write("\n" * (len(self.benchmarks) + 1))
        sys.stdout.flush()
        self._stop_event.clear()
        self._spinner_thread = threading.Thread(
            target=self._spinner_loop, daemon=True
        )
        self._spinner_thread.start()

    def _spinner_loop(self) -> None:
        """Background loop to update spinner every 1 second."""
        while not self._stop_event.is_set():
            with self.lock:
                self._render()
            self._stop_event.wait(1.0)

    def update(
        self, bench_name: str, run: int, status: str = "running"
    ) -> None:
        """Update status for a benchmark."""
        with self.lock:
            self.status[bench_name] = status

    def _render(self) -> None:
        """Render status for all benchmarks."""
        if self.quiet:
            return
        elapsed = time.monotonic() - self.start_time
        spinner_idx = int(elapsed) % len(self.SPINNER)
        spinner = self.SPINNER[spinner_idx]

        lines = [f"\033[2K  Elapsed: {elapsed:.1f}s\n"]

        for name in self.benchmarks:
            status = self.status[name]
            if status == "pending":
                indicator = "⏳"
                state = ""
            elif status == "done":
                indicator = "✓"
                state = ""
            elif status == "failed":
                indicator = "✗"
                state = " (failed)"
            else:  # running
                indicator = spinner
                state = " (running)"

            lines.append(f"\033[2K  {indicator} {name}{state}\n")

        # Move cursor up to overwrite
        sys.stdout.write(f"\033[{len(lines)}A")
        sys.stdout.write("".join(lines))
        sys.stdout.flush()

    def finish(self) -> None:
        """Stop spinner and show final state (preserving failed status)."""
        self._stop_event.set()
        if self._spinner_thread:
            self._spinner_thread.join(timeout=1.0)
        if self.quiet:
            return
        with self.lock:
            # Only mark pending/running as done, preserve failed status
            for name in self.benchmarks:
                if self.status[name] not in ("done", "failed"):
                    self.status[name] = "done"
            self._render()
        # Move past display
        print("\n" * (len(self.benchmarks) + 1))


def register_benchmark(name: str):
    """Decorator to register a benchmark class."""

    def decorator(cls: Type["Benchmark"]) -> Type["Benchmark"]:
        _BENCHMARK_REGISTRY[name.lower()] = cls
        return cls

    return decorator


def get_registered_benchmarks() -> Dict[str, Type["Benchmark"]]:
    """Return all registered benchmarks."""
    return _BENCHMARK_REGISTRY.copy()


def host_description() -> str:
    """Describe the host CPU, so results from different runners are told apart."""
    try:
        with open("/proc/cpuinfo") as f:
            for line in f:
                if line.startswith("model name"):
                    return line.split(":", 1)[1].strip()
    except OSError:
        pass
    try:
        brand = subprocess.run(
            ["sysctl", "-n", "machdep.cpu.brand_string"],
            capture_output=True,
            text=True,
            check=False,
        ).stdout.strip()
        if brand:
            return brand
    except OSError:
        pass
    return platform.processor() or platform.machine()


class Benchmark(ABC):
    """Abstract base class for all benchmarks."""

    name: ClassVar[str]
    unit: ClassVar[str]
    BIN_PATH: ClassVar[str]

    def __init__(
        self,
        n_runs: int,
        progress: Optional[ProgressIndicator],
        emu: str,
    ):
        self.n_runs = n_runs
        self.progress = progress
        self.emu = emu
        self.logs: List[str] = []

    def log(self, msg: str) -> None:
        """Buffer log messages to avoid interleaving in parallel mode."""
        self.logs.append(msg)

    def get_logs(self) -> str:
        """Return buffered logs as a single string."""
        return "\n".join(self.logs)

    @classmethod
    def prepare(cls) -> None:
        """Ensure dependencies are built. Run BEFORE parallel execution."""
        if hasattr(cls, "BIN_PATH") and not os.path.exists(cls.BIN_PATH):
            print(f"Building {cls.name}...")
            result = subprocess.run(
                ["make", "artifact"],
                capture_output=True,
                text=True,
                check=False,
            )
            if result.returncode != 0:
                raise RuntimeError(
                    f"Failed to build {cls.name}\n"
                    f"stdout: {result.stdout[:500]}\nstderr: {result.stderr[:500]}"
                )
            if not os.path.exists(cls.BIN_PATH):
                raise RuntimeError(f"{cls.name} not found at {cls.BIN_PATH}")

    @abstractmethod
    def guest_args(self) -> List[str]:
        """Return the arguments passed to the guest program."""
        raise NotImplementedError

    @abstractmethod
    def parse(self, stdout: str) -> float:
        """Extract the score from the guest output."""
        raise NotImplementedError

    def start(self, emu: str) -> subprocess.Popen:
        """Launch one iteration on the given emulator without waiting."""
        return subprocess.Popen(
            [emu, "-q", self.BIN_PATH, *self.guest_args()],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

    def collect(self, proc: subprocess.Popen, emu: str) -> float:
        """Wait for an iteration launched by start() and return its score."""
        try:
            stdout, stderr = proc.communicate(timeout=TIMEOUT_SECONDS)
        except TimeoutExpired:
            proc.kill()
            proc.communicate()  # Clean up buffers
            raise RuntimeError(
                f"{self.name} timed out after {TIMEOUT_SECONDS} seconds "
                f"on {emu}"
            )

        if proc.returncode != 0:
            raise RuntimeError(
                f"{self.name} failed on {emu} (exit {proc.returncode})\n"
                f"stdout: {stdout[:500]}\nstderr: {stderr[:500]}"
            )

        value = self.parse(stdout)
        if value <= 0:
            raise RuntimeError(f"Invalid {self.unit} value on {emu}: {value}")
        return value

    def run_single(self) -> float:
        """Run a single benchmark iteration and return the result."""
        return self.collect(self.start(self.emu), self.emu)

    def run_pair(
        self, baseline: str, baseline_first: bool, concurrent: bool
    ) -> Tuple[float, float]:
        """Run the baseline and self.emu once each. Returns both scores."""
        # Whichever runs first can win a better CPU or a warmer cache, so
        # callers alternate the order to cancel that advantage out.
        emus = [baseline, self.emu] if baseline_first else [self.emu, baseline]
        if concurrent:
            # Neither process may outlive a failure of the other.
            procs: List[subprocess.Popen] = []
            try:
                for emu in emus:
                    procs.append(self.start(emu))
                first, second = (
                    self.collect(proc, emu) for emu, proc in zip(emus, procs)
                )
            finally:
                for proc in procs:
                    if proc.poll() is None:
                        proc.kill()
                        proc.communicate()
        else:
            first, second = (self.collect(self.start(emu), emu) for emu in emus)
        return (first, second) if baseline_first else (second, first)

    def run(self) -> Tuple[float, float, List[float], int]:
        """Run the full benchmark suite. Returns (mean, stdev, filtered_values, actual_runs)."""
        bench_key = self.name.lower()
        bench_start = time.monotonic()

        # Validation run (also serves as timing reference)
        self.log(f"Validating {self.name}...")
        if self.progress:
            self.progress.update(bench_key, 0, "running")
        run_start = time.monotonic()
        first_value = self.run_single()
        run_elapsed = time.monotonic() - run_start
        self.log(f"{self.name} validation passed ({run_elapsed:.1f}s)")

        # Adaptive run count based on single run time
        actual_runs = self.n_runs
        if run_elapsed > SLOW_THRESHOLD_SECONDS:
            self.log(
                f"Warning: {self.name} took {run_elapsed:.1f}s (>{SLOW_THRESHOLD_SECONDS}s), "
                "using single run only"
            )
            actual_runs = 1

        values = [first_value]  # Include validation result
        for i in range(1, actual_runs):
            # Check time budget before starting next run.
            # Note: uses validation run time as estimate; assumes runs are similar.
            total_elapsed = time.monotonic() - bench_start
            remaining = MAX_BENCHMARK_SECONDS - total_elapsed
            if remaining < run_elapsed:
                self.log(
                    f"Time budget: {total_elapsed:.0f}s elapsed, "
                    f"stopping after {len(values)} runs"
                )
                break
            self.log(f"Running {self.name} benchmark - Run #{i + 1}")
            if self.progress:
                self.progress.update(bench_key, i + 1, "running")
            values.append(self.run_single())

        if self.progress:
            self.progress.update(bench_key, len(values), "done")

        avg, stdev, filtered = self.calculate_stats(values)
        self.log("-" * 40)
        self.log(
            f"{self.name}: {avg:.3f} ± {stdev:.3f} {self.unit} "
            f"({len(filtered)}/{len(values)} valid runs)"
        )
        self.log("-" * 40)

        return avg, stdev, filtered, len(values)

    def compare(
        self, baseline: str, max_seconds: float, concurrent: bool
    ) -> dict:
        """Run pairs against the baseline and summarize them."""
        bench_key = self.name.lower()
        bench_start = time.monotonic()
        pairs: List[Tuple[float, float]] = []
        pair_elapsed = 0.0
        for i in range(self.n_runs):
            # Keep at least two pairs, which the interval needs, then stop
            # before a pair would overrun the budget.
            total_elapsed = time.monotonic() - bench_start
            if i >= 2 and max_seconds - total_elapsed < pair_elapsed:
                self.log(
                    f"Time budget: {total_elapsed:.0f}s elapsed, "
                    f"stopping after {len(pairs)} pairs"
                )
                break
            if self.progress:
                self.progress.update(bench_key, i + 1, "running")
            pair_start = time.monotonic()
            base, cand = self.run_pair(baseline, i % 2 == 0, concurrent)
            pair_elapsed = max(pair_elapsed, time.monotonic() - pair_start)
            self.log(
                f"Pair #{i + 1}: baseline {base:.3f}, candidate {cand:.3f} "
                f"{self.unit} ({pair_elapsed:.1f}s)"
            )
            pairs.append((base, cand))

        if self.progress:
            self.progress.update(bench_key, len(pairs), "done")
        return summarize_pairs(self.name, self.unit, pairs)

    def calculate_stats(
        self, values: List[float]
    ) -> Tuple[float, float, List[float]]:
        """Filter outliers using median-based 2-sigma rule. Returns (mean, stdev, filtered)."""
        if not values:
            return 0.0, 0.0, []

        n = len(values)
        median = statistics.median(values)
        stdev_val = statistics.stdev(values) if n > 1 else 0.0

        # Filter values within 2 standard deviations of median
        filtered = [x for x in values if abs(x - median) <= 2.0 * stdev_val]

        if len(filtered) < 2:
            self.log("Warning: Too many outliers filtered, using all results")
            filtered = values

        final_mean = statistics.mean(filtered)
        final_stdev = statistics.stdev(filtered) if len(filtered) > 1 else 0.0

        return final_mean, final_stdev, filtered


def summarize_pairs(
    name: str, unit: str, pairs: List[Tuple[float, float]]
) -> dict:
    """Estimate the candidate/baseline ratio from paired scores.

    Every metric here is bigger-is-better, so a ratio above 1 means the
    candidate is faster. The estimate is the geometric mean of the per-pair
    ratios, with a 95% Student's t interval on their logarithms. No pair is
    discarded: dropping the ones that look like outliers, then computing the
    interval as if none had been dropped, makes it too narrow, and so reports
    changes where there are none.
    """
    logs = [math.log(cand / base) for base, cand in pairs]
    n = len(logs)
    mean = statistics.mean(logs)
    half = t_975(n - 1) * statistics.stdev(logs) / math.sqrt(n)
    return {
        "name": name,
        "unit": unit,
        "baseline": [base for base, _ in pairs],
        "candidate": [cand for _, cand in pairs],
        "change": math.exp(mean) - 1,
        "ci": (math.exp(mean - half) - 1, math.exp(mean + half) - 1),
    }


def verdict(result: dict, threshold: float) -> str:
    """Classify a comparison; changes below threshold (a fraction) are noise."""
    low, high = result["ci"]
    if low > 0 and result["change"] >= threshold:
        return "faster"
    if high < 0 and result["change"] <= -threshold:
        return "slower"
    return "no significant change"


def format_comparison(
    results: List[dict],
    label: str,
    threshold: float,
    host: str,
    concurrent: bool,
) -> str:
    """Render comparison results as a Markdown table."""

    def pct(x: float) -> str:
        return f"{x * 100:+.2f}%"

    title = (
        f"Benchmark comparison ({label})" if label else "Benchmark comparison"
    )
    lines = [
        f"### {title}",
        "",
        "| Benchmark | Baseline (median) | Candidate (median) "
        "| Change | 95% CI | Pairs | Verdict |",
        "| --- | ---: | ---: | ---: | :---: | ---: | --- |",
    ]
    for r in results:
        base = statistics.median(r["baseline"])
        cand = statistics.median(r["candidate"])
        low, high = r["ci"]
        lines.append(
            f"| {r['name']} | {base:.3f} {r['unit']} | {cand:.3f} {r['unit']} "
            f"| {pct(r['change'])} | [{pct(low)}, {pct(high)}] "
            f"| {len(r['baseline'])} | {r['verdict']} |"
        )
    lines += [
        "",
        f"Baseline and candidate ran "
        f"{'concurrently' if concurrent else 'alternately'} on {host}. "
        "Changes smaller "
        f"than {threshold * 100:g}% or whose interval spans zero are "
        "reported as no significant change.",
    ]
    return "\n".join(lines) + "\n"


@register_benchmark("dhrystone")
class DhrystoneBenchmark(Benchmark):
    """Dhrystone benchmark measuring DMIPS."""

    name = "Dhrystone"
    unit = "DMIPS"
    BIN_PATH = "build/riscv32/dhrystone"

    def guest_args(self) -> List[str]:
        return []

    def parse(self, stdout: str) -> float:
        match = re.search(r"([0-9]+(?:\.[0-9]+)?) DMIPS", stdout)
        if not match:
            raise RuntimeError(f"Failed to parse DMIPS:\n{stdout[:500]}")
        return float(match.group(1))


@register_benchmark("coremark")
class CoreMarkBenchmark(Benchmark):
    """CoreMark benchmark measuring iterations/sec."""

    name = "CoreMark"
    unit = "iterations/sec"
    BIN_PATH = "build/riscv32/coremark"

    ITERATIONS = 30000

    def guest_args(self) -> List[str]:
        return ["0x0", "0x0", "0x66", str(self.ITERATIONS), "7", "1", "2000"]

    def parse(self, stdout: str) -> float:
        # A checksum mismatch means the emulator computed a wrong result, which
        # must never be reported as a score. CoreMark also flags runs shorter
        # than 10 seconds as errors; that rule is about timer resolution, so
        # it is not treated as a failure here.
        crc_error = re.search(r"ERROR! \w+ crc .*", stdout)
        if crc_error:
            raise RuntimeError(f"CoreMark validation failed: {crc_error[0]}")

        match = re.search(r"Iterations/Sec\s*:\s*([0-9]+(?:\.[0-9]+)?)", stdout)
        if not match:
            raise RuntimeError(
                f"Failed to parse Iterations/Sec:\n{stdout[:500]}"
            )
        return float(match.group(1))


def run_task(
    bench_name: str,
    n_runs: int,
    progress: Optional[ProgressIndicator],
    emu: str,
    action: Callable[["Benchmark"], dict],
) -> Tuple[str, dict, List[str], Optional[Exception]]:
    """Apply action to one benchmark. Returns (name, result, logs, error)."""
    bench = None
    try:
        bench = _BENCHMARK_REGISTRY[bench_name](n_runs, progress, emu)
        return bench_name, action(bench), bench.logs, None
    except Exception as e:
        if progress:
            progress.update(bench_name, 0, "failed")
        # Preserve logs even on failure for debugging
        logs = bench.logs if bench else []
        return bench_name, {}, logs, e


def measure(bench: "Benchmark") -> dict:
    """Run a benchmark on its own emulator and summarize the runs."""
    avg, stdev, _, actual_runs = bench.run()
    return {
        "name": bench.name,
        "unit": bench.unit,  # Store raw unit for proper formatting
        "value": round(avg, 3),
        "stdev": round(stdev, 3),
        "runs": actual_runs,  # Actual number of runs completed
    }


def check_executable(path: str) -> None:
    """Exit with a message unless path is an executable file."""
    if not (os.path.isfile(path) and os.access(path, os.X_OK)):
        print(
            f"Error: {path} not found. Please compile first",
            file=sys.stderr,
        )
        sys.exit(1)


def prepare_benchmarks(selected: List[str], quiet: bool) -> None:
    """Validate selections and build their binaries before any run."""
    registry = get_registered_benchmarks()
    for name in selected:
        if name not in registry:
            print(f"Error: Unknown benchmark '{name}'", file=sys.stderr)
            print(
                f"Available: {', '.join(sorted(registry.keys()))}",
                file=sys.stderr,
            )
            sys.exit(1)

    # Build all binaries sequentially before running benchmarks
    if not quiet:
        print("Preparing benchmarks...")
    try:
        for name in selected:
            registry[name].prepare()
    except RuntimeError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
    if not quiet:
        print("Preparation complete.\n")


def print_logs(selected: List[str], all_logs: Dict[str, List[str]]) -> None:
    """Print buffered logs in user-specified order."""
    for name in selected:
        if all_logs.get(name):
            print(f"\n[{name}]")
            for line in all_logs[name]:
                print(f"  {line}")


def report_errors(errors: Dict[str, Exception]) -> None:
    """Print every error and exit with failure if there was any."""
    for name, error in errors.items():
        print(f"\nError in {name}: {error}", file=sys.stderr)
    if errors:
        sys.exit(1)


def execute(
    selected: List[str],
    n_runs: int,
    quiet: bool,
    emu: str,
    action: Callable[["Benchmark"], dict],
    parallel: int = 0,
) -> List[dict]:
    """Apply action to each selected benchmark; exit if any fails.

    Returns the results in user-specified order.
    """
    check_executable(emu)
    prepare_benchmarks(selected, quiet)

    # Create and start progress indicator
    progress = ProgressIndicator(selected, n_runs, quiet=quiet)
    progress.start()

    results: Dict[str, dict] = {}
    all_logs: Dict[str, List[str]] = {}
    errors: Dict[str, Exception] = {}

    def record(name, result, logs, error) -> None:
        all_logs[name] = logs
        if error:
            errors[name] = error
        else:
            results[name] = result

    if parallel > 0 and len(selected) > 1:
        workers = min(parallel, len(selected))
        if not quiet:
            print(
                f">>> Running {len(selected)} benchmarks in parallel ({workers} workers) <<<"
            )
        with ThreadPoolExecutor(max_workers=workers) as executor:
            futures = [
                executor.submit(run_task, name, n_runs, progress, emu, action)
                for name in selected
            ]
            for future in as_completed(futures):
                record(*future.result())
    else:
        for name in selected:
            record(*run_task(name, n_runs, progress, emu, action))

    progress.finish()

    # Print logs after spinner finishes to avoid garbled output
    if not quiet:
        print_logs(selected, all_logs)
    report_errors(errors)
    return [results[name] for name in selected]


def run_benchmarks(
    selected: List[str],
    output_json: Optional[str],
    n_runs: int,
    parallel: int = 0,
    quiet: bool = False,
    emu: str = EMU_PATH,
    label: str = "",
) -> None:
    """Run selected benchmarks, optionally in parallel."""
    start_time = time.monotonic()
    results = execute(selected, n_runs, quiet, emu, measure, parallel)
    elapsed = time.monotonic() - start_time

    # Output results in user-specified order. The names carry the label, so
    # each execution mode is tracked as its own series on the dashboard; the
    # unlabeled names are the interpreter's established series.
    host = host_description()
    print("\n" + "=" * 50)
    print("Benchmark results")
    print("=" * 50)
    ordered_results = []
    for r in results:
        ordered_results.append(
            {
                "name": f"{r['name']} ({label})" if label else r["name"],
                "unit": r["unit"],
                "value": r["value"],
                "range": f"± {r['stdev']}",
                "extra": f"{r['runs']} runs on {host}",
            }
        )
        print(
            f"  {r['name']}: {r['value']} ± {r['stdev']} {r['unit']} ({r['runs']} runs)"
        )
    print("=" * 50)
    print(f"  Total time: {elapsed:.1f}s")

    if output_json:
        with open(output_json, "w") as f:
            json.dump(ordered_results, f, indent=4)
        if not quiet:
            print(f"Saved: {output_json}")


def compare_benchmarks(
    selected: List[str],
    *,
    output_json: Optional[str],
    markdown: Optional[str],
    n_runs: int,
    quiet: bool,
    emu: str,
    baseline: str,
    label: str,
    threshold: float,
    max_seconds: float,
    concurrent: bool,
    parallel: int = 0,
) -> None:
    """Compare two emulator binaries on the selected benchmarks.

    A concurrent pair occupies two CPUs, so running several benchmarks in
    parallel needs twice as many CPUs as workers; both members of each pair
    still share whatever interference the other pairs cause.
    """
    check_executable(baseline)
    results = execute(
        selected,
        n_runs,
        quiet,
        emu,
        lambda bench: bench.compare(baseline, max_seconds, concurrent),
        parallel,
    )
    for r in results:
        r["verdict"] = verdict(r, threshold)

    table = format_comparison(
        results, label, threshold, host_description(), concurrent
    )
    print("\n" + table)

    if markdown:
        with open(markdown, "w") as f:
            f.write(table)
    if output_json:
        with open(output_json, "w") as f:
            json.dump({"label": label, "results": results}, f, indent=4)


def parse_benchmarks(args: List[str]) -> List[str]:
    """Parse benchmark arguments, preserving order."""
    if not args:
        # Default: all registered benchmarks in registration order
        return list(_BENCHMARK_REGISTRY.keys())

    # Handle comma-separated and space-separated inputs
    result = []
    for arg in args:
        for part in arg.split(","):
            name = part.strip().lower()
            if name and name not in result:  # Preserve order, no duplicates
                result.append(name)
    return result


def main():
    parser = argparse.ArgumentParser(
        description="Run benchmarks for rv32emu",
        epilog=f"Available benchmarks: {', '.join(sorted(_BENCHMARK_REGISTRY.keys()))}",
    )
    parser.add_argument(
        "--json",
        nargs="?",
        const="benchmark_output.json",
        metavar="FILE",
        help="Write results as JSON (default file: benchmark_output.json)",
    )
    parser.add_argument(
        "--parallel",
        type=int,
        metavar="N",
        help="Run benchmarks in parallel with N workers (default: sequential)",
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Quiet mode for CI (no progress indicator)",
    )
    parser.add_argument(
        "--runs",
        type=int,
        default=DEFAULT_RUNS,
        help=f"Number of runs (or pairs with --baseline) per benchmark "
        f"(default: {DEFAULT_RUNS})",
    )
    parser.add_argument(
        "--emu",
        default=EMU_PATH,
        metavar="PATH",
        help=f"Emulator binary to measure (default: {EMU_PATH})",
    )
    parser.add_argument(
        "--label",
        default="",
        help="Execution mode label, such as T1C; appended to result names",
    )
    parser.add_argument(
        "--baseline",
        metavar="PATH",
        help="Compare --emu against this emulator binary instead",
    )
    parser.add_argument(
        "--threshold",
        type=float,
        default=DEFAULT_THRESHOLD,
        metavar="PCT",
        help="Smallest change in percent that --baseline reports as "
        f"significant (default: {DEFAULT_THRESHOLD:g})",
    )
    parser.add_argument(
        "--max-seconds",
        type=float,
        default=MAX_BENCHMARK_SECONDS,
        metavar="SEC",
        help="Time budget per benchmark with --baseline; at least two pairs "
        f"always run (default: {MAX_BENCHMARK_SECONDS})",
    )
    parser.add_argument(
        "--schedule",
        choices=("concurrent", "interleaved"),
        default="concurrent",
        help="With --baseline, run each pair at the same time, or one after "
        "the other in alternating order (default: concurrent)",
    )
    parser.add_argument(
        "--markdown",
        metavar="FILE",
        help="With --baseline, also write the comparison table to FILE",
    )
    parser.add_argument(
        "benchmarks",
        nargs="*",
        metavar="BENCH",
        help="Benchmarks to run (comma or space-separated)",
    )

    args = parser.parse_args()

    # Validate --runs
    if args.runs < 1:
        parser.error("--runs must be at least 1")

    selected = parse_benchmarks(args.benchmarks)
    if not selected:
        print("Error: No benchmarks specified", file=sys.stderr)
        sys.exit(1)

    if args.baseline:
        if args.parallel and args.schedule != "concurrent":
            parser.error("--parallel needs the concurrent schedule")
        if args.runs < 2:
            parser.error("--baseline needs --runs of at least 2")
        if args.max_seconds <= 0:
            parser.error("--max-seconds must be positive")
        compare_benchmarks(
            selected,
            output_json=args.json,
            markdown=args.markdown,
            n_runs=args.runs,
            quiet=args.quiet,
            emu=args.emu,
            baseline=args.baseline,
            label=args.label,
            threshold=args.threshold / 100,
            max_seconds=args.max_seconds,
            concurrent=args.schedule == "concurrent",
            parallel=args.parallel or 0,
        )
    else:
        if args.markdown:
            parser.error("--markdown requires --baseline")
        run_benchmarks(
            selected,
            args.json,
            args.runs,
            parallel=args.parallel or 0,
            quiet=args.quiet,
            emu=args.emu,
            label=args.label,
        )


if __name__ == "__main__":
    main()
