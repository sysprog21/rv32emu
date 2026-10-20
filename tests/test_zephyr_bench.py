"""Exercise real console pipes, protocol failures and process-group cleanup."""

import io
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
from types import SimpleNamespace

from zephyr_bench import (
    BenchError,
    ConsoleProtocol,
    MANDATORY_CASES,
    main,
    observe,
    require_inventory,
    verify_engine,
    qualify_mode,
)

CASE = {"id": "allocation", "iterations": 20, "expected_checksum": 42}
LINES = [
    "*** Booting Zephyr OS build test ***",
    "RV32EMU_BENCH_WARMUP case=allocation iterations=20 checksum=42",
    "RV32EMU_BENCH_READY case=allocation",
    "RV32EMU_BENCH_START case=allocation iterations=20",
    "RV32EMU_BENCH_END case=allocation iterations=20 checksum=42 status=PASS",
]


class HostSupportTest(unittest.TestCase):
    def test_cli_rejects_missing_affinity_with_actionable_error(self):
        with (
            patch("zephyr_bench.os", SimpleNamespace(environ={})),
            patch("sys.stderr", new_callable=io.StringIO) as stderr,
        ):
            with self.assertRaises(SystemExit) as error:
                main(["--opensbi", "/unused"])
        self.assertEqual(error.exception.code, 2)
        self.assertIn("require Linux CPU affinity", stderr.getvalue())


class ProtocolTest(unittest.TestCase):
    def test_complete_ordered_fixed_work(self):
        protocol = ConsoleProtocol(CASE)
        for index, line in enumerate(LINES):
            done = protocol.feed(line + "\r\n", index + 1.0)
        self.assertTrue(done)
        self.assertEqual(protocol.ready, 3.0)
        self.assertEqual(protocol.booted, 1.0)
        self.assertEqual(protocol.ended - protocol.started, 1.0)

    def test_reject_corruption_reordering_and_wrong_work(self):
        for bad in (
            LINES[1].replace("42", "43"),
            LINES[3],
            LINES[1].replace("20", "21"),
            LINES[1] + " extra",
        ):
            with self.subTest(bad=bad), self.assertRaises(BenchError):
                protocol = ConsoleProtocol(CASE)
                protocol.feed(LINES[0], 1.0)
                protocol.feed(bad, 2.0)

    def test_reject_ready_without_boot(self):
        protocol = ConsoleProtocol(CASE)
        protocol.feed(LINES[1], 1.0)
        with self.assertRaisesRegex(BenchError, "greeting"):
            protocol.feed(LINES[2], 2.0)

    def test_reject_unresolved_timer_interval(self):
        protocol = ConsoleProtocol(CASE)
        for line in LINES[:-1]:
            protocol.feed(line, 1.0)
        with self.assertRaisesRegex(BenchError, "measurable"):
            protocol.feed(LINES[-1], 1.0)

    def test_boot_requires_application_greeting(self):
        protocol = ConsoleProtocol(dict(CASE, id="boot"))
        protocol.feed(LINES[0], 1.0)
        with self.assertRaisesRegex(BenchError, "application greeting"):
            protocol.feed(LINES[1].replace("allocation", "boot"), 2.0)


class InventoryTest(unittest.TestCase):
    def manifest(self):
        return {
            "cases": [
                {"id": name, "status": "ready", "mandatory": True}
                for name in MANDATORY_CASES
            ]
        }

    def test_missing_heap_remains_mandatory_untested(self):
        manifest = self.manifest()
        manifest["cases"] = [
            case
            for case in manifest["cases"]
            if case["id"] != "heap-regression"
        ]
        require_inventory(manifest)
        heap = next(
            case
            for case in manifest["cases"]
            if case["id"] == "heap-regression"
        )
        self.assertTrue(heap["mandatory"])
        self.assertEqual(heap["status"], "untested")
        self.assertTrue(manifest["provenance"]["suite_inventory"]["partial"])

    def test_false_flags_cannot_disable_required_work(self):
        manifest = self.manifest()
        for case in manifest["cases"]:
            case["mandatory"] = False
        require_inventory(manifest)
        self.assertTrue(all(case["mandatory"] for case in manifest["cases"]))

    def test_deleted_expensive_case_is_restored(self):
        manifest = self.manifest()
        manifest["cases"] = [
            case for case in manifest["cases"] if case["id"] != "computation"
        ]
        require_inventory(manifest)
        computation = next(
            case for case in manifest["cases"] if case["id"] == "computation"
        )
        self.assertEqual(computation["status"], "untested")
        self.assertTrue(computation["mandatory"])

    def test_duplicate_case_ids_fail_closed(self):
        manifest = self.manifest()
        manifest["cases"].append(dict(manifest["cases"][0]))
        with self.assertRaisesRegex(BenchError, "duplicate"):
            require_inventory(manifest)


class EngineConfigTest(unittest.TestCase):
    def verify_fixture(self, config_text, mode):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / ".effective-config"
            executable = Path(directory) / "rv32emu"
            config.write_text(config_text)
            executable.write_bytes(b"fixture")
            args = SimpleNamespace(
                rv32emu_config=config, rv32emu=executable, mode=mode
            )
            with patch(
                "zephyr_bench.subprocess.run",
                return_value=subprocess.CompletedProcess(
                    ["nm", str(executable)], 0, stdout="", stderr=""
                ),
            ):
                return verify_engine(args)

    def test_t2c_requires_jit_and_t2c_configuration(self):
        evidence = self.verify_fixture(
            "CONFIG_SYSTEM=y\nCONFIG_JIT=y\nCONFIG_T2C=y\n", "t2c"
        )
        self.assertEqual(evidence["mode"], "T2C")
        for config in (
            "CONFIG_SYSTEM=y\nCONFIG_JIT=y\n",
            "CONFIG_SYSTEM=y\nCONFIG_T2C=y\n",
        ):
            with self.subTest(config=config), self.assertRaises(BenchError):
                self.verify_fixture(config, "t2c")

    def test_default_lto_build_without_jit_translate_symbol(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / ".effective-config"
            executable = Path(directory) / "rv32emu"
            config.write_text("CONFIG_SYSTEM=y\nCONFIG_JIT=y\nCONFIG_LTO=y\n")
            executable.write_bytes(b"fixture for observed default LTO build")
            args = SimpleNamespace(
                rv32emu_config=config, rv32emu=executable, mode="primary"
            )
            symbols = "0000000000010000 t emit_alu32.lto_priv.0\n0000000000011000 t emit_alu64.lto_priv.0\n"
            with patch(
                "zephyr_bench.subprocess.run",
                return_value=subprocess.CompletedProcess(
                    ["nm", str(executable)], 0, stdout=symbols, stderr=""
                ),
            ):
                evidence = verify_engine(args)
            self.assertEqual(evidence["mode"], "T1")
            self.assertIsNone(evidence["jit_translate_symbol"])
            self.assertEqual(evidence["runtime_jit_execution"], "unverified")

    def test_positive_jit_symbol_contradicts_interpreter_mode(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / ".effective-config"
            executable = Path(directory) / "rv32emu"
            config.write_text("CONFIG_SYSTEM=y\nCONFIG_INTERPRETER_ONLY=y\n")
            executable.write_bytes(b"fixture")
            args = SimpleNamespace(
                rv32emu_config=config, rv32emu=executable, mode="interpreter"
            )
            with patch(
                "zephyr_bench.subprocess.run",
                return_value=subprocess.CompletedProcess(
                    ["nm", str(executable)],
                    0,
                    stdout="00000001 T jit_translate\n",
                    stderr="",
                ),
            ):
                with self.assertRaises(BenchError):
                    verify_engine(args)


class InformationalModeTest(unittest.TestCase):
    def complete_evidence(self):
        manifest = {"schema_version": 1, "provenance": {}, "cases": []}
        records = []
        for case_id in MANDATORY_CASES:
            phase = "boot" if case_id == "boot" else "work"
            manifest["cases"].append(
                {
                    "id": case_id,
                    "mandatory": True,
                    "pairs": 2,
                    "phase": phase,
                    "config": {},
                }
            )
            for pair in range(2):
                records.append(
                    {
                        "case_id": case_id,
                        "pair_id": pair,
                        "correctness": "passed",
                        "phase": phase,
                        "order": (
                            ["reference", "candidate"]
                            if pair == 0
                            else ["candidate", "reference"]
                        ),
                        "reference_seconds": 2.0,
                        "candidate_seconds": 1.0,
                    }
                )
        return manifest, records

    def test_nonprimary_success_never_qualifies_primary_goal(self):
        from system_bench import summarize

        manifest, records = self.complete_evidence()
        for mode in ("interpreter", "t2c"):
            report = qualify_mode(summarize(manifest, records), mode)
            self.assertEqual(report["status"], "informational")
            self.assertEqual(report["correctness_status"], "passed")
            self.assertFalse(report["primary_goal_passed"])

    def test_missing_case_cannot_pass_informational_correctness(self):
        from system_bench import summarize

        manifest, records = self.complete_evidence()
        manifest["cases"] = [
            case
            for case in manifest["cases"]
            if case["id"] != "heap-regression"
        ]
        records = [
            record
            for record in records
            if record["case_id"] != "heap-regression"
        ]
        report = qualify_mode(summarize(manifest, records), "t2c")
        self.assertEqual(report["status"], "not-passed")

    def test_missing_planned_pair_cannot_pass_informational_correctness(self):
        from system_bench import summarize

        manifest, records = self.complete_evidence()
        records.pop()
        report = qualify_mode(summarize(manifest, records), "interpreter")
        self.assertEqual(report["status"], "not-passed")

    def test_slower_correct_runs_remain_successful_information(self):
        from system_bench import summarize

        manifest, records = self.complete_evidence()
        for record in records:
            record["candidate_seconds"] = 4.0
        report = qualify_mode(summarize(manifest, records), "interpreter")
        self.assertEqual(report["status"], "informational")
        self.assertTrue(
            all(case["status"] == "slower" for case in report["cases"])
        )


@unittest.skipUnless(
    hasattr(os, "sched_getaffinity"),
    "process measurements require Linux CPU affinity",
)
class ProcessTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.cpu = min(os.sched_getaffinity(0))

    def tearDown(self):
        self.temp.cleanup()

    def run_program(self, source, timeout=2, case=None):
        script = self.directory / "fixture.py"
        script.write_text(source)
        return observe(
            [sys.executable, str(script)],
            case or CASE,
            timeout,
            self.cpu,
            self.directory / "console.log",
        )

    def test_real_console_and_delayed_work(self):
        result = self.run_program(
            "import time\n"
            + "\n".join(
                f"print({line!r}, flush=True); time.sleep(.03)"
                for line in LINES
            )
            + "\ntime.sleep(30)\n"
        )
        self.assertGreater(result["work_seconds"], 0.015)
        self.assertGreater(result["boot_seconds"], 0)
        self.assertIn(
            "status=PASS", (self.directory / "console.log").read_text()
        )

    def test_boot_waits_for_application_greeting_before_warmup(self):
        case = dict(CASE, id="boot")
        source = (
            f"import time\nprint({LINES[0]!r}, flush=True)\ntime.sleep(.15)\n"
        )
        source += (
            "print('Hello World! qemu_riscv32', flush=True)\ntime.sleep(.15)\n"
        )
        source += "\n".join(
            f"print({line.replace('allocation', 'boot')!r}, flush=True); time.sleep(.02)"
            for line in LINES[1:]
        )
        started = time.perf_counter()
        result = self.run_program(source, case=case)
        self.assertGreater(result["boot_seconds"], 0.15)
        self.assertGreater(
            time.perf_counter() - started - result["boot_seconds"], 0.15
        )
        self.assertGreater(result["work_seconds"], 0.01)

    def test_terminal_console_flushes_line_buffered_output(self):
        result = self.run_program(
            "import time\n"
            + "\n".join(f"print({line!r}); time.sleep(.02)" for line in LINES)
            + "\ntime.sleep(30)\n"
        )
        self.assertGreater(result["work_seconds"], 0.01)

    def test_real_console_rejects_wrong_result(self):
        lines = LINES[:-1] + [LINES[-1].replace("checksum=42", "checksum=43")]
        source = "import time\n" + "\n".join(
            f"print({line!r}, flush=True); time.sleep(.01)" for line in lines
        )
        with self.assertRaisesRegex(BenchError, "unexpected benchmark marker"):
            self.run_program(source)

    def test_closed_pipe_fails_without_waiting_for_timeout(self):
        start = time.perf_counter()
        with self.assertRaisesRegex(BenchError, "console closed"):
            self.run_program("print('not a verdict')\n")
        self.assertLess(time.perf_counter() - start, 1.0)

    def test_timeout_kills_descendant_holding_pipe(self):
        pidfile = self.directory / "child.pid"
        source = (
            "import os, signal, time\n"
            "child=os.fork()\n"
            "if child == 0:\n"
            " signal.signal(signal.SIGTERM, signal.SIG_IGN)\n"
            " time.sleep(30)\n"
            "else:\n"
            f" open({str(pidfile)!r}, 'w').write(str(child))\n"
            " os._exit(0)\n"
        )
        with self.assertRaisesRegex(BenchError, "timeout"):
            self.run_program(source, timeout=0.2)
        pid = int(pidfile.read_text())
        # Linux may retain an adopted zombie until init reaps it.
        deadline = time.monotonic() + 1
        while time.monotonic() < deadline:
            status = Path(f"/proc/{pid}/stat")
            if not status.exists() or status.read_text().split()[2] == "Z":
                break
            time.sleep(0.01)
        else:
            os.kill(pid, signal.SIGKILL)
            self.fail("descendant survived process-group cleanup")


if __name__ == "__main__":
    unittest.main()
