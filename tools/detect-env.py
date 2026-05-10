#!/usr/bin/env python3
"""
Environment Detection Script for rv32emu Build System

Detects compilers, libraries, and toolchains for Kconfig integration.
Boolean checks use exit codes (0 = true, 1 = false) for portable
$(python,...) preprocessor integration.

Usage:
    python3 detect-env.py [OPTIONS]

Options:
    --compiler           Print detected compiler type (GCC/Clang/Emscripten)
    --is-emcc           Check if compiler is Emscripten (exit 0/1)
    --is-clang          Check if compiler is Clang (exit 0/1)
    --is-gcc            Check if compiler is GCC (exit 0/1)
    --have-emcc         Check if Emscripten (emcc) is available (exit 0/1)
    --have-sdl2         Check if SDL2 is available (exit 0/1)
    --have-sdl2-mixer   Check if SDL2_mixer is available (exit 0/1)
    --have-llvm         Check if supported LLVM 19-23 is available (exit 0/1)
    --have-riscv-toolchain  Check if RISC-V toolchain exists (exit 0/1)
    --have-zlib         Check if zlib exists (exit 0/1)
    --jit-capable-target  Check if the compiler targets a JIT host (exit 0/1)
    --summary           Print full environment summary
"""

import os
import platform
import shlex
import shutil
import subprocess
import sys


def run_cmd(cmd, timeout=5):
    """Run a command and return (returncode, stdout, stderr)."""
    try:
        if isinstance(cmd, str):
            cmd = shlex.split(cmd)
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=timeout
        )
        return result.returncode, result.stdout, result.stderr
    except (FileNotFoundError, subprocess.TimeoutExpired, ValueError, OSError):
        return 1, "", ""


def get_compiler_path():
    """Determine compiler path from environment."""
    cross_compile = os.environ.get("CROSS_COMPILE", "")
    cc = os.environ.get("CC", "")

    if cc:
        return cc
    elif cross_compile:
        return f"{cross_compile}gcc"
    else:
        return "cc"


def get_compiler_version(compiler):
    """Get compiler version string (stdout + stderr for emcc compat)."""
    ret, stdout, stderr = run_cmd(
        [compiler, "--version"]
        if " " not in compiler
        else shlex.split(compiler) + ["--version"]
    )
    return (stdout + stderr) if ret == 0 else ""


def detect_compiler_type(version_output):
    """Detect compiler type from version string."""
    lower = version_output.lower()

    if "emcc" in lower:
        return "Emscripten"
    if "clang" in lower:
        return "Clang"
    if "gcc" in lower or "free software foundation" in lower:
        return "GCC"
    return "Unknown"


def check_pkg_config(package):
    """Check if a package exists via pkg-config."""
    # PKG_CONFIG may carry a wrapper or options, as make allows.
    pkg_config = shlex.split(os.environ.get("PKG_CONFIG", "pkg-config"))
    ret, _, _ = run_cmd(pkg_config + ["--exists", package])
    return ret == 0


def check_sdl2_config():
    """Check if sdl2-config exists."""
    return shutil.which("sdl2-config") is not None


def have_sdl2():
    """Check if SDL2 is available."""
    return check_sdl2_config() or check_pkg_config("sdl2")


def have_sdl2_mixer():
    """Check if SDL2_mixer is available."""
    return check_pkg_config("SDL2_mixer")


def have_emcc():
    """Check if Emscripten (emcc) is available."""
    emcc = shutil.which("emcc")
    if emcc:
        # Verify it works by checking version
        # Some Emscripten builds emit version info to stderr, so check both
        ret, stdout, stderr = run_cmd([emcc, "--version"])
        combined = (stdout + stderr).lower()
        if ret == 0 and "emcc" in combined:
            return True
    return False


LLVM_MIN_VERSION = 19
LLVM_MAX_VERSION = 23


def llvm_config_supported(llvm_config):
    """Check whether llvm-config reports a supported major version."""
    ret, stdout, _ = run_cmd([llvm_config, "--version"])
    if ret != 0:
        return False
    try:
        major = int(stdout.strip().split(".", 1)[0])
    except (IndexError, ValueError):
        return False
    return LLVM_MIN_VERSION <= major <= LLVM_MAX_VERSION


def llvm_config_candidates():
    """Yield llvm-config paths in the order the Makefile searches them."""
    # An explicit make LLVM_CONFIG=... wins there, so honor it here too.
    if os.environ.get("LLVM_CONFIG"):
        yield os.environ["LLVM_CONFIG"]

    versions = range(LLVM_MAX_VERSION, LLVM_MIN_VERSION - 1, -1)
    for version in versions:
        yield shutil.which(f"llvm-config-{version}")

    # Homebrew installs keg-only formulae outside PATH. Its unversioned llvm
    # is the newest release, so try it right after the newest llvm@N.
    if shutil.which("brew"):
        formulae = [f"llvm@{version}" for version in versions]
        formulae.insert(1, "llvm")
        for formula in formulae:
            ret, stdout, _ = run_cmd(["brew", "--prefix", formula])
            if ret == 0:
                yield os.path.join(stdout.strip(), "bin", "llvm-config")

    yield shutil.which("llvm-config")


def have_llvm():
    """Check if a supported LLVM version is available."""
    return any(
        path and os.access(path, os.X_OK) and llvm_config_supported(path)
        for path in llvm_config_candidates()
    )


def have_riscv_toolchain():
    """Check if a RISC-V cross-compiler toolchain is available."""
    toolchain_prefixes = [
        "riscv-none-elf-",
        "riscv32-elf-",
        "riscv64-elf-",
        "riscv32-unknown-elf-",
        "riscv64-unknown-elf-",
        "riscv-none-embed-",
    ]

    for prefix in toolchain_prefixes:
        gcc = shutil.which(f"{prefix}gcc")
        if gcc:
            # Verify it's actually a RISC-V compiler by checking predefined macros
            try:
                result = subprocess.run(
                    [gcc, "-dM", "-E", "-x", "c", "-"],
                    input="",
                    capture_output=True,
                    text=True,
                    timeout=5,
                )
                if result.returncode == 0 and "__riscv" in result.stdout:
                    return True
            except (subprocess.TimeoutExpired, OSError):
                pass

            # Fallback: just check if --version mentions RISC-V
            ret, stdout, _ = run_cmd([gcc, "--version"])
            if ret == 0 and (
                "riscv" in stdout.lower() or "risc-v" in stdout.lower()
            ):
                return True

    return False


def have_zlib():
    """Check if ZLIB is available."""
    return check_pkg_config("zlib")


def print_summary():
    """Print full environment summary."""
    compiler = get_compiler_path()
    version = get_compiler_version(compiler)
    comp_type = detect_compiler_type(version)

    print(f"Compiler: {compiler}")
    print(f"Type: {comp_type}")
    print(f"Emscripten: {'yes' if have_emcc() else 'no'}")
    print(f"SDL2: {'yes' if have_sdl2() else 'no'}")
    print(f"SDL2_mixer: {'yes' if have_sdl2_mixer() else 'no'}")
    print(f"LLVM 19-23: {'yes' if have_llvm() else 'no'}")
    print(f"RISC-V Toolchain: {'yes' if have_riscv_toolchain() else 'no'}")
    print(f"ZLIB: {'yes' if have_zlib() else 'no'}")


def jit_capable_target():
    """Whether the compiler targets a CPU the JIT generates code for.

    Ask the compiler rather than the kernel: a 32-bit userland on a 64-bit
    kernel reports the kernel's machine, but builds 32-bit code. The
    predefined macros also reflect flags such as -m32 in CC. Without a
    working compiler, fall back to the host machine. This is the host
    compiler: CROSS_COMPILE names the RISC-V toolchain for guest programs.
    """
    cmd = shlex.split(os.environ.get("CC") or "cc")
    ret, stdout, _ = run_cmd(cmd + ["-dM", "-E", "-x", "c", os.devnull])
    if ret == 0 and stdout:
        macros = {line.split()[1] for line in stdout.splitlines() if line}
        return bool(macros & {"__x86_64__", "_M_X64", "__aarch64__"})
    return platform.machine() in ("x86_64", "AMD64", "aarch64", "arm64")


def bool_exit(result):
    """Signal boolean result via exit code for $(python,...) integration.

    Kconfig's $(python,assert run(sys.executable, 'tools/detect-env.py', ...))
    checks the exit code: 0 maps to 'y', non-zero maps to 'n'.
    """
    sys.exit(0 if result else 1)


def _compiler_type():
    """Detect compiler type (lazy, avoids probing when not needed)."""
    compiler = get_compiler_path()
    version = get_compiler_version(compiler)
    return detect_compiler_type(version)


def main():
    if len(sys.argv) < 2:
        print_summary()
        return

    arg = sys.argv[1]

    # Compiler-type queries (probe CC lazily)
    if arg == "--compiler":
        # String output for $(shell,...) -- not convertible to $(python,...)
        print(_compiler_type())
    elif arg == "--is-emcc":
        bool_exit(_compiler_type() == "Emscripten")
    elif arg == "--is-clang":
        bool_exit(_compiler_type() == "Clang")
    elif arg == "--is-gcc":
        bool_exit(_compiler_type() == "GCC")
    # Library/toolchain availability (no compiler probing needed)
    elif arg == "--have-emcc":
        bool_exit(have_emcc())
    elif arg == "--have-sdl2":
        bool_exit(have_sdl2())
    elif arg == "--have-sdl2-mixer":
        bool_exit(have_sdl2_mixer())
    elif arg == "--have-llvm":
        bool_exit(have_llvm())
    elif arg == "--have-riscv-toolchain":
        bool_exit(have_riscv_toolchain())
    elif arg == "--have-zlib":
        bool_exit(have_zlib())
    elif arg == "--jit-capable-target":
        bool_exit(jit_capable_target())
    elif arg == "--summary":
        print_summary()
    else:
        print(f"Unknown option: {arg}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
