#!/usr/bin/env bash

# Install apt packages on a CI runner, tolerating out-of-sync mirrors.
#
# GitHub's Ubuntu images regularly hit mirrors whose Release files lag behind
# the package indexes, which makes "apt-get update" exit non-zero even though
# every package the job needs is available. Failing the whole job on that is
# noise, so the update is advisory and only the install decides the exit code.
#
# Usage:
#   .ci/apt-install.sh <package>...

set -u -o pipefail

# The update-only options below are passed to this one invocation rather than
# dropped into /etc/apt/apt.conf.d. A persistent file also governs every later
# apt run, including the update inside install-llvm.sh, which runs under set -e
# and would then abort on any repository error. apt-retry.sh adds IPv4-only
# transport and retries to every call made here.
APT_OPTS=(
    -o APT::Update::Error-Mode=any
    -o Acquire::IndexTargets::deb::DEP-11::DefaultEnabled=false
    -o Acquire::IndexTargets::deb::DEP-11-icons::DefaultEnabled=false
    -o Acquire::IndexTargets::deb::DEP-11-icons-hidpi::DefaultEnabled=false
)

APT_RETRY="$(dirname "${BASH_SOURCE[0]}")/apt-retry.sh"

# A lagging mirror fails the same way on every attempt, so do not wait it out.
APT_ATTEMPTS=1 "${APT_RETRY}" update -q=2 "${APT_OPTS[@]}" \
    || echo "WARNING: apt-get update failed, continuing with cached indexes"

if [ "$#" -eq 0 ]; then
    exit 0
fi

"${APT_RETRY}" install -y -q=2 "$@"
