#!/usr/bin/env python3
"""Build matched fixed-work guests without changing the source checkout.

Set ZEPHYR_BASE and ZEPHYR_MODULES (semicolon-separated). CROSS_COMPILE
defaults to /usr/bin/riscv64-unknown-elf-. Then
activate the Zephyr Python environment. Native TLS is enabled by default;
xPack GCC needs --tls n because it is configured without native TLS.
--coremark-dir accepts the pinned EEMBC checkout; otherwise it is fetched into the output directory. No guest
is called verified until the runner checks its console markers. Both engines
use the same compiler, ISA, TLS setting, optimization and work counts.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ZEPHYR_REV = "bbc6385f0a2c6476b5f191b3101d902303a34f50"
COREMARK_REV = "1f483d5b8316753a742cbf5590caf5bd0a4e4777"
CASES = {
    "boot": (1, 1),
    "computation": (100, 0xE714 + 0x1FD7 + 0x8E3A + 0x988C),
    "scheduling": (1000, 5000),
    "allocation": (1000, 384000),
    "serialization": (1000, 28000),
    "heap-regression": (1, 7),
}


def run(argv, **kwargs):
    return subprocess.run([str(x) for x in argv], check=True, **kwargs)


def revision(path):
    return subprocess.check_output(
        ["git", "-C", str(path), "rev-parse", "HEAD"], text=True
    ).strip()


def text_artifact(path):
    content = path.read_bytes()
    return {
        "path": str(path),
        "sha256": hashlib.sha256(content).hexdigest(),
        "text": content.decode(),
    }


def guest_configuration(build, cross, engine):
    artifact = text_artifact(build / "zephyr/.config")
    settings = {}
    for line in artifact["text"].splitlines():
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            settings[key] = value
    elf = build / "zephyr/zephyr.elf"
    env = dict(os.environ, LC_ALL="C")
    attributes = subprocess.check_output(
        [cross + "readelf", "-A", str(elf)], text=True, env=env
    )
    headers = subprocess.check_output(
        [cross + "readelf", "-lW", str(elf)], text=True, env=env
    )
    isa = re.search(r'Tag_RISCV_arch:\s*"([^"]+)"', attributes)
    entry = re.search(r"Entry point (0x[0-9a-fA-F]+)", headers)
    segments = []
    for line in headers.splitlines():
        fields = line.split()
        if fields and fields[0] == "LOAD":
            segments.append(
                {
                    "file_offset": int(fields[1], 16),
                    "virtual_address": int(fields[2], 16),
                    "physical_address": int(fields[3], 16),
                    "file_size_bytes": int(fields[4], 16),
                    "memory_size_bytes": int(fields[5], 16),
                    "flags": "".join(fields[6:-1]),
                    "alignment": int(fields[-1], 16),
                }
            )
    if not isa or not entry or not segments:
        raise ValueError(f"Cannot determine {engine} ELF ISA or load map")
    result = {
        "config": artifact,
        "isa": isa.group(1),
        "isa_config": {
            key: value
            for key, value in settings.items()
            if key.startswith("CONFIG_RISCV_ISA_")
        },
        "clock_hz": int(settings["CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC"], 0),
        "ram": {
            "base_address": int(settings["CONFIG_SRAM_BASE_ADDRESS"], 0),
            "size_bytes": int(settings["CONFIG_SRAM_SIZE"], 0) * 1024,
            "offset_bytes": int(settings["CONFIG_SRAM_OFFSET"], 0),
        },
        "entry_address": int(entry.group(1), 16),
        "load_segments": segments,
        "sbi": {
            "implementation": (
                "rv32emu-built-in-sbi"
                if engine == "rv32emu"
                else "external-opensbi"
            ),
            "firmware_identity": (
                "emulator-binary"
                if engine == "rv32emu"
                else "host-runner-records-path-and-hash"
            ),
        },
    }
    dts = build / "zephyr/zephyr.dts"
    if dts.exists():
        result["devicetree"] = text_artifact(dts)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output", type=Path, default=Path("build/zephyr/bench")
    )
    parser.add_argument("--case", action="append", choices=CASES)
    parser.add_argument("--jobs", type=int, default=2, choices=(1, 2))
    parser.add_argument("--coremark-dir", type=Path)
    parser.add_argument(
        "--tls",
        choices=("y", "n"),
        default="y",
        help="native TLS (default y; xPack requires n)",
    )
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cases = []
    manifest = {
        "schema_version": 1,
        "provenance": {
            "zephyr_revision": ZEPHYR_REV,
            "coremark_revision": COREMARK_REV,
            "coremark_source": "https://github.com/eembc/coremark",
            "source_license": "Apache-2.0",
            "coremark_score": False,
            "workload_sources": {
                "boot": "zephyr/samples/hello_world/src/main.c",
                "computation": "eembc/coremark:core_main.c (initialization and iterate)",
                "scheduling": "zephyr/tests/benchmarks/latency_measure/src/{thread_switch_yield,sema_test_signal_release,mutex_lock_unlock}.c",
                "allocation": "zephyr/tests/benchmarks/memory_allocators/src/k_heap.c",
                "serialization": "zephyr/tests/modules/nanopb/{src/main.c,proto/simple.proto,proto/simple.options}",
                "heap-regression": "zephyr/tests/lib/heap/src/main.c (unchanged)",
            },
        },
        "cases": cases,
    }
    manifest["provenance"]["adapter_sha256"] = {
        str(path.relative_to(here)): hashlib.sha256(
            path.read_bytes()
        ).hexdigest()
        for path in sorted(here.rglob("*"))
        if path.is_file() and "__pycache__" not in path.parts
    }
    manifest["provenance"]["board_patch_sha256"] = hashlib.sha256(
        (here.parent / "compare/zephyr.patch").read_bytes()
    ).hexdigest()
    selected = args.case or list(CASES)
    for name in CASES:
        iterations, checksum = CASES[name]
        cases.append(
            {
                "id": name,
                "mandatory": True,
                "config": {
                    "mode": "T1",
                    "scope": "boot" if name == "boot" else "fixed-work",
                    "reference": "qemu-tcg",
                    "vcpus": 1,
                    "icount": False,
                    "execution": "cold-start" if name == "boot" else "warm",
                    "in_guest_warmup_runs": 1,
                },
                "phase": "boot" if name == "boot" else "work",
                "iterations": iterations,
                "expected_checksum": checksum,
                "status": "unverified",
            }
        )

    def save_manifest():
        temporary = out / "manifest.json.tmp"
        temporary.write_text(json.dumps(manifest, indent=2) + "\n")
        temporary.replace(out / "manifest.json")

    save_manifest()
    failed = False
    interrupted = False
    try:
        base = Path(os.environ["ZEPHYR_BASE"]).resolve()
        modules = os.environ["ZEPHYR_MODULES"]
        cross = os.environ.get("CROSS_COMPILE", "/usr/bin/riscv64-unknown-elf-")
        manifest["provenance"]["tls"] = args.tls
        if revision(base) != ZEPHYR_REV:
            raise ValueError("ZEPHYR_BASE does not match the pinned revision")
        manifest["provenance"]["compiler"] = subprocess.check_output(
            [cross + "gcc", "--version"], text=True
        ).splitlines()[0]
        module_revisions = {
            str(Path(p).resolve()): revision(p) for p in modules.split(";")
        }
        expected_modules = {
            "picolibc": "ad4203cecf0fd65f653835fe65c20f1b7924e0ca",
            "nanopb": "0f6d088e777a33dd7b54297f137ef10d745c5da8",
        }
        for name, expected in expected_modules.items():
            matches = [
                rev
                for path, rev in module_revisions.items()
                if Path(path).name == name
            ]
            if matches != [expected]:
                raise ValueError(
                    f"ZEPHYR_MODULES needs the pinned {name} checkout"
                )
        for module in module_revisions:
            if subprocess.check_output(
                ["git", "-C", module, "status", "--porcelain"]
            ):
                raise ValueError(f"Module checkout must be clean: {module}")
        manifest["provenance"]["modules"] = module_revisions
        source = out / "zephyr-source"
        if not source.exists():
            run(["git", "clone", "--shared", "--no-checkout", base, source])
            run(["git", "-C", source, "checkout", "--detach", ZEPHYR_REV])
            run(
                [
                    "git",
                    "-C",
                    source,
                    "apply",
                    here.parent / "compare/zephyr.patch",
                ]
            )
        if revision(source) != ZEPHYR_REV:
            raise ValueError(
                "Output source checkout has an unexpected revision"
            )
        # Reused output trees must still contain exactly the board patch.
        diff = subprocess.check_output(
            ["git", "-C", str(source), "diff", "--binary"]
        )
        patch = (here.parent / "compare/zephyr.patch").read_bytes()
        if diff != patch:
            raise ValueError(
                "Output source checkout differs from the pinned board patch"
            )
        coremark = (args.coremark_dir or out / "coremark-source").resolve()
        if "computation" in selected:
            if not coremark.exists():
                run(
                    [
                        "git",
                        "clone",
                        "https://github.com/eembc/coremark.git",
                        coremark,
                    ]
                )
                run(
                    [
                        "git",
                        "-C",
                        coremark,
                        "checkout",
                        "--detach",
                        COREMARK_REV,
                    ]
                )
            if revision(coremark) != COREMARK_REV:
                raise ValueError(
                    "CoreMark checkout does not match the pinned revision"
                )
            if subprocess.check_output(
                ["git", "-C", str(coremark), "status", "--porcelain"]
            ):
                raise ValueError("CoreMark checkout must be clean")
        env = dict(
            os.environ,
            ZEPHYR_BASE=str(source),
            ZEPHYR_TOOLCHAIN_VARIANT="cross-compile",
            ZEPHYR_COMPARE_RUNNER=str(here.parent / "compare-run.py"),
        )
        for case in cases:
            if case["id"] not in selected:
                case["reason"] = "Not selected for this build"
                continue
            try:
                for engine in ("rv32emu", "qemu"):
                    build = out / case["id"] / engine
                    app = (
                        here / "heap"
                        if case["id"] == "heap-regression"
                        else here
                    )
                    command = [
                        "cmake",
                        "-GNinja",
                        "-S",
                        app,
                        "-B",
                        build,
                        "-DBOARD=rv32_compare",
                        f"-DBOARD_ROOT={here.parent / 'compare'}",
                        f"-DCOMPARE_ENGINE={engine}",
                        f"-DCROSS_COMPILE={cross}",
                        f"-DZEPHYR_MODULES={modules}",
                        "-DUSE_CCACHE=0",
                        f"-DBENCH_CASE={case['id']}",
                        f"-DCOREMARK_DIR={coremark}",
                        f"-DEXTRA_DTC_OVERLAY_FILE={here.parent / 'compare/boards/local/rv32_compare' / (engine + '.overlay')}",
                        "-DEXTRA_LDFLAGS=-Wl,--no-warn-rwx-segments",
                    ]
                    conf = (
                        str(here.parent / "no-tls.conf")
                        if args.tls == "n"
                        else ""
                    )
                    if case["id"] == "serialization":
                        conf += (";" if conf else "") + str(
                            here / "serialization.conf"
                        )
                    command.append(f"-DEXTRA_CONF_FILE={conf}")
                    with (out / f"{case['id']}-{engine}.log").open("w") as log:
                        run(
                            command,
                            env=env,
                            stdout=log,
                            stderr=subprocess.STDOUT,
                        )
                        run(
                            [
                                "cmake",
                                "--build",
                                build,
                                "--parallel",
                                args.jobs,
                            ],
                            env=env,
                            stdout=log,
                            stderr=subprocess.STDOUT,
                        )
                    config = (build / "zephyr/.config").read_text()
                    required = (
                        "CONFIG_RISCV_S_MODE=y",
                        "CONFIG_RISCV_S_MODE_EXTERNAL_SBI=y",
                        "CONFIG_SPEED_OPTIMIZATIONS=y",
                        "# CONFIG_SMP is not set",
                        "CONFIG_RISCV_ISA_RV32I=y",
                        "CONFIG_RISCV_ISA_EXT_M=y",
                        "CONFIG_RISCV_ISA_EXT_A=y",
                        "CONFIG_RISCV_ISA_EXT_C=y",
                        "CONFIG_RISCV_ISA_EXT_ZICSR=y",
                        "CONFIG_RISCV_ISA_EXT_ZIFENCEI=y",
                        (
                            "CONFIG_THREAD_LOCAL_STORAGE=y"
                            if args.tls == "y"
                            else "# CONFIG_THREAD_LOCAL_STORAGE is not set"
                        ),
                    )
                    if any(
                        setting not in config.splitlines()
                        for setting in required
                    ):
                        raise ValueError(
                            f"{engine} effective configuration does not match"
                        )
                    images = {
                        "elf": build / "zephyr/zephyr.elf",
                        "raw": build / "zephyr/zephyr.bin",
                    }
                    symbols = subprocess.check_output(
                        [cross + "nm", str(images["elf"])], text=True
                    )
                    if "__emutls" in symbols:
                        raise ValueError(
                            f"{engine} uses incompatible emulated TLS"
                        )
                    case[engine] = {
                        key: str(path) for key, path in images.items()
                    }
                    case[engine]["guest_configuration"] = guest_configuration(
                        build, cross, engine
                    )
                    case[engine]["config_sha256"] = hashlib.sha256(
                        config.encode()
                    ).hexdigest()
                    case[engine]["sha256"] = {
                        key: hashlib.sha256(path.read_bytes()).hexdigest()
                        for key, path in images.items()
                    }
                case["status"] = "ready"
                save_manifest()
            except (
                OSError,
                ValueError,
                subprocess.CalledProcessError,
            ) as error:
                case.update(status="failed", reason=str(error))
                failed = True
                save_manifest()
    except KeyboardInterrupt:
        interrupted = True
        for case in cases:
            if case["status"] == "unverified":
                case["reason"] = "Build interrupted; image not verified"
    except (
        KeyError,
        OSError,
        ValueError,
        subprocess.CalledProcessError,
    ) as error:
        failed = True
        for case in cases:
            case.update(status="failed", reason=str(error))
    save_manifest()
    for case in cases:
        print(f"{case['id']}: {case['status']} {case.get('reason', '')}")
    return 130 if interrupted else int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
