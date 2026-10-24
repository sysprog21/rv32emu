#!/usr/bin/env python3
"""Check benchmark argument forwarding and fail-closed artifact selection."""

import json
import os
import shlex
import argparse
import runpy
import ctypes
import hashlib
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llvm-config")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="aot-tools-") as temporary:
        work = Path(temporary)
        emulator = work / "emulator"
        emulator.write_text(
            "#!/usr/bin/env python3\n"
            "import json, sys\n"
            "print(json.dumps(sys.argv[1:]))\n"
        )
        emulator.chmod(0o700)
        hash_library = work / "hash.so"
        subprocess.run(
            [
                *shlex.split(
                    os.environ.get("HOSTCC") or os.environ.get("CC") or "cc"
                ),
                "-std=c11",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-shared",
                "-fPIC",
                "src/aot_hash.c",
                "-o",
                str(hash_library),
            ],
            check=True,
        )
        digest_function = ctypes.CDLL(str(hash_library)).aot_page_digest
        digest_function.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        digest_function.restype = None
        for page in (
            bytes(4096),
            bytes([255]) * 4096,
            bytes(range(256)) * 16,
            hashlib.shake_256(b"aot-page").digest(4096),
        ):
            digest = ctypes.create_string_buffer(32)
            digest_function(page, digest)
            assert digest.raw == hashlib.sha256(page).digest()
        # Cached objects are linked unrebuilt; others must not write them.
        aot = runpy.run_path("tools/aot.py")
        private_cache_dir = aot["private_cache_dir"]
        cache = work / "object-cache"
        assert private_cache_dir(cache) == cache.resolve()
        cached_object = cache / "cached.o"
        cached_object.write_bytes(b"cached object")
        for path, validate, modes in (
            (cache, private_cache_dir, (0o700, 0o750, 0o755)),
            (cached_object, aot["check_private"], (0o600, 0o640, 0o644)),
        ):
            for mode in modes:
                path.chmod(mode)
                validate(path)
            # Group write alone must fail, even without world write. Group
            # ownership does not establish exclusive membership by this user.
            for mode in (0o620, 0o660, 0o602, 0o666, 0o777):
                path.chmod(mode)
                try:
                    validate(path)
                except ValueError as error:
                    assert "not writable by group or others" in str(error)
                else:
                    raise AssertionError(
                        f"writable object cache path was accepted: {path} "
                        f"(mode {mode:o})"
                    )
            path.chmod(modes[0])
        if args.llvm_config:
            bindir = Path(
                subprocess.check_output(
                    [args.llvm_config, "--bindir"], text=True
                ).strip()
            )
            assembly = work / "valid.ll"
            assembly.write_text("define void @old(ptr %rv) { ret void }\n")
            original, renamed = work / "original.bc", work / "renamed-valid.bc"
            subprocess.run(
                [str(bindir / "llvm-as"), str(assembly), "-o", str(original)],
                check=True,
            )
            rename = aot["rename_region"]
            rename(original, renamed, "old", "new", args.llvm_config)
            disassembled = subprocess.check_output(
                [str(bindir / "llvm-dis"), str(renamed), "-o", "-"], text=True
            )
            assert "@new(" in disassembled and "@old(" not in disassembled
            invalid = work / "invalid.bc"
            invalid.write_bytes(b"invalid bitcode")
            try:
                rename(
                    invalid, work / "renamed.bc", "old", "new", args.llvm_config
                )
            except ValueError as error:
                assert "cannot parse region" in str(error)
            else:
                raise AssertionError("invalid bitcode was accepted")
        launcher = work / "launcher"
        subprocess.run(
            [
                "python3",
                ".ci/prepare-aot-bench.py",
                "--emulator",
                str(emulator),
                "--output",
                str(launcher),
            ],
            check=True,
            capture_output=True,
        )
        launcher.write_text(
            launcher.read_text().replace(
                "libraries = {}", "libraries = {'guest': '/trusted/guest.so'}"
            )
        )
        for arguments in (
            ["-d", "registers.json", "guest", "argument"],
            ["-qdregisters.json", "guest", "argument"],
            ["--", "guest", "argument"],
        ):
            result = subprocess.run(
                [str(launcher), *arguments],
                check=True,
                text=True,
                capture_output=True,
            )
            assert json.loads(result.stdout) == [
                "-A",
                "/trusted/guest.so",
                *arguments,
            ]
        result = subprocess.run(
            [str(launcher), "unprepared-guest"], capture_output=True, text=True
        )
        assert (
            result.returncode != 0 and "missing AOT artifact" in result.stderr
        )
    print("AOT benchmark launcher checks passed")


if __name__ == "__main__":
    main()
