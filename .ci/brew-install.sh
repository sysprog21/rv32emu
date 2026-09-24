#!/usr/bin/env bash

# Install the macOS build dependencies.
#
# The Cellar is restored from the actions cache before this runs, but the cache
# does not carry the /opt/homebrew/bin symlinks, and "brew install" on a formula
# it already sees in the Cellar reports "already installed" without relinking.
# So the link step below is not redundant on a cache hit: it is the only thing
# that puts these tools back on PATH.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

set -e -u -o pipefail

LLVM_VERSION=$(cat "${SCRIPT_DIR}/llvm-version")

brew install make dtc expect sdl2 bc e2fsprogs p7zip "llvm@${LLVM_VERSION}" dcfldd pkgconf portaudio

# SDL_mixer only gates the audio tests, so a bottle outage should not take the
# whole job down with it.
brew install sdl2_mixer \
    || echo "WARNING: sdl2_mixer unavailable, continuing without SDL_MIXER support"

# Relink every formula the later build steps invoke by name. Failures are
# tolerated (a keg-only formula refuses to link, and is reached through "brew
# --prefix" instead), so the verification below is what actually decides.
for formula in make dtc expect sdl2 bc p7zip dcfldd pkgconf portaudio; do
    brew link --overwrite "${formula}" > /dev/null 2>&1 || true
done

MISSING=
for tool in dtc expect bc pkg-config; do
    command -v "${tool}" > /dev/null || MISSING="${MISSING} ${tool}"
done
if [ -n "${MISSING}" ]; then
    echo "ERROR: not found in PATH after install:${MISSING}" >&2
    exit 1
fi
