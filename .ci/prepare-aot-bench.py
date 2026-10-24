#!/usr/bin/env python3
"""Prepare explicit AOT artifacts and a launcher for tests/bench.py."""

import argparse
from pathlib import Path
import runpy
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--llvm-config", default="llvm-config")
    args = parser.parse_args()
    emulator = args.emulator.resolve()
    output = args.output.resolve()
    help_result = subprocess.run(
        [str(emulator), "-h"], capture_output=True, text=True
    )
    has_aot = "-C <directory>" in help_result.stderr + help_result.stdout
    libraries = {}
    if has_aot:
        root = Path(__file__).resolve().parents[1]
        registry = runpy.run_path(str(root / "tests/bench.py"))[
            "get_registered_benchmarks"
        ]()
        for name, benchmark in registry.items():
            benchmark.prepare()
            bench = benchmark(1, None, str(emulator))
            profile = output.parent / f"aot-{name}-profile"
            library = output.parent / f"aot-{name}.so"
            # Collection requires a new directory, and a cached build/ or an
            # earlier preparation may hold this script-owned one.
            shutil.rmtree(profile, ignore_errors=True)
            subprocess.run(
                [
                    str(emulator),
                    "-q",
                    "-C",
                    str(profile),
                    bench.BIN_PATH,
                    *bench.guest_args(),
                ],
                check=True,
            )
            subprocess.run(
                [
                    "python3",
                    str(root / "tools/aot.py"),
                    str(profile),
                    "-o",
                    str(library),
                    "--llvm-config",
                    args.llvm_config,
                    # Outside the profile, so reruns reuse unchanged objects.
                    "--cache-dir",
                    str(output.parent / f"aot-{name}-objects"),
                ],
                check=True,
            )
            libraries[Path(bench.BIN_PATH).name] = str(library)
    # Older comparison baselines use their original JIT. Keep that distinction
    # visible instead of silently calling their unsupported -C/-A options.
    print(
        f"{emulator}: {'offline AOT' if has_aot else 'legacy runtime compiler'}"
    )
    output.write_text(
        "#!/usr/bin/env python3\n"
        "import getopt, os, pathlib, sys\n"
        f"emulator = {str(emulator)!r}\n"
        f"libraries = {libraries!r}\n"
        "args = sys.argv[1:]\n"
        "try:\n"
        "    _, remaining = getopt.getopt(args, 'tgqmhpd:a:k:i:b:x:C:A:s:B:')\n"
        "except getopt.GetoptError as error:\n"
        "    raise SystemExit('benchmark launcher: ' + str(error))\n"
        "if not remaining:\n"
        "    raise SystemExit('benchmark launcher: missing guest ELF')\n"
        "guest = remaining[0]\n"
        "library = libraries.get(pathlib.Path(guest).name)\n"
        "if libraries and library is None:\n"
        "    raise SystemExit('benchmark launcher: missing AOT artifact for ' + guest)\n"
        "extra = ['-A', library] if library else []\n"
        "os.execv(emulator, [emulator, *extra, *args])\n"
    )
    output.chmod(0o755)


if __name__ == "__main__":
    main()
