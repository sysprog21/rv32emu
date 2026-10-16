"""Optional Linux x86_64 interrupted-PC sampling for separate Zephyr runs.

No counter privilege or emulator source change is required. The target must be
one direct, dynamically linked ELF executable, with T2C disabled. Thread counts
at START/END cannot rule out transient threads; delivery on another thread is
fatal to that profiling process. Counts include every JIT/library/unknown PC.
"""

import bisect
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import signal
import struct
import subprocess
import time

MAGIC = 0x5256333250433032
HEADER = struct.Struct("<9Q")
CAPACITY = 1048576
INTERVAL_NS = 1000000
SCOPE = (
    "Linux x86_64 ITIMER_PROF interrupted-PC fractions in a signal-controlled "
    "host CPU window whose resume/pause controls are sent after observing "
    "START/END respectively. "
    "CLOCK_PROCESS_CPUTIME_ID measures user+system CPU including sampler/control "
    "overhead; kernel work may be charged to the interrupted user PC. "
    "Marker observation and signal delivery add boundary latency. "
    "All library/JIT/unknown samples remain in the denominator. "
    "Symbol-only PC attribution is not exclusive function time, a host-wall "
    "fraction, or proof of cause/removable cost. Zero overflow does not prove "
    "zero missed/coalesced signals; periodic sampling may alias. Thread counts "
    "are checked at both boundaries, not continuously; any sampled/control "
    "signal delivered off the main thread invalidates that run. "
    "Mapping attribution uses the START snapshot, so later mappings remain "
    "unresolved. Unprofiled paired wall-time runs must establish improvements."
)


class ProfileError(ValueError):
    pass


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def parse_payload(data):
    """Require the complete v2 payload and all successful timer/clock fields."""
    if len(data) < HEADER.size:
        raise ProfileError("truncated v2 sampler header")
    fields = HEADER.unpack_from(data)
    (
        magic,
        count,
        dropped,
        begin,
        end,
        interval,
        start_err,
        stop_err,
        clock_err,
    ) = fields
    if magic != MAGIC:
        raise ProfileError("unsupported sampler payload version")
    if not 0 < count <= CAPACITY:
        raise ProfileError("invalid sampler count")
    if len(data) != HEADER.size + count * 8:
        raise ProfileError("truncated or extra sampler payload")
    if dropped or start_err or stop_err or clock_err:
        raise ProfileError("sampler overflow or timer/clock syscall error")
    if end <= begin or interval != INTERVAL_NS:
        raise ProfileError("invalid sampler CPU duration or interval")
    return {
        "version": 2,
        "samples": count,
        "dropped": dropped,
        "begin_cpu_ns": begin,
        "end_cpu_ns": end,
        "cpu_seconds": (end - begin) / 1e9,
        "interval_ns": interval,
        "start_error": start_err,
        "stop_error": stop_err,
        "clock_error": clock_err,
    }, struct.unpack_from(f"<{count}Q", data, HEADER.size)


def read_payload(path):
    with Path(path).open("rb") as handle:
        data = handle.read(HEADER.size + CAPACITY * 8 + 1)
    return parse_payload(data)


def wait_payload(path, timeout=1):
    deadline = time.monotonic() + timeout
    while True:
        try:
            with Path(path).open("rb") as handle:
                data = handle.read(HEADER.size + CAPACITY * 8 + 1)
        except FileNotFoundError:
            data = b""
        if len(data) >= HEADER.size:
            fields = HEADER.unpack_from(data)
            if fields[0] != MAGIC or not 0 < fields[1] <= CAPACITY:
                return parse_payload(data)
            if len(data) >= HEADER.size + 8 * fields[1]:
                return parse_payload(data)
        if time.monotonic() >= deadline:
            raise ProfileError("sampler did not finish its complete v2 payload")
        time.sleep(0.005)


def elf_segments(binary):
    """Map executable file offsets to ELF virtual addresses for PIE and ET_EXEC."""
    with Path(binary).open("rb") as handle:
        header = handle.read(64)
        if len(header) != 64 or header[:6] != b"\x7fELF\x02\x01":
            raise ProfileError(
                "cpu-sampler needs a direct Linux x86_64 ELF binary"
            )
        elf = struct.unpack("<16sHHIQQQIHHHHHH", header)
        if (
            elf[1] not in (2, 3)
            or elf[2] != 62
            or elf[9] != 56
            or not 0 < elf[10] < 4096
        ):
            raise ProfileError("unsupported target ELF header")
        handle.seek(elf[5])
        segments = []
        interpreter = False
        for _ in range(elf[10]):
            data = handle.read(56)
            if len(data) != 56:
                raise ProfileError("truncated ELF program header")
            kind, flags, offset, vaddr, _, filesz, _, _ = struct.unpack(
                "<IIQQQQQQ", data
            )
            interpreter |= kind == 3
            if kind == 1 and flags & 1:
                segments.append((offset, offset + filesz, vaddr))
        if not interpreter or not segments:
            raise ProfileError(
                "cpu-sampler requires a dynamically linked executable"
            )
    return segments


def read_symbols(binary):
    try:
        process = subprocess.run(
            ["nm", "-n", "-S", "--defined-only", str(binary)],
            capture_output=True,
            text=True,
            timeout=10,
            check=True,
        )
    except (OSError, subprocess.SubprocessError) as error:
        return [], f"text symbols unavailable: {error}"
    symbols = []
    for line in process.stdout.splitlines():
        fields = line.split()
        if len(fields) == 4 and fields[2] in ("t", "T", "w", "W"):
            try:
                symbols.append(
                    (int(fields[0], 16), int(fields[1], 16), fields[3])
                )
            except ValueError:
                continue
    return sorted(symbols), None if symbols else "no sized host text symbols"


def read_maps(text):
    maps = []
    for line in text.splitlines():
        fields = line.split(maxsplit=5)
        if len(fields) < 5:
            raise ProfileError("invalid process mapping")
        try:
            start, end = (int(part, 16) for part in fields[0].split("-"))
            offset = int(fields[2], 16)
        except ValueError as error:
            raise ProfileError("invalid process mapping") from error
        if end <= start:
            raise ProfileError("invalid process mapping bounds")
        maps.append(
            (
                start,
                end,
                fields[1],
                offset,
                fields[5] if len(fields) == 6 else "",
            )
        )
    if not maps:
        raise ProfileError("empty process mappings")
    return maps


def attribute(pcs, maps, binary, segments, symbols):
    addresses = [symbol[0] for symbol in symbols]
    result = Counter()
    for pc in pcs:
        mapping = next(
            (entry for entry in maps if entry[0] <= pc < entry[1]), None
        )
        if mapping is None:
            name = "unmapped-at-start-snapshot"
        elif mapping[4] == str(binary):
            file_offset = pc - mapping[0] + mapping[3]
            segment = next(
                (
                    entry
                    for entry in segments
                    if entry[0] <= file_offset < entry[1]
                ),
                None,
            )
            name = "unresolved-main"
            if segment is not None and "x" in mapping[2]:
                elf_pc = segment[2] + file_offset - segment[0]
                index = bisect.bisect_right(addresses, elf_pc) - 1
                if index >= 0:
                    start, size, symbol = symbols[index]
                    if start <= elf_pc < start + size:
                        name = "main:" + symbol
        elif "x" in mapping[2] and not mapping[4]:
            name = "anonymous-executable/JIT-unresolved"
        elif mapping[4]:
            name = "module:" + mapping[4]
        else:
            name = "unknown-nonexecutable"
        result[name] += 1
    return result


def live_group_members(group):
    members = []
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            fields = (entry / "stat").read_text().rsplit(")", 1)[1].split()
            if int(fields[2]) == group and fields[0] != "Z":
                members.append(int(entry.name))
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            continue
    return members


class Window:
    def __init__(self, backend, directory):
        self.backend = backend
        self.directory = directory
        self.payload = directory / "samples.bin"
        self.process = None
        self.events = []
        self.workload_passed = False
        self.header = None
        self.threads = {}

    def launched(self, process):
        self.process = process
        if Path(os.readlink(f"/proc/{process.pid}/exe")) != self.backend.binary:
            raise ProfileError(
                "cpu-sampler requires the direct target ELF process"
            )

    def check_threads(self, boundary):
        threads = len(list(Path(f"/proc/{self.process.pid}/task").iterdir()))
        self.threads[boundary] = threads
        if threads != 1:
            raise ProfileError(
                "cpu-sampler only supports a single-threaded target"
            )

    def start(self, process):
        self.check_threads("start")
        maps = Path(f"/proc/{process.pid}/maps").read_text()
        read_maps(maps)
        (self.directory / "maps-start.txt").write_text(maps)
        os.kill(process.pid, signal.SIGUSR2)
        self.events.append(
            {
                "event": "sampling-resume",
                "host_perf_counter": time.perf_counter(),
            }
        )

    def finish(self, process):
        self.workload_passed = True
        self.check_threads("end")
        (self.directory / "maps-end.txt").write_text(
            Path(f"/proc/{process.pid}/maps").read_text()
        )
        os.kill(process.pid, signal.SIGUSR2)
        self.events.append(
            {
                "event": "sampling-pause",
                "host_perf_counter": time.perf_counter(),
            }
        )
        self.header, _ = wait_payload(self.payload)


class CpuSampler:
    def __init__(self, binary, config, output, compiler="cc"):
        self.binary = Path(binary).resolve()
        self.config = Path(config).resolve()
        self.output = Path(output).resolve()
        if platform.system() != "Linux" or platform.machine() != "x86_64":
            raise ProfileError("cpu-sampler supports only Linux x86_64")
        if os.environ.get("LD_PRELOAD"):
            raise ProfileError(
                "cpu-sampler does not replace an existing LD_PRELOAD"
            )
        settings = self.config.read_text()
        if "CONFIG_SYSTEM=y" not in settings or "CONFIG_T2C=y" in settings:
            raise ProfileError(
                "cpu-sampler needs system mode with T2C disabled"
            )
        if self.config.stat().st_mtime > self.binary.stat().st_mtime:
            raise ProfileError(
                "effective configuration is newer than the executable"
            )
        self.segments = elf_segments(self.binary)
        if self.binary.stat().st_mode & 0o6000:
            raise ProfileError("cpu-sampler cannot preload a privileged target")
        if "security.capability" in os.listxattr(self.binary):
            raise ProfileError("cpu-sampler cannot preload a capability target")
        self.output.mkdir(parents=True, exist_ok=False)
        source = Path(__file__).with_name("zephyr_cpu_sampler.c")
        self.library = self.output / "cpu_sampler.so"
        command = shlex.split(compiler)
        if not command:
            raise ProfileError("no CPU sampler compiler supplied")
        command += [
            "-shared",
            "-fPIC",
            "-O2",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            str(source),
            "-o",
            str(self.library),
        ]
        build = subprocess.run(
            command, capture_output=True, text=True, timeout=60
        )
        (self.output / "compiler.log").write_text(build.stdout + build.stderr)
        if build.returncode:
            raise ProfileError(
                f"CPU sampler compilation failed; see {self.output / 'compiler.log'}"
            )
        self.identity_paths = {
            "sampler_source_sha256": source,
            "sampler_so_sha256": self.library,
            "binary_sha256": self.binary,
            "config_sha256": self.config,
            "analyzer_source_sha256": Path(__file__),
        }
        self.identity = {
            key: digest(path) for key, path in self.identity_paths.items()
        }
        self.symbols, self.symbol_error = read_symbols(self.binary)
        self.build_command = command
        self.compiler = {"command": shlex.split(compiler)}
        executable = shutil.which(self.compiler["command"][0])
        if executable:
            self.compiler.update(
                executable=str(Path(executable).resolve()),
                sha256=digest(executable),
            )
        try:
            version = subprocess.run(
                self.compiler["command"] + ["--version"],
                capture_output=True,
                text=True,
                timeout=10,
            )
            self.compiler.update(
                version=version.stdout.strip(),
                version_returncode=version.returncode,
            )
        except (OSError, subprocess.SubprocessError) as error:
            self.compiler["version_error"] = str(error)

    def check_identity(self):
        if self.identity != {
            key: digest(path) for key, path in self.identity_paths.items()
        }:
            raise ProfileError(
                "profile source/build identity changed during collection"
            )

    def run(self, command, case, timeout, cpu, index, observe):
        record = {
            "case_id": case["id"],
            "phase": "profile",
            "backend": "cpu-sampler",
            "profile_run": index,
            "correctness": "untested",
            "scope": SCOPE,
            "identity": self.identity,
            "identity_paths": {
                key: str(path) for key, path in self.identity_paths.items()
            },
            "build_command": self.build_command,
            "compiler": self.compiler,
            "command": command,
            "cpu": cpu,
            "iterations": case["iterations"],
            "expected_checksum": case["expected_checksum"],
            "symbolization_error": self.symbol_error,
        }
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", case["id"]):
            record["reason"] = "unsafe profile case ID"
            return record
        directory = self.output / case["id"] / f"run-{index:04d}"
        record["directory"] = str(directory)
        window = Window(self, directory)
        created = False
        try:
            directory.mkdir(parents=True, exist_ok=False)
            created = True
            self.check_identity()
            if "-k" in command:
                image = Path(command[command.index("-k") + 1])
                record["guest_image_sha256"] = digest(image)
            if Path(command[0]).resolve() != self.binary:
                raise ProfileError(
                    "profile command does not name the direct target ELF"
                )
            environment = os.environ.copy()
            environment.update(
                LD_PRELOAD=str(self.library),
                RV32_CPU_SAMPLE_OUT=str(window.payload),
            )
            observed = observe(
                command,
                case,
                timeout,
                cpu,
                directory / "console.log",
                environment=environment,
                observer=window,
            )
            self.check_identity()
            if "-k" in command and record["guest_image_sha256"] != digest(
                image
            ):
                raise ProfileError(
                    "guest image changed during profile collection"
                )
            header, pcs = read_payload(window.payload)
            counts = attribute(
                pcs,
                read_maps((directory / "maps-start.txt").read_text()),
                self.binary,
                self.segments,
                self.symbols,
            )
            record.update(
                header=header,
                sample_valid=True,
                symbol_counts=dict(counts),
                observed_work_seconds=observed["work_seconds"],
                payload_sha256=digest(window.payload),
                maps_sha256=digest(directory / "maps-start.txt"),
            )
            record["correctness"] = "passed"
        except (
            OSError,
            ValueError,
            RuntimeError,
            subprocess.SubprocessError,
        ) as error:
            record["reason"] = f"CPU profiling unavailable or invalid: {error}"
        finally:
            if record["correctness"] != "passed":
                try:
                    with window.payload.open("rb") as handle:
                        header_bytes = handle.read(HEADER.size)
                    if len(header_bytes) == HEADER.size:
                        record["payload_header_words"] = list(
                            HEADER.unpack(header_bytes)
                        )
                except OSError:
                    pass
            record["events"] = window.events
            record["target_threads"] = window.threads
            record["workload_correctness"] = (
                "passed" if window.workload_passed else "untested"
            )
            if window.process is not None:
                record["process_returncode"] = window.process.returncode
                record["live_process_group_members"] = live_group_members(
                    window.process.pid
                )
                if record["live_process_group_members"]:
                    record.update(
                        correctness="untested",
                        reason="profile process group has live members after cleanup",
                    )
            if created:
                (directory / "run.json").write_text(
                    json.dumps(record, indent=2) + "\n"
                )
        return record


def aggregate_profiles(records, planned_runs):
    """Never publish fractions from only the successful subset of planned runs."""
    result = {
        "status": "untested",
        "planned_runs": planned_runs,
        "scope": SCOPE,
        "valid_runs": sum(
            record.get("correctness") == "passed" for record in records
        ),
    }
    try:
        if (
            type(planned_runs) is not int
            or planned_runs < 1
            or len(records) != planned_runs
        ):
            raise ProfileError("missing planned profile runs")
        if {record.get("profile_run") for record in records} != set(
            range(planned_runs)
        ):
            raise ProfileError("duplicate or missing profile run indices")
        identity = records[0].get("identity")
        case = records[0].get("case_id")
        if not identity:
            raise ProfileError("missing profile source/build identity")
        if "binary_sha256" not in identity:
            raise ProfileError("missing executable profile identity")
        paths = records[0].get("identity_paths", {})
        if set(paths) != set(identity) or any(
            digest(path) != identity[key] for key, path in paths.items()
        ):
            raise ProfileError(
                "profile source/build identity no longer matches its files"
            )
        binary = Path(paths["binary_sha256"])
        segments = elf_segments(binary)
        symbols, _ = read_symbols(binary)
        counts = Counter()
        runs = []
        cpu_seconds = 0.0
        samples = 0
        for record in records:
            if (
                record.get("case_id") != case
                or record.get("identity") != identity
                or record.get("identity_paths") != paths
                or record.get("iterations") != records[0].get("iterations")
                or record.get("expected_checksum")
                != records[0].get("expected_checksum")
                or record.get("command") != records[0].get("command")
                or record.get("guest_image_sha256")
                != records[0].get("guest_image_sha256")
                or record.get("correctness") != "passed"
                or not record.get("sample_valid")
                or record.get("target_threads") != {"start": 1, "end": 1}
                or record.get("live_process_group_members") != []
                or record.get("process_returncode") is None
            ):
                raise ProfileError(
                    "invalid workload, payload, threads, cleanup, or mixed profile identity"
                )
            directory = Path(record["directory"])
            payload = directory / "samples.bin"
            if digest(payload) != record.get("payload_sha256") or digest(
                directory / "maps-start.txt"
            ) != record.get("maps_sha256"):
                raise ProfileError("profile evidence changed after collection")
            header, pcs = read_payload(payload)
            one = attribute(
                pcs,
                read_maps((directory / "maps-start.txt").read_text()),
                binary,
                segments,
                symbols,
            )
            if dict(one) != record.get("symbol_counts"):
                raise ProfileError(
                    "attribution differs from raw PCs and mapping evidence"
                )
            if (
                not one
                or any(type(n) is not int or n <= 0 for n in one.values())
                or sum(one.values()) != header["samples"]
                or record.get("header") != header
            ):
                raise ProfileError(
                    "sample attribution does not reconcile with the complete payload"
                )
            counts.update(one)
            runs.append(
                {
                    "profile_run": record["profile_run"],
                    "samples": header["samples"],
                    "cpu_seconds": header["cpu_seconds"],
                    "nominal_interval_ns": header["interval_ns"],
                }
            )
            samples += header["samples"]
            cpu_seconds += header["cpu_seconds"]
        result.update(
            status="passed",
            case_id=case,
            samples=samples,
            cpu_seconds=cpu_seconds,
            observed_cpu_seconds_per_sample=cpu_seconds / samples,
            identity=identity,
            runs=runs,
            functions=[
                {"symbol": name, "samples": n, "fraction": n / samples}
                for name, n in counts.most_common()
            ],
        )
    except (OSError, KeyError, TypeError, ValueError) as error:
        result["reason"] = str(error)
    return result
