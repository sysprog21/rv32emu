#!/usr/bin/env python3
"""Compare pinned Zephyr fixed work with normal, single-vCPU QEMU TCG.

Example: python3 tests/zephyr_bench.py --build --rv32emu ./build/rv32emu
  --rv32emu-config build/.effective-config --opensbi /path/to/fw_jump.elf --out-dir /tmp/zbench

Use --mode interpreter or --mode t2c for informational correctness reports.
Fixed-work scores measure a warm region after one in-guest workload pass;
--warmups discards additional fresh-process pairs before retained samples.
The manifest comes from .ci/build-zephyr-bench.sh. No guest timer or DMIPS
value enters the score. Logs, commands, image hashes and paired samples are
saved in out-dir. CPU affinity applies equally to both emulator processes.

--diagnostics FILE accepts a JSON array of separately measured components:
[{"case_id":"allocation","component_seconds":2,"total_seconds":5,
  "component_scope":"host-wall","total_scope":"host-wall",
  "source_evidence":"separate profile measurement and scope",
  "hypothetical_speedup":2,"attribution":"non-overlapping-subset"}]
Predictions remain conditional and do not change the measured score.

Optional profiles run after all scoring. --profile-case computation
--profile-backend cpu-sampler --profile-runs 50 compiles a local preload probe
with --profile-cc (default CC or cc); it requires Linux x86_64, a direct dynamic
ELF binary, and single-threaded execution with T2C disabled. Reuse a debug-symbol
build with normal optimization for host symbol names. No privileges change.
The signal-controlled window follows observed START/END; marker/signal latency,
handler overhead, user+system CPU charging and periodic sampling alias/coalescing
limit attribution. Boundary thread checks cannot exclude transient threads.
Library, JIT and unknown PCs stay in the denominator. These are symbol-only
interrupted-PC CPU fractions, not exclusive function time, host-wall fractions,
or causal evidence. See cpu-profiles.json for full scope and per-run validation.
Use a fresh output directory to preserve each CPU profiling experiment.
"""
import argparse
import errno
import hashlib
import json
import os
from pathlib import Path
import pty
import select
import signal
import shutil
import subprocess
import sys
import time

PREFIX = "RV32EMU_BENCH_"
GREETING = "*** Booting Zephyr OS"
MANDATORY_CASES = (
    "boot",
    "computation",
    "scheduling",
    "allocation",
    "heap-regression",
    "serialization",
)


class BenchError(RuntimeError):
    pass


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class ConsoleProtocol:
    """Accept one ordered, complete run with the declared amount of work."""

    def __init__(self, case):
        self.case = case
        self.stage = 0
        self.greeting = False
        self.booted = None
        self.ready = self.started = self.ended = None

    def feed(self, line, observed):
        line = line.rstrip("\r\n")
        if line.startswith(GREETING):
            self.greeting = True
            if self.case["id"] != "boot" and self.booted is None:
                self.booted = observed
        if (
            self.case["id"] == "boot"
            and line.startswith("Hello World!")
            and self.booted is None
        ):
            if not self.greeting:
                raise BenchError(
                    "application greeting preceded the Zephyr boot banner"
                )
            self.booted = observed
        if not line.startswith(PREFIX):
            return False
        if (
            self.case["id"] == "boot"
            and self.stage == 0
            and self.booted is None
        ):
            raise BenchError(
                "boot workload began before the application greeting"
            )
        case_id = self.case["id"]
        count = self.case["iterations"]
        checksum = self.case["expected_checksum"]
        expected = [
            f"{PREFIX}WARMUP case={case_id} iterations={count} checksum={checksum}",
            f"{PREFIX}READY case={case_id}",
            f"{PREFIX}START case={case_id} iterations={count}",
            f"{PREFIX}END case={case_id} iterations={count} checksum={checksum} status=PASS",
        ]
        if self.stage >= len(expected) or line != expected[self.stage]:
            raise BenchError(f"unexpected benchmark marker: {line!r}")
        if self.stage == 1:
            if not self.greeting:
                raise BenchError("READY preceded the Zephyr boot greeting")
            self.ready = observed
        elif self.stage == 2:
            self.started = observed
        elif self.stage == 3:
            self.ended = observed
            if self.ended <= self.started and self.case["id"] != "boot":
                raise BenchError(
                    "work markers have no measurable host interval"
                )
        self.stage += 1
        return self.stage == 4


def stop_group(process):
    # Descendants may still own the serial pipe after the parent exits.
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=1)
    except subprocess.TimeoutExpired:
        pass
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait()


def observe(
    command, case, timeout, cpu, log_path, *, environment=None, observer=None
):
    protocol = ConsoleProtocol(case)
    begin = time.perf_counter()
    master, slave = pty.openpty()
    original_affinity = os.sched_getaffinity(0)
    try:
        os.sched_setaffinity(0, {cpu})
        process = subprocess.Popen(
            command,
            stdin=subprocess.DEVNULL,
            stdout=slave,
            stderr=slave,
            start_new_session=True,
            env=environment,
        )
    except BaseException:
        os.close(master)
        raise
    finally:
        os.close(slave)
        os.sched_setaffinity(0, original_affinity)
    pending = b""
    try:
        if observer is not None:
            observer.launched(process)
        with Path(log_path).open("wb") as log:
            while True:
                remaining = timeout - (time.perf_counter() - begin)
                if remaining <= 0:
                    raise BenchError(f"timeout after {timeout:g} seconds")
                readable, _, _ = select.select(
                    [master], [], [], min(0.1, remaining)
                )
                if not readable:
                    continue
                try:
                    data = os.read(master, 65536)
                except OSError as error:
                    if error.errno != errno.EIO:
                        raise
                    data = b""
                if not data:
                    raise BenchError(
                        f"console closed before verification (exit {process.poll()})"
                    )
                log.write(data)
                log.flush()
                observed = time.perf_counter()
                pending += data
                if len(pending) > 1024 * 1024:
                    raise BenchError("console line exceeds one MiB")
                while b"\n" in pending:
                    line, pending = pending.split(b"\n", 1)
                    previous_stage = protocol.stage
                    done = protocol.feed(
                        line.decode("utf-8", errors="replace"), observed
                    )
                    if (
                        observer is not None
                        and previous_stage == 2
                        and protocol.stage == 3
                    ):
                        observer.start(process)
                    if done:
                        if observer is not None:
                            observer.finish(process)
                        return {
                            "boot_seconds": protocol.booted - begin,
                            "work_seconds": protocol.ended - protocol.started,
                            "command": command,
                        }
    finally:
        stop_group(process)
        os.close(master)


def require_inventory(manifest):
    """Keep omitted mandatory work visible in targeted development reports."""
    cases = manifest["cases"]
    ids = [case["id"] for case in cases]
    if len(ids) != len(set(ids)):
        raise BenchError("duplicate case IDs in manifest")
    omitted = []
    relabeled = []
    for case_id in MANDATORY_CASES:
        if case_id not in ids:
            cases.append(
                {
                    "id": case_id,
                    "status": "untested",
                    "mandatory": True,
                    "reason": "mandatory case omitted from input manifest",
                }
            )
            omitted.append(case_id)
        else:
            case = next(case for case in cases if case["id"] == case_id)
            if not case.get("mandatory", False):
                relabeled.append(case_id)
            case["mandatory"] = True
    manifest.setdefault("provenance", {})["suite_inventory"] = {
        "mandatory_cases": list(MANDATORY_CASES),
        "omitted_cases": omitted,
        "restored_mandatory_flags": relabeled,
        "partial": bool(omitted),
        "boot_endpoint": "first observed boot application Hello World! before warmup",
        "timing_limit": "host serial observation includes console delivery latency",
    }


def command_for(engine, case, args, base, dummy):
    image = case[engine]
    if engine == "rv32emu":
        return [
            str(args.rv32emu),
            "-k",
            str((base / image["raw"]).resolve()),
            "-x",
            f"vblk:{dummy},rootfs",
        ]
    return [
        args.qemu,
        "-accel",
        "tcg,thread=single",
        "-machine",
        "virt",
        "-cpu",
        "rv32",
        "-m",
        "256",
        "-smp",
        "1",
        "-bios",
        str(args.opensbi),
        "-kernel",
        str((base / image["elf"]).resolve()),
        "-display",
        "none",
        "-monitor",
        "none",
        "-serial",
        "stdio",
    ]


def verify_engine(args):
    config = args.rv32emu_config.read_text()
    if args.rv32emu_config.stat().st_mtime > args.rv32emu.stat().st_mtime:
        raise BenchError(
            "effective configuration is newer than the executable; rebuild"
        )
    try:
        result = subprocess.run(
            ["nm", str(args.rv32emu)], capture_output=True, text=True
        )
        symbols = result.stdout
        # LTO can inline or rename the function, so absence proves nothing.
        jit = (
            True
            if result.returncode == 0
            and any(
                line.split()[-1:] == ["jit_translate"]
                for line in symbols.splitlines()
            )
            else None
        )
    except OSError:
        jit = None
    expected = args.mode != "interpreter"
    expected_t2c = args.mode == "t2c"
    if (
        "CONFIG_SYSTEM=y" not in config
        or ("CONFIG_JIT=y" in config) != expected
        or (jit is not None and jit != expected)
        or ("CONFIG_T2C=y" in config) != expected_t2c
        or ("CONFIG_INTERPRETER_ONLY=y" in config) != (not expected)
    ):
        raise BenchError(
            "effective executable symbols/config do not match the requested system mode"
        )
    return {
        "binary_sha256": sha256(args.rv32emu),
        "config_sha256": sha256(args.rv32emu_config),
        "effective_config": config,
        "effective_config_path": str(args.rv32emu_config.resolve()),
        "jit_translate_symbol": jit,
        "runtime_jit_execution": "unverified",
        "mode": {"primary": "T1", "interpreter": "interpreter", "t2c": "T2C"}[
            args.mode
        ],
    }


def qualify_mode(report, mode):
    """Keep nonprimary timings informational while requiring complete correctness."""
    report["primary_goal_passed"] = (
        mode == "primary" and report["status"] == "passed"
    )
    report["performance_qualification"] = (
        "primary" if mode == "primary" else "informational"
    )
    if mode == "primary":
        return report
    outcomes = {case["id"]: case for case in report["cases"]}
    required = set(MANDATORY_CASES) | {
        case["id"] for case in report["cases"] if case["mandatory"]
    }
    complete = all(
        case_id in outcomes
        and outcomes[case_id]["correctness"] == "passed"
        and outcomes[case_id]["status"]
        in ("passed", "slower", "below-target", "inconclusive")
        for case_id in required
    )
    report["correctness_status"] = "passed" if complete else "not-passed"
    report["status"] = "informational" if complete else "not-passed"
    report["rule"] = (
        "Informational mode: all mandatory cases require correct results and "
        "all planned pairs; timing ratios do not qualify the primary goal"
    )
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("build/zephyr/bench/manifest.json"),
    )
    parser.add_argument("--build", action="store_true")
    parser.add_argument(
        "--coremark-dir",
        type=Path,
        help="official pinned CoreMark checkout passed to builder",
    )
    parser.add_argument(
        "--diagnostics",
        type=Path,
        help="JSON file of measured components for conditional Amdahl predictions",
    )
    parser.add_argument(
        "--profile-case",
        action="append",
        default=[],
        metavar="ID",
        help="after scoring, profile this candidate case separately (repeatable)",
    )
    parser.add_argument(
        "--profile-backend", choices=("perf", "cpu-sampler"), default="perf"
    )
    parser.add_argument(
        "--profile-runs",
        type=int,
        default=1,
        help="separate profile runs per selected case (default: 1)",
    )
    parser.add_argument(
        "--profile-cc",
        default=os.environ.get("CC", "cc"),
        help="host C compiler for optional cpu-sampler (default: CC or cc)",
    )
    parser.add_argument(
        "--tls",
        choices=("y", "n"),
        default="y",
        help="forward native TLS setting to the guest builder (default: y)",
    )
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--rv32emu", type=Path, default=Path("build/rv32emu"))
    parser.add_argument(
        "--rv32emu-config",
        type=Path,
        help="default: .effective-config next to rv32emu",
    )
    parser.add_argument("--qemu", default="qemu-system-riscv32")
    parser.add_argument("--opensbi", type=Path, required=True)
    parser.add_argument(
        "--out-dir", type=Path, default=Path("build/zephyr-bench-results")
    )
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--cpu", type=int, default=min(os.sched_getaffinity(0)))
    parser.add_argument(
        "--mode", choices=("primary", "interpreter", "t2c"), default="primary"
    )
    args = parser.parse_args(argv)
    if (
        args.runs < 2
        or args.warmups < 0
        or args.timeout <= 0
        or args.jobs < 1
        or args.profile_runs < 1
    ):
        parser.error(
            "runs must be >=2, warmups >=0, and timeout/jobs/profile-runs positive"
        )
    if len(set(args.profile_case)) != len(args.profile_case):
        parser.error("profile cases must be unique")
    if args.cpu not in os.sched_getaffinity(0):
        parser.error("CPU is outside the current affinity set")
    args.rv32emu = args.rv32emu.resolve()
    if args.rv32emu_config is None:
        args.rv32emu_config = args.rv32emu.parent / ".effective-config"
    args.opensbi = args.opensbi.resolve()
    from system_bench import validate_output_dir

    args.out_dir = validate_output_dir(args.out_dir)
    diagnostics = (
        json.loads(args.diagnostics.read_text()) if args.diagnostics else None
    )
    if diagnostics is not None and not isinstance(diagnostics, list):
        raise BenchError("diagnostics must be a JSON array")
    args.out_dir.mkdir(parents=True, exist_ok=True)
    if args.build:
        builder = (
            Path(__file__).resolve().parents[1] / ".ci/build-zephyr-bench.sh"
        )
        build_command = [
            str(builder),
            "--output",
            str(args.manifest.parent.resolve()),
            "--jobs",
            str(args.jobs),
            "--tls",
            args.tls,
        ]
        if args.coremark_dir:
            build_command += [
                "--coremark-dir",
                str(args.coremark_dir.resolve()),
            ]
        subprocess.run(build_command, check=True)
    manifest = json.loads(args.manifest.read_text())
    if manifest.get("schema_version") != 1:
        raise BenchError("unsupported manifest schema")
    require_inventory(manifest)
    known_cases = {case["id"]: case for case in manifest["cases"]}
    unknown_profiles = set(args.profile_case) - set(known_cases)
    if unknown_profiles:
        raise BenchError(f"unknown profile cases: {sorted(unknown_profiles)}")
    provenance = manifest.setdefault("provenance", {})
    provenance["candidate"] = verify_engine(args)
    qemu_path = shutil.which(args.qemu)
    if qemu_path is None:
        raise BenchError(f"QEMU executable not found: {args.qemu}")
    version = subprocess.run(
        [qemu_path, "--version"],
        capture_output=True,
        text=True,
        check=True,
        timeout=10,
    )
    provenance["reference"] = {
        "executable": str(Path(qemu_path).resolve()),
        "binary_sha256": sha256(qemu_path),
        "version": version.stdout.strip(),
        "version_command": [qemu_path, "--version"],
    }
    provenance["reference_policy"] = {
        "accel": "tcg,thread=single",
        "vcpus": 1,
        "icount": False,
    }
    provenance["host_cpu_affinity"] = [args.cpu]
    provenance["warmup_policy"] = {
        "discarded_fresh_process_pairs": args.warmups,
        "in_guest_warmup_passes_before_fixed_work": 1,
        "fixed_work_execution": "warm",
        "boot_execution": "cold-start",
    }
    provenance["opensbi_sha256"] = sha256(args.opensbi)
    records = []
    dummy = (args.out_dir / "dummy.img").resolve()
    with dummy.open("wb") as handle:
        handle.truncate(1024 * 1024)
    for case in manifest["cases"]:
        case.setdefault("mandatory", True)
        case["pairs"] = args.runs
        case.setdefault("config", {}).update(
            mode=provenance["candidate"]["mode"],
            execution="cold-start" if case["id"] == "boot" else "warm",
            in_guest_warmup_passes=1,
            discarded_fresh_process_pairs=args.warmups,
            scope="boot" if case["id"] == "boot" else "fixed-work",
            reference="qemu-tcg",
            vcpus=1,
            icount=False,
        )
        if case.get("status") != "ready":
            records.append(
                {
                    "case_id": case["id"],
                    "pair_id": 0,
                    "correctness": (
                        "untested"
                        if case.get("status") == "unverified"
                        else case.get("status", "untested")
                    ),
                    "reason": case.get("reason", "guest not ready"),
                    "phase": "boot" if case["id"] == "boot" else "work",
                }
            )
            continue
        for engine in ("rv32emu", "qemu"):
            case[engine]["hashes"] = {
                key: sha256(args.manifest.parent / case[engine][key])
                for key in ("elf", "raw")
            }
        for pair in range(-args.warmups, args.runs):
            order = (
                ["qemu", "rv32emu"] if pair % 2 == 0 else ["rv32emu", "qemu"]
            )
            record = {
                "case_id": case["id"],
                "pair_id": pair,
                "correctness": "passed",
                "phase": "boot" if case["id"] == "boot" else "work",
                "order": [
                    "reference" if e == "qemu" else "candidate" for e in order
                ],
            }
            for engine in order:
                label = "reference" if engine == "qemu" else "candidate"
                command = command_for(
                    engine, case, args, args.manifest.parent, dummy
                )
                record[label + "_command"] = command
                try:
                    sample = observe(
                        command,
                        case,
                        args.timeout,
                        args.cpu,
                        args.out_dir / f"{case['id']}-{pair}-{engine}.log",
                    )
                    record[label + "_seconds"] = sample[
                        (
                            "boot_seconds"
                            if case["id"] == "boot"
                            else "work_seconds"
                        )
                    ]
                except (BenchError, OSError) as error:
                    record["correctness"] = "failed"
                    record[label + "_error"] = str(error)
            if pair >= 0 or record["correctness"] != "passed":
                records.append(record)
            if record["correctness"] != "passed":
                break
    cpu_profiles = []
    sampler = None
    sampler_error = None
    if args.profile_case and args.profile_backend == "cpu-sampler":
        from zephyr_cpu_profile import CpuSampler, SCOPE, aggregate_profiles

        try:
            sampler = CpuSampler(
                args.rv32emu,
                args.rv32emu_config,
                args.out_dir / "cpu-sampler",
                args.profile_cc,
            )
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            sampler_error = f"CPU profiling unavailable: {error}"
    for case_id in args.profile_case:
        case = known_cases[case_id]
        case_profiles = []
        for index in range(args.profile_runs):
            profile = {
                "case_id": case_id,
                "phase": "profile",
                "correctness": "untested",
                "backend": args.profile_backend,
                "profile_run": index,
            }
            if case.get("status") != "ready":
                profile["reason"] = "guest not ready"
            elif args.profile_backend == "cpu-sampler":
                if sampler is None:
                    profile["reason"] = sampler_error
                else:
                    profile = sampler.run(
                        command_for(
                            "rv32emu", case, args, args.manifest.parent, dummy
                        ),
                        case,
                        args.timeout,
                        args.cpu,
                        index,
                        observe,
                    )
            else:
                suffix = "" if args.profile_runs == 1 else f"-{index:04d}"
                command = [
                    "perf",
                    "record",
                    "-g",
                    "-o",
                    str(
                        (
                            args.out_dir / f"{case_id}{suffix}.perf.data"
                        ).resolve()
                    ),
                    "--",
                ]
                command += command_for(
                    "rv32emu", case, args, args.manifest.parent, dummy
                )
                profile["command"] = command
                try:
                    observe(
                        command,
                        case,
                        args.timeout,
                        args.cpu,
                        args.out_dir / f"{case_id}{suffix}-profile.log",
                    )
                    profile["correctness"] = "passed"
                except (BenchError, OSError) as error:
                    profile["reason"] = (
                        f"profiling unavailable: {error}; inspect profile log"
                    )
            records.append(profile)
            case_profiles.append(profile)
        if args.profile_backend == "cpu-sampler":
            summary = aggregate_profiles(case_profiles, args.profile_runs)
            summary["case_id"] = case_id
            cpu_profiles.append(summary)
    from system_bench import attach_diagnostics, summarize, write_report

    (args.out_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n"
    )
    (args.out_dir / "records.json").write_text(
        json.dumps(records, indent=2) + "\n"
    )
    report = qualify_mode(summarize(manifest, records), args.mode)
    if cpu_profiles:
        report["cpu_profiles"] = cpu_profiles
        report["limitations"] += (
            " Optional CPU profiles are separate diagnostics. " + SCOPE
        )
        (args.out_dir / "cpu-profiles.json").write_text(
            json.dumps(cpu_profiles, indent=2) + "\n"
        )
    if diagnostics is not None:
        report = attach_diagnostics(report, diagnostics)
    write_report(report, args.out_dir)
    print(json.dumps(report, indent=2))
    return 0 if report.get("status") in ("passed", "informational") else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (
        BenchError,
        OSError,
        ValueError,
        subprocess.SubprocessError,
    ) as error:
        print(f"zephyr_bench: {error}", file=sys.stderr)
        sys.exit(1)
