#!/usr/bin/env bash
# Fetch the ACT4 suite at the pin the Makefile uses (mk/riscv-arch-test.mk).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
act_url=$(sed -n 's/^ARCH_TEST_URL := //p' mk/riscv-arch-test.mk)
act_rev=$(sed -n 's/^ARCH_TEST_REV := //p' mk/riscv-arch-test.mk)
tools/fetch-dep.sh tests/riscv-arch-test "$act_url" "$act_rev"
