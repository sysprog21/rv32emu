#!/usr/bin/env bash

# The -e is not set because we want to get all the mismatch format at once

set -u -o pipefail

set -x

REPO_ROOT="$(git rev-parse --show-toplevel)"

C_SOURCES=$(find "${REPO_ROOT}" | egrep "\.(c|cxx|cpp|h|hpp)$")
for file in ${C_SOURCES}; do
    clang-format-18 ${file} > expected-format
    diff -u -p --label="${file}" --label="expected coding style" ${file} expected-format
done
C_MISMATCH_LINE_CNT=$(clang-format-18 --output-replacements-xml ${C_SOURCES} | egrep -c "</replacement>")

SH_SOURCES=$(find "${REPO_ROOT}" | egrep "\.sh$")
for file in ${SH_SOURCES}; do
    shfmt -d "${file}"
done
SH_MISMATCH_FILE_CNT=$(shfmt -l ${SH_SOURCES})

exit $((C_MISMATCH_LINE_CNT + SH_MISMATCH_FILE_CNT))
