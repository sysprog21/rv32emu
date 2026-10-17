#!/usr/bin/env python3
"""Validate and report paired Linux and Zephyr host wall-time measurements.

Drivers own OS launch and correctness checks. Manifest cases declare id,
mandatory, planned pairs, phase (work or boot), and config. Records declare
case_id, pair_id,
phase, correctness, order, reference_seconds, and candidate_seconds. Pair IDs
start at zero; execution order alternates reference-first and candidate-first.
Profile records remain evidence and never count as scored samples.
"""

import argparse
import json
import math
import os
import signal
import statistics
import subprocess
import time
from pathlib import Path

from benchmark_stats import amdahl_bound, paired_log_interval


STATES = {"passed", "failed", "unsupported", "untested"}


def summarize(manifest, records):
    """Fail closed on incomplete evidence; gate every mandatory case."""
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
        raise ValueError("manifest schema_version must be 1")
    json.dumps(manifest, allow_nan=False)
    json.dumps(records, allow_nan=False)
    cases = manifest.get("cases")
    if not isinstance(cases, list) or not cases:
        raise ValueError("manifest must declare cases")
    if not isinstance(manifest.get("provenance"), dict):
        raise ValueError("manifest must declare provenance")
    ids = set()
    for case in cases:
        if (
            not isinstance(case, dict)
            or not isinstance(case.get("id"), str)
            or not case["id"]
            or case["id"] in ids
            or type(case.get("mandatory")) is not bool
            or case.get("phase", "work") not in ("work", "boot")
            or not isinstance(case.get("config"), dict)
            or type(case.get("pairs")) is not int
            or case["pairs"] < 2
        ):
            raise ValueError("invalid or duplicate manifest case")
        ids.add(case["id"])
    if not isinstance(records, list):
        raise ValueError("results must be a list of pair records")
    grouped = {case_id: [] for case_id in ids}
    profiles = []
    for record in records:
        if (
            not isinstance(record, dict)
            or not isinstance(record.get("case_id"), str)
            or record["case_id"] not in ids
        ):
            raise ValueError("result references an undeclared case")
        if record.get("phase") not in ("work", "boot", "profile"):
            raise ValueError("result must declare work, boot or profile phase")
        if record.get("correctness") not in STATES:
            raise ValueError("result must declare a correctness state")
        if record["phase"] == "profile":
            profiles.append(record)
        else:
            grouped[record["case_id"]].append(record)
    outcomes = []
    for case in cases:
        samples = grouped[case["id"]]
        outcome = {"id": case["id"], "mandatory": case["mandatory"]}
        states = {sample["correctness"] for sample in samples}
        correctness = next(
            (
                state
                for state in ("failed", "unsupported", "untested")
                if state in states
            ),
            "passed" if samples else "untested",
        )
        outcome["correctness"] = correctness
        outcome["status"] = correctness
        reasons = [sample["reason"] for sample in samples if "reason" in sample]
        if reasons:
            outcome["reason"] = str(reasons[0])
        for engine in ("reference", "candidate"):
            times = [
                sample[f"{engine}_seconds"]
                for sample in samples
                if isinstance(sample.get(f"{engine}_seconds"), (int, float))
                and not isinstance(sample[f"{engine}_seconds"], bool)
                and math.isfinite(sample[f"{engine}_seconds"])
                and sample[f"{engine}_seconds"] > 0
            ]
            if times:
                outcome[f"{engine}_median_seconds"] = statistics.median(times)
        if correctness == "passed" or (
            correctness == "untested"
            and samples
            and all(
                "reference_seconds" in s and "candidate_seconds" in s
                for s in samples
            )
        ):
            try:
                _validate_pairs(case, samples)
                interval = paired_log_interval(
                    [s["reference_seconds"] for s in samples],
                    [s["candidate_seconds"] for s in samples],
                )
                outcome.update(interval)
                if correctness == "passed":
                    if interval["lower"] > 1.02:
                        outcome["status"] = "passed"
                    elif interval["upper"] < 1:
                        outcome["status"] = "slower"
                    elif interval["upper"] <= 1.02:
                        outcome["status"] = "below-target"
                    else:
                        outcome["status"] = "inconclusive"
            except (ValueError, KeyError, TypeError) as error:
                outcome["status"] = "invalid"
                outcome["reason"] = str(error)
        outcomes.append(outcome)
    mandatory = [case for case in outcomes if case["mandatory"]]
    passed = bool(mandatory) and all(c["status"] == "passed" for c in mandatory)
    summary_cases = mandatory or outcomes
    ratios = [c["ratio"] for c in summary_cases if "ratio" in c]
    return {
        "schema_version": 1,
        "status": (
            "passed"
            if passed
            else (
                "not-passed"
                if mandatory
                else (
                    "failed"
                    if any(
                        c["status"] in ("failed", "invalid") for c in outcomes
                    )
                    else "observational"
                )
            )
        ),
        "rule": "Every mandatory case: paired 95% CI lower ratio > 1.02",
        "cases": outcomes,
        "geometric_mean_supplementary": (
            math.exp(sum(map(math.log, ratios)) / len(ratios))
            if len(ratios) == len(summary_cases) and ratios
            else None
        ),
        "manifest": manifest,
        "records": records,
        "profile_records": profiles,
        "limitations": (
            "These are pointwise 95% intervals, not simultaneous intervals. "
            "Input provenance and correctness are driver attestations. "
            "Profiling is separate from scored runs."
        ),
    }


def _validate_pairs(case, samples):
    phase = case.get("phase", "work")
    seen = set()
    for sample in samples:
        pair_id = sample.get("pair_id")
        if type(pair_id) is not int or pair_id < 0 or pair_id in seen:
            raise ValueError("invalid or duplicate pair_id")
        seen.add(pair_id)
        expected = ["reference", "candidate"]
        if pair_id % 2:
            expected.reverse()
        if sample.get("order") != expected or sample["phase"] != phase:
            raise ValueError("pair order or measurement phase mismatch")
    if seen != set(range(len(samples))):
        raise ValueError("pair IDs must be contiguous from zero")
    expected_pairs = case.get("pairs")
    if expected_pairs != len(samples):
        raise ValueError("missing or extra planned pairs")


def attach_diagnostics(report, diagnostics):
    """Attach conditional Amdahl predictions without changing measured results.

    Each entry supplies one non-overlapping component and its complete total
    in matching host-wall or total-process-cpu scope, an evidence reference,
    and a hypothetical speedup. Attribution remains an unverified input.
    """
    if not isinstance(diagnostics, list):
        raise ValueError("diagnostics must be a list")
    json.dumps(diagnostics, allow_nan=False)
    known_cases = {case["id"] for case in report["cases"]}
    seen = set()
    predictions = []
    for item in diagnostics:
        if not isinstance(item, dict):
            raise ValueError("each diagnostic must be an object")
        case_id = item.get("case_id")
        if not isinstance(case_id, str) or case_id not in known_cases:
            raise ValueError("diagnostic references an unknown case")
        if case_id in seen:
            raise ValueError("only one component per case avoids overlap")
        seen.add(case_id)
        scope = item.get("component_scope")
        if (
            scope not in ("host-wall", "total-process-cpu")
            or item.get("total_scope") != scope
        ):
            raise ValueError(
                "component and total must have matching time scope"
            )
        if item.get("attribution") != "non-overlapping-subset":
            raise ValueError("component must be a non-overlapping total subset")
        evidence = item.get("source_evidence")
        if not isinstance(evidence, str) or not evidence.strip():
            raise ValueError("diagnostic needs source evidence provenance")
        for field in (
            "component_seconds",
            "total_seconds",
            "hypothetical_speedup",
        ):
            value = item.get(field)
            if (
                isinstance(value, bool)
                or not isinstance(value, (int, float))
                or not math.isfinite(value)
                or value <= 0
            ):
                raise ValueError(f"{field} must be finite and positive")
        component = item["component_seconds"]
        total = item["total_seconds"]
        speedup = item["hypothetical_speedup"]
        if component > total or speedup < 1:
            raise ValueError(
                "component exceeds total or improvement is below 1"
            )
        fraction = component / total
        prediction = amdahl_bound(fraction, speedup, scope)
        ceiling = amdahl_bound(fraction, math.inf, scope)
        predictions.append(
            {
                **item,
                "fraction": fraction,
                "remaining_fraction": 1 - fraction,
                "prediction_scope": scope,
                "predicted_overall_speedup": prediction["upper_bound"],
                "theoretical_ceiling": ceiling["upper_bound"],
                "attribution_status": "supplied; not independently verified",
                "condition": (
                    "Prediction assumes the component improves by the supplied "
                    "factor while remaining time and work stay constant. The "
                    "ceiling assumes that component takes no time. CPU-scope "
                    "predictions do not establish host wall-time improvements."
                ),
                "required_experiment": (
                    "Change only the proposed component; verify equivalent "
                    "work, correctness, component attribution without overlap, "
                    "the same total time scope, and stable remaining time. "
                    "Then repeat uninstrumented paired host wall-time runs "
                    "to test the gate. A hot function alone does not establish "
                    "a bottleneck or cause."
                ),
            }
        )
    return dict(report, diagnostics=predictions)


def validate_output_dir(out_dir):
    """Check the output location before launch without creating files."""
    out = Path(out_dir).resolve()
    roots = (Path("build").resolve(), Path("/tmp").resolve())
    if not any(out == root or root in out.parents for root in roots):
        raise ValueError("output directory must be under build/ or /tmp/")
    return out


def write_report(report, out_dir):
    """Write JSON and Markdown only under build/ or /tmp/."""
    out = validate_output_dir(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    json_path = out / "system-bench.json"
    markdown_path = out / "system-bench.md"
    encoded = json.dumps(report, indent=2, allow_nan=False) + "\n"
    lines = [
        f"System benchmark: {report['status']}",
        "",
        report["rule"],
        "",
        "| Case | Required | Correctness | Result | Reference median (ms) | "
        "Candidate median (ms) | Ratio (95% CI) |",
        "| --- | --- | --- | --- | --- | --- | --- |",
    ]
    reasons = []
    for case in report["cases"]:
        interval = (
            f"{case['ratio']:.4f} ({case['lower']:.4f}, {case['upper']:.4f})"
            if "ratio" in case
            else "unavailable"
        )
        name = case["id"].replace("|", "\\|").replace("\n", " ")
        medians = [
            (
                f"{case[f'{engine}_median_seconds'] * 1000:.3f}"
                if f"{engine}_median_seconds" in case
                else "unavailable"
            )
            for engine in ("reference", "candidate")
        ]
        lines.append(
            f"| {name} | {case['mandatory']} | {case['correctness']} "
            f"| {case['status']} | {medians[0]} | {medians[1]} | {interval} |"
        )
        if "reason" in case:
            reasons.extend(["", f"{name}: {case['reason']}", ""])
    lines.extend(reasons)
    lines.extend(
        [
            "",
            report["limitations"],
            "",
            "Full configuration, provenance, and samples: system-bench.json",
            "",
        ]
    )
    note = report["manifest"]["provenance"].get("correctness_note")
    if note:
        lines.extend([str(note), ""])
    geometric_mean = report["geometric_mean_supplementary"]
    if geometric_mean is not None:
        lines.extend(
            [f"Supplementary geometric mean ratio: {geometric_mean:.4f}", ""]
        )
    for item in report.get("diagnostics", []):
        name = item["case_id"].replace("|", "\\|").replace("\n", " ")
        evidence = item["source_evidence"].replace("\n", " ")
        ceiling = item["theoretical_ceiling"]
        if isinstance(ceiling, (int, float)):
            ceiling = f"{ceiling:.4f}"
        lines.extend(
            [
                f"Conditional Amdahl diagnostic for {name}",
                "",
                "Supplied attribution; not independently verified. "
                "This prediction is separate from measured results "
                "and qualification.",
                "",
                "| Time scope | Component fraction | Remaining fraction | "
                "Assumed component speedup | Predicted overall speedup | "
                "Theoretical ceiling |",
                "| --- | --- | --- | --- | --- | --- |",
                f"| {item['prediction_scope']} | {item['fraction']:.4f} | "
                f"{item['remaining_fraction']:.4f} | "
                f"{item['hypothetical_speedup']:.4f} | "
                f"{item['predicted_overall_speedup']:.4f} | {ceiling} |",
                "",
                f"Source evidence: {evidence}",
                "",
                item["condition"],
                "",
                item["required_experiment"],
                "",
            ]
        )
    json_path.write_text(encoded)
    markdown_path.write_text("\n".join(lines))
    return json_path, markdown_path


LINUX_CASES = ("boot", "dhrystone", "coremark", "fork-exec", "gzip-md5")


def _kill_linux_run(process):
    # Expect can give its emulator a separate session through a PTY.
    # Kill descendants too, before their parent can orphan them.
    descendants = []

    def collect(pid):
        try:
            children = Path(f"/proc/{pid}/task/{pid}/children").read_text()
        except OSError:
            return
        for child in children.split():
            child = int(child)
            collect(child)
            descendants.append(child)

    collect(process.pid)
    for pid in descendants:
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


def _linux_sample(script, emulator, timeout):
    process = subprocess.Popen(
        ["bash", str(script), str(emulator), "1"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        start_new_session=True,
    )
    started = time.monotonic()
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except BaseException:
        _kill_linux_run(process)
        process.communicate()
        raise
    elapsed = time.monotonic() - started
    if process.returncode:
        raise ValueError(
            f"Linux benchmark exited {process.returncode}: {stderr[-500:]}"
        )
    fields = stdout.split()
    if len(fields) != len(LINUX_CASES):
        raise ValueError("Linux benchmark must emit exactly five case timings")
    timings = {}
    for field in fields:
        name, separator, value = field.partition("=")
        if separator != "=" or name not in LINUX_CASES or name in timings:
            raise ValueError("invalid or duplicate Linux benchmark case")
        try:
            milliseconds = float(value)
        except ValueError as error:
            raise ValueError("invalid Linux benchmark timing") from error
        if not math.isfinite(milliseconds) or milliseconds <= 0:
            raise ValueError(
                "Linux benchmark timing must be finite and positive"
            )
        timings[name] = milliseconds / 1000
    return timings, elapsed


def run_linux(reference, candidate, runs=5, timeout=7200, script=None):
    """Measure serial Linux pairs; prompt checks leave correctness untested."""
    if type(runs) is not int or runs < 2:
        raise ValueError("Linux comparison requires at least two planned pairs")
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("timeout must be finite and positive")
    script = Path(script or Path(__file__).with_name("system-bench.sh"))
    manifest = {
        "schema_version": 1,
        "provenance": {
            "os": "Linux",
            "reference": str(reference),
            "candidate": str(candidate),
            "script": str(script.resolve()),
            "image": "build/linux-image",
            "pairs": runs,
            "correctness_note": (
                "The legacy shell checks login and prompts only. These timings "
                "are observational; benchmark output correctness is untested."
            ),
        },
        "cases": [
            {
                "id": name,
                "mandatory": False,
                "pairs": runs,
                "phase": "boot" if name == "boot" else "work",
                "config": {
                    "comparison": "Linux emulator baseline/candidate",
                    "clock": "host-wall",
                    "timeout_seconds": timeout,
                },
            }
            for name in LINUX_CASES
        ],
    }
    records = []
    for pair_id in range(runs):
        order = ["reference", "candidate"]
        if pair_id % 2:
            order.reverse()
        samples = {}
        failure = None
        for engine in order:
            try:
                samples[engine] = _linux_sample(
                    script,
                    reference if engine == "reference" else candidate,
                    timeout,
                )
            except (OSError, ValueError, subprocess.TimeoutExpired) as error:
                failure = str(error)
                break
        for name in LINUX_CASES:
            row = {
                "case_id": name,
                "pair_id": pair_id,
                "order": order,
                "phase": "boot" if name == "boot" else "work",
                "correctness": "failed" if failure else "untested",
            }
            if failure:
                row["reason"] = failure
            else:
                row["reference_seconds"] = samples["reference"][0][name]
                row["candidate_seconds"] = samples["candidate"][0][name]
                row["launch_wall_seconds"] = {
                    engine: sample[1] for engine, sample in samples.items()
                }
            records.append(row)
        if failure:
            break
    return summarize(manifest, records)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Example entry in a --diagnostics JSON list:\n"
            '{"case_id":"compute","component_seconds":30,"total_seconds":100,\n'
            ' "component_scope":"host-wall","total_scope":"host-wall",\n'
            ' "source_evidence":"profile.json sha256:...",\n'
            ' "hypothetical_speedup":2,\n'
            ' "attribution":"non-overlapping-subset"}\n'
            "Use one component per case. Fractions require separate measured "
            "time evidence; block counts and hot-function names do not suffice."
        ),
    )
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--results", type=Path)
    parser.add_argument(
        "--diagnostics",
        type=Path,
        help="JSON attribution list for conditional Amdahl predictions",
    )
    parser.add_argument("--linux-reference", type=Path)
    parser.add_argument("--linux-candidate", type=Path)
    parser.add_argument(
        "--runs",
        type=int,
        default=5,
        help="planned serial pairs for Linux measurement (minimum 2)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=7200,
        help="timeout in seconds for each Linux shell invocation",
    )
    parser.add_argument(
        "--out-dir",
        required=True,
        type=Path,
        help="report directory under build/ or /tmp/",
    )
    args = parser.parse_args(argv)
    try:
        validate_output_dir(args.out_dir)
        if args.linux_reference or args.linux_candidate:
            if not (args.linux_reference and args.linux_candidate):
                raise ValueError("both Linux emulator paths are required")
            if args.manifest or args.results:
                raise ValueError("choose Linux measurement or saved results")
            report = run_linux(
                args.linux_reference,
                args.linux_candidate,
                args.runs,
                args.timeout,
            )
        else:
            if not (args.manifest and args.results):
                raise ValueError("both --manifest and --results are required")
            manifest = json.loads(args.manifest.read_text())
            records = json.loads(args.results.read_text())
            report = summarize(manifest, records)
        if args.diagnostics:
            report = attach_diagnostics(
                report, json.loads(args.diagnostics.read_text())
            )
        write_report(report, args.out_dir)
    except (OSError, ValueError, TypeError) as error:
        parser.exit(2, f"system-bench: {error}\n")
    print(f"System benchmark: {report['status']}")
    if any(case["status"] in ("failed", "invalid") for case in report["cases"]):
        return 1
    return 0 if report["status"] in ("passed", "observational") else 1


if __name__ == "__main__":
    raise SystemExit(main())
