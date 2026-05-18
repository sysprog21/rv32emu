#!/usr/bin/env bash
#
# Install LLVM <major> from apt.llvm.org with GPG fingerprint pinning.
#
# Replaces the pattern of fetching upstream's llvm.sh and sudo-executing it
# without verification, which is a supply-chain risk: a hostile mirror or
# DNS hijack could ship anything to the runner and run it as root.
#
# What this script does (mirrors the relevant parts of llvm.sh):
#   1. Download the apt.llvm.org signing key.
#   2. Verify its fingerprint matches the pinned LLVM_APT_FINGERPRINT below.
#      Any mismatch aborts before anything is added to apt or installed.
#   3. Install the verified key to /etc/apt/trusted.gpg.d.
#   4. Add the apt source line for the requested major version.
#   5. apt-get update.
#
# Usage:
#   sudo .ci/install-llvm.sh <major-version>
#
# Example:
#   sudo .ci/install-llvm.sh 20
#
# Afterwards the caller is expected to `apt-get install` the specific
# llvm-<N>* / clang-<N> / lld-<N> packages it needs.

set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <major-version>" >&2
    exit 2
fi
VER=$1

if ! [[ "${VER}" =~ ^[0-9]+$ ]]; then
    echo "Error: major version must be an integer, got '${VER}'" >&2
    exit 2
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "Error: this script must run as root (use sudo)" >&2
    exit 2
fi

# Pinned fingerprint for the apt.llvm.org signing key.
# Published by the LLVM project; rotating this requires a deliberate edit.
# Verify against https://apt.llvm.org/llvm-snapshot.gpg.key (no spaces).
LLVM_APT_FINGERPRINT='6084F3CF814B57C1CF12EFD515CF4D18AF4F7421'

KEY_URL='https://apt.llvm.org/llvm-snapshot.gpg.key'
WORK_DIR=$(mktemp -d)
trap 'rm -rf "${WORK_DIR}"' EXIT

echo "Fetching apt.llvm.org signing key..."
if command -v curl > /dev/null 2>&1; then
    curl -fsSL --retry 3 -o "${WORK_DIR}/llvm.key" "${KEY_URL}"
elif command -v wget > /dev/null 2>&1; then
    wget -q -O "${WORK_DIR}/llvm.key" "${KEY_URL}"
else
    echo "Error: neither curl nor wget is available" >&2
    exit 1
fi

# Convert the ASCII-armored key to a dearmored keyring so apt can use it,
# and extract the fingerprint for verification.
gpg --dearmor < "${WORK_DIR}/llvm.key" > "${WORK_DIR}/llvm.gpg"

ACTUAL_FINGERPRINT=$(gpg --with-colons --import-options show-only --import \
    "${WORK_DIR}/llvm.gpg" 2> /dev/null \
    | awk -F: '/^fpr:/ {print $10; exit}')

if [ -z "${ACTUAL_FINGERPRINT}" ]; then
    echo "Error: could not extract fingerprint from downloaded key" >&2
    exit 1
fi

if [ "${ACTUAL_FINGERPRINT}" != "${LLVM_APT_FINGERPRINT}" ]; then
    echo "Error: apt.llvm.org key fingerprint mismatch" >&2
    echo "  expected: ${LLVM_APT_FINGERPRINT}" >&2
    echo "  got:      ${ACTUAL_FINGERPRINT}" >&2
    echo "Refusing to install; abort before touching apt sources." >&2
    exit 1
fi
echo "Fingerprint OK: ${ACTUAL_FINGERPRINT}"

KEYRING=/etc/apt/trusted.gpg.d/apt.llvm.org.gpg
install -m 0644 "${WORK_DIR}/llvm.gpg" "${KEYRING}"

# Derive the apt.llvm.org distribution from the running release codename.
CODENAME=$(lsb_release -cs 2> /dev/null || true)
if [ -z "${CODENAME}" ]; then
    echo "Error: lsb_release -cs failed; install lsb-release first" >&2
    exit 1
fi

SOURCE_LIST=/etc/apt/sources.list.d/llvm-${VER}.list
echo "deb http://apt.llvm.org/${CODENAME}/ llvm-toolchain-${CODENAME}-${VER} main" \
    > "${SOURCE_LIST}"
chmod 0644 "${SOURCE_LIST}"

echo "Updating apt cache for llvm-${VER}..."
apt-get update -q=2

echo "apt.llvm.org repository for LLVM ${VER} is configured."
echo "Run 'apt-get install llvm-${VER} clang-${VER} lld-${VER}' (or similar) next."
