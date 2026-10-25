#!/usr/bin/env bash

# Run apt-get with the options CI needs to survive flaky mirrors.
#
# apt.llvm.org and the Azure mirrors are often unreachable over IPv6 from the
# runners, so every call is IPv4 only, retries each download, and is retried
# as a whole with a growing delay. Runs through sudo when not already root.
#
# Usage:
#   .ci/apt-retry.sh <apt-get arguments>...
#
# APT_ATTEMPTS overrides the number of whole-command attempts (default 3).

set -u -o pipefail

if [ "$#" -eq 0 ]; then
    echo "Usage: $0 <apt-get arguments>..." >&2
    exit 2
fi

attempts="${APT_ATTEMPTS:-3}"
if ! [[ "${attempts}" =~ ^[1-9][0-9]*$ ]]; then
    echo "Error: APT_ATTEMPTS must be a positive integer, got '${attempts}'" >&2
    exit 2
fi
SUDO=()
[ "$(id -u)" -eq 0 ] || SUDO=(sudo)

for ((attempt = 1; attempt <= attempts; attempt++)); do
    if "${SUDO[@]}" apt-get -o Acquire::Retries=5 \
        -o Acquire::ForceIPv4=true "$@"; then
        exit 0
    fi

    if [ "${attempt}" -eq "${attempts}" ]; then
        echo "Error: apt-get $* failed after ${attempt} attempts" >&2
        exit 1
    fi

    echo "apt-get $* failed; retrying in $((attempt * 10)) seconds..." >&2
    sleep $((attempt * 10))
done
