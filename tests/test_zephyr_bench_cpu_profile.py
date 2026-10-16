"""Validate optional sampling evidence and its separation from scored results."""

import contextlib
import copy
import io
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

from zephyr_bench import MANDATORY_CASES, main, observe
from zephyr_cpu_profile import (
    CAPACITY,
    CpuSampler,
    HEADER,
    INTERVAL_NS,
    MAGIC,
    ProfileError,
    aggregate_profiles,
    attribute,
    digest,
    elf_segments,
    parse_payload,
    read_maps,
    wait_payload,
)


def payload(**changes):
    fields = dict(
        magic=MAGIC,
        count=2,
        dropped=0,
        begin=100,
        end=2000100,
        interval=INTERVAL_NS,
        start_error=0,
        stop_error=0,
        clock_error=0,
    )
    fields.update(changes)
    return HEADER.pack(*fields.values()) + struct.pack("<2Q", 0x1010, 0x1020)


class PayloadTests(unittest.TestCase):
    def test_complete_v2_payload_has_measured_cpu_duration(self):
        header, pcs = parse_payload(payload())
        self.assertEqual(header["samples"], 2)
        self.assertAlmostEqual(header["cpu_seconds"], 0.002)
        self.assertEqual(pcs, (0x1010, 0x1020))

    def test_reject_partial_old_extra_overflow_and_syscall_errors(self):
        invalid = [
            payload()[:40],
            payload()[:-1],
            payload() + b"x",
            payload(magic=0x5256333250433031),
            payload(count=0),
            payload(count=CAPACITY + 1),
            payload(dropped=1),
            payload(start_error=22),
            payload(stop_error=22),
            payload(clock_error=22),
            payload(end=100),
            payload(interval=INTERVAL_NS * 2),
        ]
        for data in invalid:
            with self.subTest(data=data[:72]), self.assertRaises(ProfileError):
                parse_payload(data)

    def test_wait_rejects_dump_that_never_reaches_declared_length(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "truncated.bin"
            path.write_bytes(payload()[:-8])
            with self.assertRaisesRegex(ProfileError, "complete v2"):
                wait_payload(path, timeout=0.01)

    def test_pie_nonpie_and_unknown_samples_keep_full_denominator(self):
        binary = Path("/example/rv32emu")
        maps = read_maps(
            "70001000-70002000 r-xp 00001000 00:00 0 /example/rv32emu\n"
            "80000000-80001000 r-xp 00000000 00:00 0\n"
            "90000000-90001000 r-xp 00000000 00:00 0 /lib/libc.so\n"
            "a0000000-a0001000 rw-p 00000000 00:00 0\n"
        )
        pcs = [0x70001010, 0x80000010, 0x90000010, 0xA0000010, 0xB0000010]
        for vaddr in (0x1000, 0x401000):
            counts = attribute(
                pcs,
                maps,
                binary,
                [(0x1000, 0x2000, vaddr)],
                [(vaddr, 0x100, "hot_loop")],
            )
            self.assertEqual(counts["main:hot_loop"], 1)
            self.assertEqual(counts["anonymous-executable/JIT-unresolved"], 1)
            self.assertEqual(counts["module:/lib/libc.so"], 1)
            self.assertEqual(counts["unknown-nonexecutable"], 1)
            self.assertEqual(counts["unmapped-at-start-snapshot"], 1)
            self.assertEqual(sum(counts.values()), len(pcs))
        gap = attribute(
            [0x70001800],
            maps,
            binary,
            [(0x1000, 0x2000, 0x1000)],
            [(0x1000, 0x100, "hot_loop")],
        )
        self.assertEqual(gap, {"unresolved-main": 1})

    def test_wrappers_are_rejected_before_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "wrapper"
            script.write_text("#!/bin/sh\nexec rv32emu\n")
            with self.assertRaisesRegex(
                ProfileError, "direct Linux x86_64 ELF"
            ):
                elf_segments(script)

    def test_profile_cli_rejects_zero_runs(self):
        with (
            contextlib.redirect_stderr(io.StringIO()),
            self.assertRaises(SystemExit) as error,
        ):
            main(["--opensbi", "/unused", "--profile-runs", "0"])
        self.assertEqual(error.exception.code, 2)


FIXTURE = r"""
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static uint64_t now(void) {
    struct timespec t;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    return (uint64_t)t.tv_sec * 1000000000ULL + t.tv_nsec;
}
static void busy(uint64_t ns) {
    uint64_t end = now() + ns;
    volatile unsigned work = 0;
    while (now() < end) work++;
}
static void *wrong_thread(void *unused) {
    (void)unused;
    pthread_kill(pthread_self(), SIGPROF);
    return NULL;
}
int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        puts("fixture version"); return 0;
    }
    if (argc > 1 && strcmp(argv[1], "thread-signal") == 0) {
        puts("READY"); fflush(stdout);
        pause();
        pthread_t thread;
        pthread_create(&thread, NULL, wrong_thread, NULL);
        pthread_join(thread, NULL);
        return 0;
    }
    puts("*** Booting Zephyr OS build fixture ***");
    puts("RV32EMU_BENCH_WARMUP case=allocation iterations=20 checksum=42");
    puts("RV32EMU_BENCH_READY case=allocation");
    puts("RV32EMU_BENCH_START case=allocation iterations=20"); fflush(stdout);
    busy(80000000);
    puts("RV32EMU_BENCH_END case=allocation iterations=20 checksum=42 status=PASS");
    fflush(stdout);
    while (1) pause();
}
"""
SUPPORTED = (
    platform.system() == "Linux"
    and platform.machine() == "x86_64"
    and shutil.which("cc") is not None
    and not os.environ.get("LD_PRELOAD")
)


@unittest.skipUnless(
    SUPPORTED, "optional sampler needs Linux x86_64 and a host C compiler"
)
class NativeSamplerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.directory = Path(cls.temp.name)
        cls.source = cls.directory / "fixture.c"
        cls.source.write_text(FIXTURE)
        cls.binary = cls.directory / "fixture"
        cls.config = cls.directory / ".effective-config"
        cls.config.write_text("CONFIG_SYSTEM=y\nCONFIG_JIT=y\n")
        subprocess.run(
            [
                "cc",
                "-O2",
                "-g",
                "-pthread",
                str(cls.source),
                "-o",
                str(cls.binary),
            ],
            check=True,
            capture_output=True,
        )
        cls.backend = CpuSampler(
            cls.binary, cls.config, cls.directory / "profiles"
        )
        cls.cpu = min(os.sched_getaffinity(0))
        cls.case = {
            "id": "allocation",
            "iterations": 20,
            "expected_checksum": 42,
        }
        cls.sequence = 0

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def collect(self):
        index = type(self).sequence
        type(self).sequence += 1
        result = self.backend.run(
            [str(self.binary)], self.case, 3, self.cpu, index, observe
        )
        self.assertEqual(result["correctness"], "passed", result.get("reason"))
        self.assertEqual(result["live_process_group_members"], [])
        return result

    def test_real_cpu_window_dump_and_aggregate(self):
        record = self.collect()
        record["profile_run"] = 0
        result = aggregate_profiles([record], 1)
        self.assertEqual(result["status"], "passed", result.get("reason"))
        self.assertGreater(result["samples"], 5)
        self.assertGreater(result["cpu_seconds"], 0.01)
        self.assertEqual(
            sum(item["samples"] for item in result["functions"]),
            result["samples"],
        )
        self.assertAlmostEqual(
            sum(item["fraction"] for item in result["functions"]), 1.0
        )
        self.assertIn("not exclusive", result["scope"])

    def test_partial_invalid_or_changed_evidence_never_aggregates(self):
        record = self.collect()
        record["profile_run"] = 0
        self.assertEqual(aggregate_profiles([record], 2)["status"], "untested")
        bad = copy.deepcopy(record)
        bad.update(correctness="untested", profile_run=1)
        self.assertEqual(
            aggregate_profiles([record, bad], 2)["status"], "untested"
        )
        altered = copy.deepcopy(record)
        altered["symbol_counts"] = {
            "main:invented": record["header"]["samples"]
        }
        result = aggregate_profiles([altered], 1)
        self.assertEqual(result["status"], "untested")
        self.assertNotIn("functions", result)
        sample = Path(record["directory"]) / "samples.bin"
        sample.write_bytes(sample.read_bytes()[:-1])
        self.assertEqual(aggregate_profiles([record], 1)["status"], "untested")

    def test_source_identity_mixing_is_rejected(self):
        one = self.collect()
        two = self.collect()
        one["profile_run"] = 0
        two["profile_run"] = 1
        two["identity"] = dict(two["identity"], sampler_so_sha256="different")
        self.assertEqual(
            aggregate_profiles([one, two], 2)["status"], "untested"
        )

    def test_inherited_signal_timer_and_blocked_signal_guards(self):
        for label, setup in (
            ("SIGPROF", "signal.signal(signal.SIGPROF,signal.SIG_IGN)"),
            ("SIGUSR2", "signal.signal(signal.SIGUSR2,signal.SIG_IGN)"),
            ("timer", "signal.setitimer(signal.ITIMER_PROF,1,1)"),
            (
                "blocked",
                "signal.pthread_sigmask(signal.SIG_BLOCK,{signal.SIGUSR2})",
            ),
        ):
            with self.subTest(guard=label):
                environment = os.environ.copy()
                environment.update(
                    LD_PRELOAD=str(self.backend.library),
                    RV32_CPU_SAMPLE_OUT=str(self.directory / (label + ".bin")),
                )
                code = (
                    "import signal,os;"
                    + setup
                    + ";os.execve('/bin/true',['true'],"
                    + repr(environment)
                    + ")"
                )
                result = subprocess.run(
                    [sys.executable, "-c", code],
                    capture_output=True,
                    text=True,
                    timeout=3,
                )
                self.assertEqual(result.returncode, 125, result.stderr)
                self.assertIn("collision", result.stderr)

    def test_dump_open_failure_and_existing_output_are_explicit(self):
        for path in (self.directory / "absent" / "samples.bin", self.source):
            environment = os.environ.copy()
            environment.update(
                LD_PRELOAD=str(self.backend.library),
                RV32_CPU_SAMPLE_OUT=str(path),
            )
            before = self.source.read_bytes()
            result = subprocess.run(
                ["/bin/true"],
                env=environment,
                capture_output=True,
                text=True,
                timeout=3,
            )
            self.assertEqual(result.returncode, 125)
            self.assertIn("open", result.stderr)
            self.assertEqual(self.source.read_bytes(), before)

    def test_sample_delivered_to_worker_thread_is_fatal(self):
        environment = os.environ.copy()
        environment.update(
            LD_PRELOAD=str(self.backend.library),
            RV32_CPU_SAMPLE_OUT=str(self.directory / "off-main-thread.bin"),
        )
        process = subprocess.Popen(
            [str(self.binary), "thread-signal"],
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            self.assertEqual(process.stdout.readline().strip(), "READY")
            os.kill(process.pid, signal.SIGUSR2)
            _, error = process.communicate(timeout=3)
            self.assertEqual(process.returncode, 125)
            self.assertIn("single-threaded", error)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            process.stdout.close()
            process.stderr.close()

    def test_reject_t2c_configuration_and_missing_compiler(self):
        config = self.directory / "t2c-config"
        config.write_text("CONFIG_SYSTEM=y\nCONFIG_JIT=y\nCONFIG_T2C=y\n")
        with self.assertRaisesRegex(ProfileError, "T2C disabled"):
            CpuSampler(self.binary, config, self.directory / "t2c")
        with self.assertRaises(FileNotFoundError):
            CpuSampler(
                self.binary,
                self.config,
                self.directory / "no-compiler",
                compiler="/missing-host-cc",
            )

    def test_optional_compiler_failure_cannot_change_scored_gate(self):
        for reference, candidate in ((2.0, 1.0), (1.0, 2.0)):
            with (
                self.subTest(reference=reference),
                tempfile.TemporaryDirectory() as directory,
            ):
                directory = Path(directory)
                cases = []
                for case_id in MANDATORY_CASES:
                    image = directory / (case_id + ".img")
                    image.write_bytes(b"fixed guest fixture")
                    cases.append(
                        {
                            "id": case_id,
                            "status": "ready",
                            "mandatory": True,
                            "phase": "boot" if case_id == "boot" else "work",
                            "iterations": 20,
                            "expected_checksum": 42,
                            "rv32emu": {"elf": str(image), "raw": str(image)},
                            "qemu": {"elf": str(image), "raw": str(image)},
                        }
                    )
                manifest = directory / "manifest.json"
                manifest.write_text(
                    json.dumps(
                        {"schema_version": 1, "cases": cases, "provenance": {}}
                    )
                )

                def scored(command, *_args, **_kwargs):
                    seconds = reference if "-kernel" in command else candidate
                    return {"work_seconds": seconds, "boot_seconds": seconds}

                with (
                    patch("zephyr_bench.observe", side_effect=scored),
                    contextlib.redirect_stdout(io.StringIO()),
                ):
                    status = main(
                        [
                            "--manifest",
                            str(manifest),
                            "--rv32emu",
                            str(self.binary),
                            "--rv32emu-config",
                            str(self.config),
                            "--qemu",
                            str(self.binary),
                            "--opensbi",
                            str(self.source),
                            "--out-dir",
                            str(directory / "results"),
                            "--runs",
                            "2",
                            "--warmups",
                            "0",
                            "--profile-case",
                            "allocation",
                            "--profile-backend",
                            "cpu-sampler",
                            "--profile-runs",
                            "2",
                            "--profile-cc",
                            "/missing-host-cc",
                        ]
                    )
                report = json.loads(
                    (directory / "results/system-bench.json").read_text()
                )
                self.assertEqual(status, 0 if candidate < reference else 1)
                self.assertEqual(
                    report["primary_goal_passed"], candidate < reference
                )
                self.assertEqual(
                    report["cpu_profiles"][0]["status"], "untested"
                )
                self.assertEqual(len(report["profile_records"]), 2)
                self.assertTrue(
                    all(
                        record["correctness"] == "untested"
                        for record in report["profile_records"]
                    )
                )


if __name__ == "__main__":
    unittest.main()
