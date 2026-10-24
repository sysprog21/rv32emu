#!/usr/bin/env python3
"""Exercise collection, offline compilation, replay, and identity rejection."""

import argparse
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile


def run(*args, success=True, env=None):
    result = subprocess.run(
        [str(arg) for arg in args],
        capture_output=True,
        text=True,
        timeout=120,
        env=env,
    )
    if (result.returncode == 0) != success:
        raise AssertionError(f"{args}: {result.returncode}\n{result.stderr}")
    return result


def profile_rows(emulator, guest):
    """Return a -p run's report and its block rows, split into columns."""
    report = (emulator.parent / f"{guest.name}.prof").read_text()
    rows = [
        line.split("|") for line in report.splitlines() if line.startswith("0x")
    ]
    return report, rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", required=True, type=Path)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--llvm-config", required=True)
    parser.add_argument("--native-pgo", action="store_true")
    parser.add_argument("--profile-runtime", type=Path)
    parser.add_argument("--thin-lto", action="store_true")
    args = parser.parse_args()
    # Per-user groups make 002 a common umask, under which compilers create
    # group-writable files; the object cache must still be reusable.
    os.umask(0o002)
    compiler_options = []
    if args.thin_lto:
        compiler_options.append("--thin-lto")

    def compile_aot(profile, output, *options, success=True):
        return run(
            "python3",
            "tools/aot.py",
            profile,
            "-o",
            output,
            "--llvm-config",
            args.llvm_config,
            *options,
            success=success,
        )

    emulator = args.emulator.resolve()
    with tempfile.TemporaryDirectory(
        prefix="aot-test-", dir=emulator.parent
    ) as tmp:
        work = Path(tmp)
        guest = work / work.name
        run(
            args.cc,
            "-march=rv32imc",
            "-mabi=ilp32",
            "-nostdlib",
            "-static",
            "-fno-pie",
            "-no-pie",
            "-Wl,--no-relax",
            "tests/aot.S",
            "-o",
            guest,
        )
        profile = work / "profile"
        library = work / "guest.so"
        run(emulator, "-C", profile, guest)
        compile_aot(profile, library, *compiler_options)
        if args.native_pgo:
            instrumented = work / "instrumented.so"
            runtime = (
                ["--profile-runtime", args.profile_runtime]
                if args.profile_runtime
                else []
            )
            compile_aot(
                profile,
                instrumented,
                "--profile-generate",
                *runtime,
                *compiler_options,
            )
            env = dict(
                os.environ, LLVM_PROFILE_FILE=str(work / "%p-%m.profraw")
            )
            baseline, trained = (
                work / "pgo-baseline.json",
                work / "trained.json",
            )
            run(emulator, "-d", baseline, guest)
            training = run(
                emulator, "-A", instrumented, "-d", trained, guest, env=env
            )
            assert re.search(r"AOT: entries=[1-9][0-9]*", training.stderr)
            assert json.loads(baseline.read_text()) == json.loads(
                trained.read_text()
            )
            raw = list(work.glob("*.profraw"))
            assert raw, "instrumented artifact produced no native profile"
            bindir = Path(run(args.llvm_config, "--bindir").stdout.strip())
            data = work / "native.profdata"
            run(bindir / "llvm-profdata", "merge", *raw, "-o", data)
            optimized = work / "optimized.so"
            compile_aot(
                profile, optimized, "--profile-use", data, *compiler_options
            )
            replayed = work / "pgo-replay.json"
            for guest_args in ([], ["cold"]):
                reference = run(emulator, "-d", baseline, guest, *guest_args)
                replay = run(
                    emulator,
                    "-A",
                    optimized,
                    "-d",
                    replayed,
                    guest,
                    *guest_args,
                )
                assert re.search(r"AOT: entries=[1-9][0-9]*", replay.stderr)
                assert replay.stdout == reference.stdout
                assert json.loads(baseline.read_text()) == json.loads(
                    replayed.read_text()
                )
        # Separate processes exercise different host addresses under ASLR.
        for guest_args in ([], ["cold"]):
            baseline = work / "baseline.json"
            replay = work / "replay.json"
            run(emulator, "-d", baseline, guest, *guest_args)
            run(emulator, "-A", library, "-p", "-d", replay, guest, *guest_args)
            assert json.loads(baseline.read_text()) == json.loads(
                replay.read_text()
            )
            report, rows = profile_rows(emulator, guest)
            assert any(row[5].strip() == "true" for row in rows), report
        # FENCE.I is an explicit boundary: discard bindings and refuse to
        # turn a run that changes instruction visibility into an artifact.
        run(emulator, "-A", library, "-p", guest, "fence", "now")
        report, rows = profile_rows(emulator, guest)
        assert all(row[5].strip() == "false" for row in rows), report
        rejected = work / "fence-profile"
        run(emulator, "-C", rejected, guest, "fence", "now", success=False)
        assert not (rejected / "manifest").exists()
        (emulator.parent / f"{guest.name}.prof").unlink()
        # A deployment can execute without the LLVM collector beside it.
        deployed = work / "rv32emu-deployed"
        deployed.write_bytes(emulator.read_bytes())
        deployed.chmod(0o700)
        run(deployed, "-A", library, guest)
        no_collector = work / "no-collector"
        run(deployed, "-C", no_collector, guest, success=False)
        assert not (no_collector / "manifest").exists()
        # A loadable collector with no matching source/layout signature fails
        # before its entry point can inspect the VM.
        wrong_collector = work / "libt2c.so"
        wrong_collector.write_bytes(library.read_bytes())
        mismatch = work / "mismatched-collector"
        run(deployed, "-C", mismatch, guest, success=False)
        assert not (mismatch / "manifest").exists()
        wrong_collector.unlink()
        # An older LLVM reader must be rejected before invoking its tools.
        manifest = profile / "manifest"
        original_manifest = manifest.read_text()
        words = original_manifest.splitlines()
        words[6] = f"{24:016x}"
        manifest.write_text("\n".join(words) + "\n")
        compile_aot(profile, work / "bad-llvm.so", success=False)
        manifest.write_text(original_manifest)
        # Selection is explicit, incompatible and absent artifacts fail closed.
        run(emulator, "-A", library, "-m", guest, success=False)
        other_emulator = work / "rv32emu-changed"
        other_emulator.write_bytes(emulator.read_bytes() + b"changed")
        other_emulator.chmod(0o700)
        run(other_emulator, "-A", library, guest, success=False)
        run(emulator, "-A", work / "absent.so", guest, success=False)
        changed = work / "changed"
        changed.write_bytes(guest.read_bytes() + b"changed")
        run(emulator, "-A", library, changed, success=False)
        run(emulator, "-C", profile, guest, success=False)
        # Recompiling identical bitcode reuses complete cached objects.
        cached_objects = {
            path: path.stat().st_mtime_ns
            for path in (profile / ".objects").glob("*.o")
        }
        assert cached_objects
        compile_aot(profile, library)
        assert all(
            path.stat().st_mtime_ns == stamp
            for path, stamp in cached_objects.items()
        )
        incremental = work / "incremental-profile"
        replay_collect = run(emulator, "-A", library, "-C", incremental, guest)
        assert replay_collect.stdout == run(emulator, guest).stdout
        assert (incremental / "manifest").exists()
        # An incomplete collection must not become an apparently valid artifact.
        (profile / "manifest").unlink()
        compile_aot(profile, library, success=False)
        # Exercise cycle accounting, fault helpers, and the remaining memory
        # addressing forms through actual offline-generated code as well.
        for name, arch in (
            ("aot-divrem", "rv32im"),
            ("aot-exit", "rv32i"),
            ("load-x0", "rv32i"),
            ("jit-cycles", "rv32i_zicsr"),
            ("jit-misalign", "rv32ic_zicsr"),
            ("aot-misalign-count", "rv32i_zicsr"),
            ("aot-address-wrap", "rv32ic_zicsr"),
            ("jit-memory-address", "rv32imc"),
        ):
            guest = work / name
            profile = work / f"{name}-profile"
            library = work / f"{name}.so"
            run(
                args.cc,
                f"-march={arch}",
                "-mabi=ilp32",
                "-nostdlib",
                "-static",
                "-fno-pie",
                "-no-pie",
                f"tests/{name}.S",
                "-o",
                guest,
            )
            baseline = run(emulator, "-C", profile, guest)
            compile_aot(profile, library)
            replay = run(
                emulator,
                "-A",
                library,
                guest,
                *(["wrap"] if name == "aot-address-wrap" else []),
            )
            # Identical output alone could come from the fallback tiers.
            assert re.search(r"AOT: entries=[1-9][0-9]*", replay.stderr), (
                name + ": replay ran no native code\n" + replay.stderr
            )
            if name == "aot-address-wrap":
                # The wrapping replay leaves each block at its first access;
                # the training addresses run the whole block natively.
                native = run(emulator, "-A", library, guest)
                assert re.search(r"AOT: entries=[1-9][0-9]*", native.stderr), (
                    name
                    + ": native replay ran no native code\n"
                    + native.stderr
                )
                assert baseline.stdout == native.stdout, (
                    name + ": native replay miscounted instructions"
                )
            if name == "aot-divrem":
                # Recompiling reuses the cached objects, which must pass the
                # private-cache check under the umask set in main().
                cached = work / "divrem-cached.so"
                compile_aot(profile, cached)
                if args.native_pgo:
                    instrumented = work / "divrem-instrumented.so"
                    runtime = (
                        ["--profile-runtime", args.profile_runtime]
                        if args.profile_runtime
                        else []
                    )
                    compile_aot(
                        profile,
                        instrumented,
                        "--profile-generate",
                        *runtime,
                        *compiler_options,
                    )
                    env = dict(
                        os.environ,
                        LLVM_PROFILE_FILE=str(work / "divrem-%p.profraw"),
                    )
                    training = run(emulator, "-A", instrumented, guest, env=env)
                    assert re.search(
                        r"AOT: entries=[1-9][0-9]*", training.stderr
                    )
                    data = work / "divrem.profdata"
                    run(
                        bindir / "llvm-profdata",
                        "merge",
                        *work.glob("divrem-*.profraw"),
                        "-o",
                        data,
                    )
                    compile_aot(
                        profile,
                        library,
                        "--profile-use",
                        data,
                        *compiler_options,
                    )
                    replay = run(emulator, "-A", library, guest)
                    assert re.search(r"AOT: entries=[1-9][0-9]*", replay.stderr)
            assert baseline.stdout == replay.stdout
    print("AOT collection/replay/fallback/identity checks passed")


if __name__ == "__main__":
    main()
