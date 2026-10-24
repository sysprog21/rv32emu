#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"
git submodule update --init tests/riscv-arch-test

# Follow the dependency versions pinned by the ACT4 submodule.
sail_version=$(sed -n 's/^REQUIRED_SAIL_VERSION = "\([^"]*\)".*/\1/p' \
    tests/riscv-arch-test/framework/src/act/config.py)
if [ -z "$sail_version" ]; then
    echo "Cannot read the ACT4 Sail version" >&2
    exit 1
fi
bundler_version=$(
    python3 - << 'PYTHON'
import tomllib
from pathlib import Path
print(tomllib.loads(Path("tests/riscv-arch-test/.mise.toml").read_text())["tools"]["gem:bundler"])
PYTHON
)
# Sail publishes Linux binaries named after uname -m.
case "$OS_TYPE/$MACHINE_TYPE" in
    Linux/x86_64 | Linux/aarch64) ;;
    *)
        echo "Unsupported Sail host: $OS_TYPE/$MACHINE_TYPE" >&2
        exit 1
        ;;
esac
sail_dir="$PWD/build/act-tools/sail-riscv-Linux-$MACHINE_TYPE/bin"
if [ ! -x "$sail_dir/sail_riscv_sim" ] \
    || [ "$("$sail_dir/sail_riscv_sim" --version)" != "$sail_version" ]; then
    # SHA-256 digests of the Sail release assets. Moving the submodule to
    # another Sail version needs that release's digests added here and in
    # docker/Dockerfile-sail.
    case "$sail_version/$MACHINE_TYPE" in
        0.16/x86_64) sail_sha256=f9bb8a32b8f3eddbdf69b3a29d96dff4ba1e77f45877165e0478418367da6bee ;;
        0.16/aarch64) sail_sha256=92163c1b02b416ae6a031283e0b2085674daed5dfa2d79b1c929ef23da51da73 ;;
        *)
            echo "No pinned checksum for Sail $sail_version on $MACHINE_TYPE" >&2
            exit 1
            ;;
    esac
    mkdir -p build/act-tools
    download_to_file \
        "https://github.com/riscv/sail-riscv/releases/download/$sail_version/sail-riscv-Linux-$MACHINE_TYPE.tar.gz" \
        build/act-tools/sail.tar.gz
    echo "$sail_sha256  build/act-tools/sail.tar.gz" | sha256sum -c --quiet -
    tar -xzf build/act-tools/sail.tar.gz -C build/act-tools
fi
if [ -n "${GITHUB_PATH:-}" ]; then
    printf '%s\n' "$sail_dir" >> "$GITHUB_PATH"
fi

# Install UDB into the active Ruby environment so its CLI is on PATH.
gem list --installed bundler -v "$bundler_version" > /dev/null \
    || gem install bundler -v "$bundler_version" --no-document
gemfile="$PWD/tests/riscv-arch-test/framework/src/act/data/Gemfile"
# Name the version so a newer Bundler installed alongside cannot resolve it.
bundle=(bundle "_${bundler_version}_")
BUNDLE_GEMFILE=$gemfile "${bundle[@]}" check || BUNDLE_GEMFILE=$gemfile "${bundle[@]}" install
