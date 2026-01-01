#!/usr/bin/env python3
"""
Environment Detection Script for rv32emu Build System

Detects compilers, libraries, and toolchains for Kconfig integration.
Called by Kconfig with $(shell,...) to populate configuration options.

Usage:
    python3 detect-env.py [OPTIONS]

Options:
    --compiler           Print detected compiler type (GCC/Clang/Emscripten)
    --is-emcc           Check if compiler is Emscripten (prints y/n)
    --is-clang          Check if compiler is Clang (prints y/n)
    --is-gcc            Check if compiler is GCC (prints y/n)
    --have-sdl2         Check if SDL2 is available (prints y/n)
    --have-sdl2-mixer   Check if SDL2_mixer is available (prints y/n)
    --have-llvm18       Check if LLVM 18 is available (prints y/n)
    --have-riscv-toolchain  Check if RISC-V toolchain exists (prints y/n)
    --summary           Print full environment summary
"""

import os
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
    """Get compiler version string."""
    ret, stdout, _ = run_cmd(
        [compiler, "--version"]
        if not " " in compiler
        else shlex.split(compiler) + ["--version"]
    )
    return stdout if ret == 0 else ""


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
    ret, _, _ = run_cmd(["pkg-config", "--exists", package])
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


def have_llvm18():
    """Check if LLVM 18 is available."""
    # Check for llvm-config-18
    if shutil.which("llvm-config-18"):
        return True

    # Check Homebrew path on macOS (dynamic detection)
    if shutil.which("brew"):
        ret, stdout, _ = run_cmd(["brew", "--prefix", "llvm@18"])
        if ret == 0:
            homebrew_path = os.path.join(stdout.strip(), "bin", "llvm-config")
            if os.access(homebrew_path, os.X_OK):
                return True

    # Check standard llvm-config and verify version
    llvm_config = shutil.which("llvm-config")
    if llvm_config:
        ret, stdout, _ = run_cmd([llvm_config, "--version"])
        if ret == 0 and stdout.strip().startswith("18."):
            return True

    return False


def have_riscv_toolchain():
    """Check if a RISC-V cross-compiler toolchain is available."""
    toolchain_prefixes = [
        "riscv-none-elf-",
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


def print_summary():
    """Print full environment summary."""
    compiler = get_compiler_path()
    version = get_compiler_version(compiler)
    comp_type = detect_compiler_type(version)

    print(f"Compiler: {compiler}")
    print(f"Type: {comp_type}")
    print(f"SDL2: {'yes' if have_sdl2() else 'no'}")
    print(f"SDL2_mixer: {'yes' if have_sdl2_mixer() else 'no'}")
    print(f"LLVM 18: {'yes' if have_llvm18() else 'no'}")
    print(f"RISC-V Toolchain: {'yes' if have_riscv_toolchain() else 'no'}")


def main():
    if len(sys.argv) < 2:
        print_summary()
        return

    compiler = get_compiler_path()
    version = get_compiler_version(compiler)
    comp_type = detect_compiler_type(version)

    arg = sys.argv[1]

    if arg == "--compiler":
        print(comp_type)
    elif arg == "--is-emcc":
        print("y" if comp_type == "Emscripten" else "n")
    elif arg == "--is-clang":
        print("y" if comp_type == "Clang" else "n")
    elif arg == "--is-gcc":
        print("y" if comp_type == "GCC" else "n")
    elif arg == "--have-sdl2":
        print("y" if have_sdl2() else "n")
    elif arg == "--have-sdl2-mixer":
        print("y" if have_sdl2_mixer() else "n")
    elif arg == "--have-llvm18":
        print("y" if have_llvm18() else "n")
    elif arg == "--have-riscv-toolchain":
        print("y" if have_riscv_toolchain() else "n")
    elif arg == "--summary":
        print_summary()
    else:
        print(f"Unknown option: {arg}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
