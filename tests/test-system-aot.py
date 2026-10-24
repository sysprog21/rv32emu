#!/usr/bin/env python3
"""Replay MMU, x0, fault, and timer tests through system AOT artifacts."""

import argparse
import os
import pty
import select
import shutil
import time
import tty
from pathlib import Path
import re
import subprocess
import tempfile


def run(*command, cwd=None):
    result = subprocess.run(
        [str(word) for word in command],
        capture_output=True,
        text=True,
        timeout=120,
        cwd=cwd,
    )
    if result.returncode:
        raise RuntimeError(f"{command}: {result.returncode}\n{result.stderr}")
    return result


def compile_aot(llvm_config, output, *profiles):
    return run(
        "python3",
        "tools/aot.py",
        *profiles,
        "-o",
        output,
        "--llvm-config",
        llvm_config,
    )


def run_reboot(command, manifest=None):
    master, slave = pty.openpty()
    tty.setraw(slave)
    process = subprocess.Popen(
        [str(word) for word in command], stdin=slave, stdout=slave, stderr=slave
    )
    os.close(slave)
    output = bytearray()
    boots = 0
    deadline = time.monotonic() + 60
    try:
        while process.poll() is None:
            if time.monotonic() > deadline:
                raise TimeoutError(f"reboot test timed out: {output!r}")
            if not select.select([master], [], [], 0.1)[0]:
                continue
            try:
                chunk = os.read(master, 65536)
            except OSError:
                break
            output.extend(chunk)
            observed = output.count(b"AOT_REBOOT_READY")
            if observed > boots:
                boots = observed
                if manifest is not None:
                    assert (
                        not manifest.exists()
                    ), "reboot prematurely finalized profile"
                os.write(master, b"r" if boots == 1 else b"q")
        process.wait(timeout=5)
        while select.select([master], [], [], 0)[0]:
            try:
                output.extend(os.read(master, 65536))
            except OSError:
                break
        assert process.returncode == 0 and boots == 2, output
        return output.decode(errors="replace")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", type=Path, required=True)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--objcopy", required=True)
    parser.add_argument("--llvm-config", required=True)
    parser.add_argument("--rvc", action="store_true")
    args = parser.parse_args()
    emulator = args.emulator.resolve()
    with tempfile.TemporaryDirectory(
        prefix="system-aot-", dir=emulator.parent
    ) as temporary:
        work = Path(temporary)
        cases = [
            ("system-mmu-cache", "SYSTEM MMU cache OK"),
            ("system-trap-cycles", "SYSTEM trap cycles OK"),
            ("system-priv", "SYSTEM privilege OK"),
            ("system-aot-counters", "SYSTEM AOT counters OK"),
            ("system-aot-status", "SYSTEM AOT status OK"),
            ("system-aot-atomic", "SYSTEM AOT atomic OK"),
            ("system-aot-fence", "SYSTEM AOT fence OK"),
            ("system-aot-asid", "SYSTEM AOT ASID scope OK"),
            ("system-aot-collection", "SYSTEM AOT collection OK"),
            ("system-aot-edges", "SYSTEM AOT static edges OK"),
            ("system-aot-binding", "SYSTEM AOT binding OK"),
            ("system-aot-trap-cache", "SYSTEM AOT trap cache OK"),
            ("system-aot-remap", "SYSTEM AOT remap OK"),
        ]
        if args.rvc:
            cases.append(("system-aot-edges-rvc", "SYSTEM AOT static edges OK"))
            cases.append(("system-aot-binding-rvc", "SYSTEM AOT binding OK"))
            cases.append(
                ("system-aot-collection-rvc", "SYSTEM AOT collection OK")
            )
        for case, expected in cases:
            elf, image = work / f"{case}.elf", work / f"{case}.bin"
            rvc = case.endswith("-rvc")
            run(
                args.cc,
                "-march=rv32imac_zicsr" if rvc else "-march=rv32ia_zicsr",
                f"-DAOT_RVC={int(rvc)}",
                "-mabi=ilp32",
                "-nostdlib",
                "-static",
                "-fno-pie",
                "-no-pie",
                "-Wl,-e,_start,-Ttext=0,--build-id=none",
                f"tests/{case.removesuffix('-rvc')}.S",
                "-o",
                elf,
            )
            run(args.objcopy, "-O", "binary", elf, image)
            profile, library = work / f"{case}-profile", work / f"{case}.so"
            command = [
                emulator,
                "-s",
                "4096",
                "-B",
                "14",
                "-b",
                "root=/dev/null",
                "-k",
                image,
            ]
            baseline = run(*command)
            collected = run(*command, "-C", profile)
            if case.startswith("system-aot-collection"):
                # A nonnative boundary must not exclude its native prefix,
                # including when invalidation repeatedly discards its counters.
                pcs = {
                    int(pc, 16)
                    for entry_file in profile.glob("*.bc.entries")
                    for pc in entry_file.read_text().split()
                }
                assert 0x1000 in pcs, sorted(pcs)
            compile_aot(args.llvm_config, library, profile)
            replay = run(*command, "-A", library)
            for result in [baseline, collected, replay]:
                assert expected in result.stdout, result.stdout + result.stderr
            stats = {
                name: int(value)
                for name, value in re.findall(r"(\w+)=(\d+)", replay.stderr)
            }

            def check(condition, log=replay.stderr):
                assert condition, log

            entries = stats.get("entries", 0)
            check(entries > 0 and stats.get("native_insns", 0) > 0)
            if case == "system-aot-asid":
                # Each of the thousand remaps must bind the new code again.
                check(stats["bindings"] >= 1000)
            if case.startswith("system-aot-collection"):
                check(entries >= 128 and stats["fences"] == 128)
            if case == "system-aot-fence":
                check(stats["hash_rechecks"] > 0 and stats["hash_reuses"] > 0)
                check(stats["fences"] == 3)
            if case.startswith("system-aot-edges"):
                check(entries < 1000)
            if case.startswith("system-aot-binding"):
                # Native function entries include tail jumps. Count only
                # returns through the C dispatcher to detect a slot mismatch.
                check(0 < stats["dispatches"] < 1000)
            if case == "system-aot-status":
                # Disabled-SIE writes resume, and immediate clears avoid helpers.
                check(entries < 3000 and stats["slow_calls"] < 12000)
            if case == "system-aot-atomic":
                check(stats["slow_resumes"] > 1000)
            if case == "system-aot-remap":
                # A bare -A name is the file in the working directory, not a
                # library path search.
                local = run(*command, "-A", library.name, cwd=work)
                assert expected in local.stdout, local.stdout + local.stderr
            if case == "system-aot-atomic":
                # Three partial indexes force renaming and reuse of an already
                # renamed identical body; their entry union must stay callable.
                left, right, third = (work / f"merge-{i}" for i in range(3))
                for directory in (left, right, third):
                    shutil.copytree(
                        profile,
                        directory,
                        ignore=shutil.ignore_patterns(".objects"),
                    )
                for entry_file in profile.glob("*.bc.entries"):
                    pcs = entry_file.read_text().splitlines()
                    for index, directory in enumerate((left, right, third)):
                        target = directory / entry_file.name
                        subset = pcs[index::3]
                        if subset:
                            target.write_text("\n".join(subset) + "\n")
                        else:
                            target.unlink()
                            target.with_suffix("").unlink()
                merged = work / "merged.so"
                compile_aot(args.llvm_config, merged, left, right, third)
                assert expected in run(*command, "-A", merged).stdout
                # The quick 64-bit page key alone must never authorize code.
                wrong = work / "wrong-digest"
                shutil.copytree(
                    profile, wrong, ignore=shutil.ignore_patterns(".objects")
                )
                for digest_file in wrong.glob("*.bc.digest"):
                    digest_file.write_bytes(bytes(32))
                rejected = work / "wrong-digest.so"
                compile_aot(args.llvm_config, rejected, wrong)
                fallback = run(*command, "-A", rejected)
                assert expected in fallback.stdout
                assert re.search(r"AOT: entries=0(?:\s|$)", fallback.stderr)

            print(f"{case}: {entries} AOT entries, passed")
        elf, image = work / "reboot.elf", work / "reboot.bin"
        run(
            args.cc,
            "-march=rv32ia_zicsr",
            "-mabi=ilp32",
            "-nostdlib",
            "-static",
            "-fno-pie",
            "-no-pie",
            "-Wl,-e,_start,-Ttext=0,--build-id=none",
            "tests/system-aot-reboot.S",
            "-o",
            elf,
        )
        run(args.objcopy, "-O", "binary", elf, image)
        command = [emulator, "-s", "4096", "-B", "14", "-k", image]
        profile, library = work / "reboot-profile", work / "reboot.so"
        run_reboot(command)
        run_reboot([*command, "-C", profile], profile / "manifest")
        assert (profile / "manifest").exists()
        compile_aot(args.llvm_config, library, profile)
        replay = run_reboot([*command, "-A", library])
        entries = re.search(r"AOT: entries=(\d+)", replay)
        assert entries and int(entries[1]) > 0, replay
        print("system-aot-reboot: collection/replay lifecycle passed")


if __name__ == "__main__":
    main()
