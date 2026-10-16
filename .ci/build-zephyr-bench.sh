#!/usr/bin/env bash
# Build pinned guests; Python keeps failed cases visible in manifest.json.
set -e -u -o pipefail
exec python3 "$(dirname "$0")/zephyr/bench/build.py" "$@"
