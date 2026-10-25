#!/usr/bin/env bash

# Pack or unpack the installed LLVM tree for the CI cache.
#
# actions/cache extracts as the runner user, which cannot write under /usr, so
# the tree is cached as a tarball and unpacked with sudo. The tarball covers
# /usr/lib/llvm-<N>, the shared libraries that apt places beside it, and the
# headers under /usr/include, which /usr/lib/llvm-<N>/include links to.
#
# Usage:
#   .ci/llvm-cache.sh save <major-version> <tarball>
#   .ci/llvm-cache.sh restore <major-version> <tarball>
#
# restore fails unless the unpacked tree has a working llvm-config, the C API
# headers, and every shared library libLLVM needs, so the caller can fall back
# to a fresh install.

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
            "usr/include/llvm-${VER}" "usr/include/llvm-c-${VER}" \
            usr/lib/*-linux-gnu/libLLVM*.so* usr/lib/*-linux-gnu/libclang*.so*
        sudo mv "${TARBALL}.tmp" "${TARBALL}"
        ;;
    restore)
        sudo tar -xpf "${TARBALL}" -C /
        "/usr/lib/llvm-${VER}/bin/llvm-config" --version > /dev/null
        [ -f "/usr/lib/llvm-${VER}/include/llvm-c/Analysis.h" ]
        # Dependencies such as libedit and libxml2 come from apt packages that a
        # fresh runner may lack; a missing one means the tree is not usable.
        for lib in /usr/lib/*-linux-gnu/libLLVM*.so.*; do
            if ldd "${lib}" 2> /dev/null | grep -q 'not found'; then
                echo "Error: ${lib} has unresolved dependencies" >&2
                exit 1
            fi
        done
        ;;
    *)
        echo "Error: unknown action '${ACTION}'" >&2
        exit 2
        ;;
esac
