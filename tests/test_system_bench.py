"""Host-only regression checks for system benchmark evidence and gating."""

import copy
import json
import math
import os
import subprocess
import sys
import time
from pathlib import Path
import tempfile
import unittest

from benchmark_stats import amdahl_bound, paired_log_interval
from system_bench import (
    _linux_sample,
    attach_diagnostics,
    main,
    run_linux,
    summarize,
    validate_output_dir,
    write_report,
)


def manifest(*names):
    return {
        "schema_version": 1,
        "provenance": {"source": "host-test fixture, not performance evidence"},
        "cases": [
            {
                "id": name,
                "mandatory": True,
                "pairs": 3,
                "phase": "boot" if name == "boot" else "work",
                "config": {
                    "mode": "T1",
                    "reference": "qemu-tcg",
                    "vcpus": 1,
                    "icount": False,
                    "scope": "boot" if name == "boot" else "fixed-work",
                },
            }
            for name in names
        ],
    }


def records(name, times=(1, 1, 1)):
    return [
        {
            "case_id": name,
            "pair_id": i,
            "reference_seconds": 2,
            "candidate_seconds": value,
            "correctness": "passed",
            "phase": "boot" if name == "boot" else "work",
            "order": (
                ["reference", "candidate"]
                if i % 2 == 0
                else ["candidate", "reference"]
            ),
        }
        for i, value in enumerate(times)
    ]


class StatisticsTests(unittest.TestCase):
    def test_constant_ratio(self):
        result = paired_log_interval([2, 4, 6], [1, 2, 3])
        self.assertAlmostEqual(result["ratio"], 2)
        self.assertAlmostEqual(result["lower"], 2)
        self.assertEqual(result["n"], 3)

    def test_known_two_pair_t_interval(self):
        result = paired_log_interval([2, 4], [1, 1])
        mean = (math.log(2) + math.log(4)) / 2
        margin = 12.706 * math.log(2) / 2
        self.assertAlmostEqual(result["lower"], math.exp(mean - margin))
        self.assertAlmostEqual(result["upper"], math.exp(mean + margin))

    def test_invalid_samples(self):
        for ref, candidate in [
            ([], []),
            ([1], [1]),
            ([1, 2], [1]),
            ([0, 1], [1, 1]),
            ([True, 1], [1, 1]),
            ([math.inf, 1], [1, 1]),
            ([1, 1], [math.nan, 1]),
            (["2", 1], [1, 1]),
            ([1e308, 1e-308], [1e-308, 1e308]),
        ]:
            with self.subTest(ref=ref, candidate=candidate):
                with self.assertRaises(ValueError):
                    paired_log_interval(ref, candidate)

    def test_amdahl_hypothesis(self):
        result = amdahl_bound(0.6, 2, "host-wall")
        self.assertAlmostEqual(result["upper_bound"], 1 / 0.7)
        self.assertIn("Hypothetical", result["assumptions"])
        self.assertEqual(
            amdahl_bound(0.6, math.inf, "host-wall")["upper_bound"], 2.5
        )
        for fraction, scope in [(1.1, "host-wall"), (0.5, "block-counts")]:
            with self.assertRaises(ValueError):
                amdahl_bound(fraction, 2, scope)


class GateTests(unittest.TestCase):
    def test_all_mandatory_cases_including_boot(self):
        data = records("compute") + records("boot")
        self.assertEqual(
            summarize(manifest("compute", "boot"), data)["status"], "passed"
        )
        data[-1]["candidate_seconds"] = 20
        result = summarize(manifest("compute", "boot"), data)
        self.assertEqual(result["status"], "not-passed")
        self.assertEqual(result["cases"][1]["status"], "inconclusive")

    def test_noisy_negative_and_boundary_are_inconclusive(self):
        for times in [(1, 1, 8), (3, 3, 3), (2 / 1.02,) * 3]:
            result = summarize(manifest("compute"), records("compute", times))
            self.assertEqual(result["status"], "not-passed")

    def test_failures_remain_distinct(self):
        for state in ("failed", "unsupported", "untested"):
            data = records("compute")
            data[1]["correctness"] = state
            result = summarize(manifest("compute"), data)
            self.assertEqual(result["cases"][0]["status"], state)
            self.assertEqual(result["status"], "not-passed")

    def test_missing_case_not_hidden_by_other_win(self):
        result = summarize(manifest("compute", "boot"), records("compute"))
        self.assertEqual(result["cases"][1]["status"], "untested")
        self.assertIsNone(result["geometric_mean_supplementary"])
        self.assertEqual(result["status"], "not-passed")

    def test_incomplete_duplicate_and_nonserial_pairs(self):
        original = records("compute")
        variants = [original[:-1], original + [original[0]]]
        for field, value in [
            ("order", ["candidate", "reference"]),
            ("pair_id", 8),
            ("phase", "boot"),
        ]:
            data = copy.deepcopy(original)
            data[0][field] = value
            variants.append(data)
        for data in variants:
            with self.subTest(data=data):
                self.assertEqual(
                    summarize(manifest("compute"), data)["status"], "not-passed"
                )

    def test_profile_cannot_supply_missing_scored_pairs(self):
        data = records("compute")
        for row in data:
            row["phase"] = "profile"
        result = summarize(manifest("compute"), data)
        self.assertEqual(result["status"], "not-passed")
        self.assertEqual(len(result["profile_records"]), 3)
        data += records("compute")
        self.assertEqual(
            summarize(manifest("compute"), data)["status"], "passed"
        )

    def test_observational_measurements_do_not_pass_correctness(self):
        spec = manifest("compute")
        spec["cases"][0]["mandatory"] = False
        data = records("compute")
        for row in data:
            row["correctness"] = "untested"
        result = summarize(spec, data)
        self.assertEqual(result["status"], "observational")
        self.assertEqual(result["cases"][0]["status"], "untested")
        self.assertAlmostEqual(result["cases"][0]["ratio"], 2)
        self.assertAlmostEqual(result["geometric_mean_supplementary"], 2)

    def test_classify_supported_regression_and_small_win(self):
        slower = summarize(manifest("compute"), records("compute", (3,) * 3))
        self.assertEqual(slower["cases"][0]["status"], "slower")
        small = summarize(manifest("compute"), records("compute", (1.99,) * 3))
        self.assertEqual(small["cases"][0]["status"], "below-target")

    def test_nonfinite_evidence_is_rejected_even_in_profiles(self):
        data = records("compute")
        data[0]["candidate_seconds"] = math.nan
        with self.assertRaises(ValueError):
            summarize(manifest("compute"), data)
        data[0]["phase"] = "profile"
        with self.assertRaises(ValueError):
            summarize(manifest("compute"), data)

    def test_invalid_manifest_and_undeclared_cases(self):
        for spec in [{}, manifest("same", "same"), manifest()]:
            with self.assertRaises(ValueError):
                summarize(spec, [])
        with self.assertRaises(ValueError):
            summarize(manifest("compute"), records("unknown"))

    def test_measured_medians_do_not_require_qualification(self):
        for state in ("passed", "failed", "unsupported", "untested"):
            data = records("compute", (1, 3, 10))
            for row, reference in zip(data, (2, 4, 100)):
                row["reference_seconds"] = reference
                row["correctness"] = state
            profile = dict(
                data[0],
                phase="profile",
                reference_seconds=999,
                candidate_seconds=999,
            )
            report = summarize(manifest("compute"), data + [profile])
            with self.subTest(correctness=state):
                self.assertEqual(
                    report["cases"][0]["reference_median_seconds"], 4
                )
                self.assertEqual(
                    report["cases"][0]["candidate_median_seconds"], 3
                )
                self.assertEqual(report["cases"][0]["correctness"], state)
        incomplete = summarize(manifest("compute"), records("compute")[:1])
        self.assertEqual(incomplete["cases"][0]["status"], "invalid")
        self.assertEqual(incomplete["cases"][0]["reference_median_seconds"], 2)

    def test_reason_paragraphs_follow_all_table_rows(self):
        data = records("compute") + records("boot")
        data[0].update(correctness="failed", reason="fixture failure")
        report = summarize(manifest("compute", "boot"), data)
        with tempfile.TemporaryDirectory() as directory:
            json_path, markdown_path = write_report(report, directory)
            saved = json.loads(json_path.read_text())
            self.assertEqual(saved["cases"][0]["reference_median_seconds"], 2)
            lines = markdown_path.read_text().splitlines()
            table_indices = [
                i for i, line in enumerate(lines) if line.startswith("|")
            ]
            self.assertEqual(len(table_indices), 4)
            self.assertEqual(
                table_indices,
                list(range(table_indices[0], table_indices[0] + 4)),
            )
            self.assertIn("Reference median (ms)", lines[table_indices[0]])
            self.assertIn("Candidate median (ms)", lines[table_indices[0]])
            self.assertIn("2000.000 | 1000.000", lines[table_indices[2]])
            reason_index = lines.index("compute: fixture failure")
            self.assertGreater(reason_index, table_indices[-1])

    def test_reports_and_cli(self):
        with tempfile.TemporaryDirectory(
            prefix="system-bench-test-"
        ) as directory:
            report = summarize(manifest("compute"), records("compute"))
            paths = write_report(report, directory)
            self.assertEqual(
                json.loads(paths[0].read_text())["status"], "passed"
            )
            self.assertIn("95% CI", paths[1].read_text())
            source = Path(directory)
            self.assertEqual(
                validate_output_dir(source / "not-created"),
                source / "not-created",
            )
            self.assertFalse((source / "not-created").exists())
            (source / "manifest.json").write_text(
                json.dumps(manifest("compute"))
            )
            (source / "records.json").write_text(json.dumps(records("compute")))
            self.assertEqual(
                main(
                    [
                        "--manifest",
                        str(source / "manifest.json"),
                        "--results",
                        str(source / "records.json"),
                        "--out-dir",
                        str(source / "out"),
                    ]
                ),
                0,
            )
        with self.assertRaises(ValueError):
            write_report(report, "/var/system-bench-test")


class DiagnosticTests(unittest.TestCase):
    def setUp(self):
        self.report = summarize(manifest("compute"), records("compute"))
        self.item = {
            "case_id": "compute",
            "component_seconds": 30,
            "total_seconds": 100,
            "component_scope": "host-wall",
            "total_scope": "host-wall",
            "source_evidence": "controlled-profile.json sha256:fixture-only",
            "hypothetical_speedup": 2,
            "attribution": "non-overlapping-subset",
        }

    def test_prediction_is_separate_from_measured_qualification(self):
        original = copy.deepcopy(self.report)
        result = attach_diagnostics(self.report, [self.item])
        item = result["diagnostics"][0]
        self.assertAlmostEqual(item["fraction"], 0.3)
        self.assertAlmostEqual(item["remaining_fraction"], 0.7)
        self.assertAlmostEqual(item["predicted_overall_speedup"], 1 / 0.85)
        self.assertAlmostEqual(item["theoretical_ceiling"], 1 / 0.7)
        self.assertIn("not independently verified", item["attribution_status"])
        self.assertIn("uninstrumented paired", item["required_experiment"])
        self.assertEqual(result["status"], original["status"])
        self.assertEqual(result["cases"], original["cases"])
        self.assertEqual(self.report, original)

    def test_cpu_scope_and_entire_component_are_explicit(self):
        self.item.update(
            component_scope="total-process-cpu",
            total_scope="total-process-cpu",
            component_seconds=100,
        )
        item = attach_diagnostics(self.report, [self.item])["diagnostics"][0]
        self.assertEqual(item["prediction_scope"], "total-process-cpu")
        self.assertEqual(item["predicted_overall_speedup"], 2)
        self.assertEqual(item["theoretical_ceiling"], "unbounded")
        self.assertIn("do not establish host wall-time", item["condition"])

    def test_reject_invalid_times_counts_and_improvement(self):
        for field, value in [
            ("component_seconds", 0),
            ("component_seconds", -1),
            ("component_seconds", 101),
            ("component_seconds", math.nan),
            ("component_seconds", True),
            ("component_seconds", "30"),
            ("total_seconds", 0),
            ("total_seconds", math.inf),
            ("hypothetical_speedup", 0),
            ("hypothetical_speedup", 0.5),
            ("hypothetical_speedup", math.inf),
        ]:
            item = dict(self.item, **{field: value})
            with self.subTest(field=field, value=value):
                with self.assertRaises(ValueError):
                    attach_diagnostics(self.report, [item])

    def test_reject_unknown_case_scope_overlap_and_missing_evidence(self):
        for field, value in [
            ("case_id", "unknown"),
            ("case_id", ["compute"]),
            ("component_scope", "block-count"),
            ("total_scope", "total-process-cpu"),
            ("attribution", "overlapping-inclusive-profile-counts"),
            ("source_evidence", ""),
            ("source_evidence", "  "),
            ("source_evidence", None),
        ]:
            item = dict(self.item, **{field: value})
            with self.subTest(field=field, value=value):
                with self.assertRaises(ValueError):
                    attach_diagnostics(self.report, [item])
        with self.assertRaises(ValueError):
            attach_diagnostics(self.report, [self.item, self.item])
        with self.assertRaises(ValueError):
            attach_diagnostics(self.report, {})
        with self.assertRaises(ValueError):
            attach_diagnostics(self.report, [None])

    def test_cli_writes_visible_conditional_diagnostic(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            (source / "manifest.json").write_text(
                json.dumps(manifest("compute"))
            )
            (source / "results.json").write_text(json.dumps(records("compute")))
            (source / "diagnostics.json").write_text(json.dumps([self.item]))
            self.assertEqual(
                main(
                    [
                        "--manifest",
                        str(source / "manifest.json"),
                        "--results",
                        str(source / "results.json"),
                        "--diagnostics",
                        str(source / "diagnostics.json"),
                        "--out-dir",
                        str(source / "out"),
                    ]
                ),
                0,
            )
            encoded = json.loads((source / "out/system-bench.json").read_text())
            self.assertAlmostEqual(
                encoded["diagnostics"][0]["predicted_overall_speedup"], 1 / 0.85
            )
            markdown = (source / "out/system-bench.md").read_text()
            for text in (
                "Conditional Amdahl",
                "0.3000",
                "0.7000",
                "2.0000",
                "1.1765",
                "1.4286",
                "not independently verified",
                "Change only",
                "Source evidence:",
            ):
                self.assertIn(text, markdown)

    def test_diagnostic_cannot_qualify_failed_measurement(self):
        report = summarize(manifest("compute"), records("compute", (3,) * 3))
        result = attach_diagnostics(report, [self.item])
        self.assertEqual(result["status"], "not-passed")
        self.assertEqual(result["cases"][0]["status"], "slower")


class LinuxAdapterTests(unittest.TestCase):
    def test_actual_shell_adapter_and_serial_order(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            script = directory / "fake.sh"
            script.write_text(
                'printf "%s\\n" "$1" >> "' + str(directory / "order") + '"\n'
                'if [ "$1" = "ref" ]; then value=200; else value=100; fi\n'
                'echo "boot=$value dhrystone=$value coremark=$value '
                'fork-exec=$value gzip-md5=$value"\n'
            )
            report = run_linux("ref", "candidate", 2, 2, script)
            self.assertEqual(report["status"], "observational")
            self.assertEqual(
                (directory / "order").read_text().splitlines(),
                ["ref", "candidate", "candidate", "ref"],
            )
            self.assertEqual(len(report["cases"]), 5)
            for case in report["cases"]:
                self.assertEqual(case["correctness"], "untested")
                self.assertAlmostEqual(case["ratio"], 2)

    def test_malformed_or_failed_output_is_not_skipped(self):
        for output in [
            "boot=1",
            "boot=nan dhrystone=1 coremark=1 fork-exec=1 gzip-md5=1",
            "boot=1 dhrystone=1 coremark=1 fork-exec=1 boot=1",
        ]:
            with tempfile.TemporaryDirectory() as directory:
                script = Path(directory) / "fake.sh"
                script.write_text("echo '" + output + "'\n")
                report = run_linux("ref", "candidate", 2, 2, script)
                self.assertEqual(len(report["records"]), 5)
                self.assertEqual(report["status"], "failed")
                self.assertTrue(
                    all(c["status"] == "failed" for c in report["cases"])
                )
        with self.assertRaises(ValueError):
            run_linux("ref", "candidate", 1)

    @unittest.skipUnless(Path("/proc").is_dir(), "Linux process cleanup")
    def test_timeout_kills_child_in_separate_session(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            child_pid = directory / "child.pid"
            script = directory / "fake.sh"
            code = (
                "import subprocess, sys, time; "
                "p=subprocess.Popen([sys.executable, '-c', "
                "'import time; time.sleep(60)'], "
                "start_new_session=True); "
                f"open({str(child_pid)!r}, 'w').write(str(p.pid)); "
                "time.sleep(60)"
            )
            import shlex

            script.write_text(
                "exec "
                + shlex.quote(sys.executable)
                + " -c "
                + shlex.quote(code)
                + "\n"
            )
            with self.assertRaises(subprocess.TimeoutExpired):
                _linux_sample(script, "unused", 0.8)
            pid = int(child_pid.read_text())
            for _ in range(20):
                status = Path(f"/proc/{pid}/status")
                try:
                    text = status.read_text()
                except (FileNotFoundError, ProcessLookupError):
                    break
                if "State:\tZ" in text:
                    break
                time.sleep(0.01)
            else:
                os.kill(pid, 9)
                self.fail("timed-out run left an emulator descendant running")


if __name__ == "__main__":
    unittest.main()
