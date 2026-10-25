#!/usr/bin/env bash

# Install the formatters that .ci/check-format.sh shells out to.
#
# clang-format is pulled from apt.llvm.org through install-llvm.sh so the
# signing key is fingerprint-checked rather than trusted blind, and so the
# version tracks .ci/clang-format-version instead of drifting on its own.
# That pin is separate from .ci/llvm-version so moving T2C to a newer LLVM
# does not reformat the tree.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

set -e -u -o pipefail

CLANG_FORMAT_VERSION=$(cat "${SCRIPT_DIR}/clang-format-version")
DTSFMT_VERSION=v0.8.0

"${SCRIPT_DIR}/apt-install.sh" curl wget lsb-release gnupg
sudo "${SCRIPT_DIR}/install-llvm.sh" "${CLANG_FORMAT_VERSION}"
"${SCRIPT_DIR}/apt-retry.sh" install -y -q=2 \
    "clang-format-${CLANG_FORMAT_VERSION}" shfmt python3-pip

pip3 install --break-system-packages black==25.1.0

# Fetch the release binary directly rather than piping the vendor's installer
# into a shell: that installer is unversioned, so the formatter it lands could
# change under a rerun and start reporting different diffs than the run that
# passed. Linux x86_64 is the only host this job runs on.
DTSFMT_URL="https://github.com/mskelton/dtsfmt/releases/download/${DTSFMT_VERSION}/dtsfmt-x86_64-unknown-linux-musl.tar.gz"
curl -fsSL --retry 3 "${DTSFMT_URL}" | sudo tar -xz -C /usr/local/bin dtsfmt
dtsfmt --version

# HTML/JS formatter
npm install -g prettier@3.9.8
