#!/usr/bin/env bash

# The -e is not set because we want to get all the mismatch format at once

set -u -o pipefail

# Use git ls-files to exclude submodules and untracked files
C_SOURCES=()
while IFS= read -r file; do
    [ -n "$file" ] && C_SOURCES+=("$file")
done < <(git ls-files -- '*.c' '*.cxx' '*.cpp' '*.h' '*.hpp')

if [ ${#C_SOURCES[@]} -gt 0 ]; then
    if command -v clang-format-20 > /dev/null 2>&1; then
        echo "Checking C/C++ files..."
        clang-format-20 -n --Werror "${C_SOURCES[@]}"
        C_FORMAT_EXIT=$?
    else
        echo "Skipping C/C++ format check: clang-format-20 not found" >&2
        C_FORMAT_EXIT=0
    fi
else
    C_FORMAT_EXIT=0
fi

SH_SOURCES=()
while IFS= read -r file; do
    [ -n "$file" ] && SH_SOURCES+=("$file")
done < <(git ls-files -- '*.sh')

if [ ${#SH_SOURCES[@]} -gt 0 ]; then
    echo "Checking shell scripts..."
    MISMATCHED_SH=$(shfmt -l "${SH_SOURCES[@]}")
    if [ -n "$MISMATCHED_SH" ]; then
        echo "The following shell scripts are not formatted correctly:"
        printf '%s\n' "$MISMATCHED_SH"
        shfmt -d "${SH_SOURCES[@]}"
        SH_FORMAT_EXIT=1
    else
        SH_FORMAT_EXIT=0
    fi
else
    SH_FORMAT_EXIT=0
fi

PY_SOURCES=()
while IFS= read -r file; do
    [ -n "$file" ] && PY_SOURCES+=("$file")
done < <(git ls-files -- '*.py')

if [ ${#PY_SOURCES[@]} -gt 0 ]; then
    echo "Checking Python files..."
    black --check --diff "${PY_SOURCES[@]}"
    PY_FORMAT_EXIT=$?
else
    PY_FORMAT_EXIT=0
fi

DTS_SOURCES=()
while IFS= read -r file; do
    [ -n "$file" ] && DTS_SOURCES+=("$file")
done < <(git ls-files -- '*.dts' '*.dtsi')

if [ ${#DTS_SOURCES[@]} -gt 0 ]; then
    echo "Checking DTS/DTSI files..."
    DTS_FORMAT_EXIT=0
    for dts_src in "${DTS_SOURCES[@]}"; do
        dtsfmt --check "${dts_src}"
        DTS_FORMAT_EXIT=$((DTS_FORMAT_EXIT + $?))
    done
else
    DTS_FORMAT_EXIT=0
fi

exit $((C_FORMAT_EXIT + SH_FORMAT_EXIT + PY_FORMAT_EXIT + DTS_FORMAT_EXIT))
