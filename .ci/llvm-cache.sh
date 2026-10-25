#!/usr/bin/env bash

# Pack or unpack the installed LLVM tree for the CI cache.
#
# actions/cache extracts as the runner user, which cannot write under /usr, so
# the tree is cached as a tarball and unpacked with sudo. The tarball covers
# /usr/lib/llvm-<N> and the shared libraries that apt places beside it.
#
# Usage:
#   .ci/llvm-cache.sh save <major-version> <tarball>
#   .ci/llvm-cache.sh restore <major-version> <tarball>
#
# restore fails unless the unpacked tree reports a working llvm-config, so the
# caller can fall back to a fresh install.

set -e -u -o pipefail

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 save|restore <major-version> <tarball>" >&2
    exit 2
fi
ACTION=$1
VER=$2
TARBALL=$3

case "${ACTION}" in
    save)
        mkdir -p "$(dirname "${TARBALL}")"
        # Paths are relative to /; the glob for an absent architecture
        # drops out.
        cd /
        shopt -s nullglob
        # Rename only a complete archive: a partial one would be cached and,
        # being an exact key hit, could never be replaced.
        trap 'sudo rm -f "${TARBALL}.tmp"' EXIT
        sudo tar -cpf "${TARBALL}.tmp" "usr/lib/llvm-${VER}" \
            usr/lib/*-linux-gnu/libLLVM*.so* usr/lib/*-linux-gnu/libclang*.so*
        sudo mv "${TARBALL}.tmp" "${TARBALL}"
        ;;
    restore)
        sudo tar -xpf "${TARBALL}" -C /
        "/usr/lib/llvm-${VER}/bin/llvm-config" --version > /dev/null
        ;;
    *)
        echo "Error: unknown action '${ACTION}'" >&2
        exit 2
        ;;
esac
