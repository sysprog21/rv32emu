#!/usr/bin/env python3
"""
Unified benchmark runner for rv32emu.

Benchmarks are registered via the @register_benchmark decorator.
Supports parallel execution while preserving user-specified output order.
"""

import subprocess
import re
import statistics
import os
import sys
import json
import argparse
import threading
import time
from abc import ABC, abstractmethod
from concurrent.futures import ThreadPoolExecutor, as_completed
from subprocess import TimeoutExpired
from typing import ClassVar, Dict, List, Optional, Tuple, Type

# Configuration
EMU_PATH = "build/rv32emu"
DEFAULT_RUNS = 5  # Balance, providing reasonable statistics
TIMEOUT_SECONDS = 600  # 10 min timeout per run (safety limit)
SLOW_THRESHOLD_SECONDS = 300  # If single run > 5 min, use only 1 run
MAX_BENCHMARK_SECONDS = 600  # 10 min max total time per benchmark

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


class Benchmark(ABC):
    """Abstract base class for all benchmarks."""

    name: ClassVar[str]
    unit: ClassVar[str]
    BIN_PATH: ClassVar[str]

    def __init__(
        self, n_runs: int, progress: Optional[ProgressIndicator] = None
    ):
        self.n_runs = n_runs
        self.progress = progress
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
    def run_single(self) -> float:
        """Run a single benchmark iteration and return the result."""
        raise NotImplementedError

    def validate(self) -> float:
        """Run validation before benchmark. Returns the result for reuse."""
        return self.run_single()

    def run(self) -> Tuple[float, float, List[float], int]:
        """Run the full benchmark suite. Returns (mean, stdev, filtered_values, actual_runs)."""
        bench_key = self.name.lower()
        bench_start = time.monotonic()

        # Validation run (also serves as timing reference)
        self.log(f"Validating {self.name}...")
        if self.progress:
            self.progress.update(bench_key, 0, "running")
        run_start = time.monotonic()
        first_value = self.validate()
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


@register_benchmark("dhrystone")
class DhrystoneBenchmark(Benchmark):
    """Dhrystone benchmark measuring DMIPS."""

    name = "Dhrystone"
    unit = "DMIPS"
    BIN_PATH = "build/riscv32/dhrystone"

    def run_single(self) -> float:
        proc = subprocess.Popen(
            [EMU_PATH, "-q", self.BIN_PATH],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            stdout, stderr = proc.communicate(timeout=TIMEOUT_SECONDS)
        except TimeoutExpired:
            proc.kill()
            proc.communicate()  # Clean up buffers
            raise RuntimeError(
                f"Dhrystone timed out after {TIMEOUT_SECONDS} seconds"
            )

        if proc.returncode != 0:
            raise RuntimeError(
                f"Dhrystone failed (exit {proc.returncode})\n"
                f"stdout: {stdout[:500]}\nstderr: {stderr[:500]}"
            )

        match = re.search(r"([0-9]+(?:\.[0-9]+)?) DMIPS", stdout)
        if not match:
            raise RuntimeError(f"Failed to parse DMIPS:\n{stdout[:500]}")

        return float(match.group(1))

    def validate(self) -> float:
        dmips = self.run_single()
        if dmips <= 0:
            raise RuntimeError(f"Invalid DMIPS value: {dmips}")
        return dmips


@register_benchmark("coremark")
class CoreMarkBenchmark(Benchmark):
    """CoreMark benchmark measuring iterations/sec."""

    name = "CoreMark"
    unit = "iterations/sec"
    BIN_PATH = "build/riscv32/coremark"

    ITERATIONS = 30000

    def run_single(self) -> float:
        cmd = [
            EMU_PATH,
            "-q",
            self.BIN_PATH,
            "0x0",
            "0x0",
            "0x66",
            str(self.ITERATIONS),
            "7",
            "1",
            "2000",
        ]
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            stdout, stderr = proc.communicate(timeout=TIMEOUT_SECONDS)
        except TimeoutExpired:
            proc.kill()
            proc.communicate()  # Clean up buffers
            raise RuntimeError(
                f"CoreMark timed out after {TIMEOUT_SECONDS} seconds"
            )

        if proc.returncode != 0:
            raise RuntimeError(
                f"CoreMark failed (exit {proc.returncode})\n"
                f"stdout: {stdout[:500]}\nstderr: {stderr[:500]}"
            )

        match = re.search(r"Iterations/Sec\s*:\s*([0-9]+(?:\.[0-9]+)?)", stdout)
        if not match:
            raise RuntimeError(
                f"Failed to parse Iterations/Sec:\n{stdout[:500]}"
            )

        return float(match.group(1))


def run_benchmark_task(
    bench_name: str, n_runs: int, progress: Optional[ProgressIndicator] = None
) -> Tuple[str, dict, List[str], Optional[Exception]]:
    """Run a single benchmark. Returns (name, result, logs, error)."""
    bench = None
    try:
        bench_cls = _BENCHMARK_REGISTRY[bench_name]
        bench = bench_cls(n_runs, progress)
        avg, stdev, _, actual_runs = bench.run()
        result = {
            "name": bench.name,
            "unit": bench.unit,  # Store raw unit for proper formatting
            "value": round(avg, 3),
            "stdev": round(stdev, 3),
            "runs": actual_runs,  # Actual number of runs completed
        }
        return bench_name, result, bench.logs, None
    except Exception as e:
        if progress:
            progress.update(bench_name, 0, "failed")
        # Preserve logs even on failure for debugging
        logs = bench.logs if bench else []
        return bench_name, {}, logs, e


def run_benchmarks(
    selected: List[str],
    output_json: bool,
    n_runs: int,
    parallel: int = 0,
    quiet: bool = False,
) -> None:
    """Run selected benchmarks, optionally in parallel."""
    if not os.path.exists(EMU_PATH):
        print(
            f"Error: {EMU_PATH} not found. Please compile first",
            file=sys.stderr,
        )
        sys.exit(1)

    # Validate selections
    registry = get_registered_benchmarks()
    for name in selected:
        if name not in registry:
            print(f"Error: Unknown benchmark '{name}'", file=sys.stderr)
            print(
                f"Available: {', '.join(sorted(registry.keys()))}",
                file=sys.stderr,
            )
            sys.exit(1)

    # Prepare phase: build all binaries sequentially before running benchmarks
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

    # Create and start progress indicator
    progress = ProgressIndicator(selected, n_runs, quiet=quiet)
    progress.start()

    results: Dict[str, dict] = {}
    all_logs: Dict[str, List[str]] = {}
    errors: Dict[str, Exception] = {}

    start_time = time.monotonic()

    if parallel and parallel > 0 and len(selected) > 1:
        workers = min(parallel, len(selected))
        if not quiet:
            print(
                f">>> Running {len(selected)} benchmarks in parallel ({workers} workers) <<<"
            )
        with ThreadPoolExecutor(max_workers=workers) as executor:
            futures = {
                executor.submit(
                    run_benchmark_task, name, n_runs, progress
                ): name
                for name in selected
            }
            for future in as_completed(futures):
                name, result, logs, error = future.result()
                all_logs[name] = logs
                if error:
                    errors[name] = error
                else:
                    results[name] = result

        progress.finish()

        # Print logs in user-specified order after all complete (only if not quiet)
        if not quiet:
            for name in selected:
                if name in all_logs and all_logs[name]:
                    print(f"\n[{name}]")
                    for line in all_logs[name]:
                        print(f"  {line}")
    else:
        for name in selected:
            name, result, logs, error = run_benchmark_task(
                name, n_runs, progress
            )
            all_logs[name] = logs
            if error:
                errors[name] = error
            else:
                results[name] = result

        progress.finish()

        # Print logs after spinner finishes to avoid garbled output
        if not quiet:
            for name in selected:
                if name in all_logs and all_logs[name]:
                    print(f"\n[{name}]")
                    for line in all_logs[name]:
                        print(f"  {line}")

    elapsed = time.monotonic() - start_time

    # Report errors
    for name, error in errors.items():
        print(f"\nError in {name}: {error}", file=sys.stderr)

    if errors:
        sys.exit(1)

    # Output results in user-specified order
    print("\n" + "=" * 50)
    print("Benchmark results")
    print("=" * 50)
    ordered_results = []
    for name in selected:
        if name in results:
            r = results[name]
            ordered_results.append(
                {
                    "name": r["name"],
                    "unit": r["unit"],
                    "value": r["value"],
                    "runs": r["runs"],
                }
            )
            print(
                f"  {r['name']}: {r['value']} ± {r['stdev']} {r['unit']} ({r['runs']} runs)"
            )
    print("=" * 50)
    print(f"  Total time: {elapsed:.1f}s")

    if output_json:
        combined_file = "benchmark_output.json"
        with open(combined_file, "w") as f:
            json.dump(ordered_results, f, indent=4)
        if not quiet:
            print(f"Saved: {combined_file}")


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
        action="store_true",
        help="Output results to JSON files",
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
        help=f"Number of runs per benchmark (default: {DEFAULT_RUNS})",
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

    run_benchmarks(
        selected,
        args.json,
        args.runs,
        parallel=args.parallel or 0,
        quiet=args.quiet,
    )


if __name__ == "__main__":
    main()
