#!/usr/bin/env bash

# The -e is not set because we want to get all the mismatch format at once

set -u -o pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"

# Use git ls-files to exclude submodules and untracked files
mapfile -t C_SOURCES < <(git ls-files | grep -E '\.(c|cxx|cpp|h|hpp)$')

if [ ${#C_SOURCES[@]} -gt 0 ]; then
    echo "Checking C/C++ files..."
    clang-format-18 -n --Werror "${C_SOURCES[@]}"
    C_FORMAT_EXIT=$?
else
    C_FORMAT_EXIT=0
fi

mapfile -t SH_SOURCES < <(git ls-files | grep -E '\.sh$')

if [ ${#SH_SOURCES[@]} -gt 0 ]; then
    echo "Checking shell scripts..."
    shfmt -d "${SH_SOURCES[@]}"
    SH_FORMAT_EXIT=$?
else
    SH_FORMAT_EXIT=0
fi

mapfile -t PY_SOURCES < <(git ls-files | grep -E '\.py$')

if [ ${#PY_SOURCES[@]} -gt 0 ]; then
    echo "Checking Python files..."
    black --check --diff "${PY_SOURCES[@]}"
    PY_FORMAT_EXIT=$?
else
    PY_FORMAT_EXIT=0
fi

exit $((C_FORMAT_EXIT + SH_FORMAT_EXIT + PY_FORMAT_EXIT))
