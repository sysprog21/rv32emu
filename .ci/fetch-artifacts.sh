#!/usr/bin/env bash

# Download the prebuilt artifacts a job needs, in one place so the various CI
# jobs stop each spelling out their own slightly different sequence.
#
# Usage:
#   .ci/fetch-artifacts.sh <elf|linux-image|doom>...
#
# Environment:
#   GH_TOKEN: raises the GitHub API rate limit used to resolve release tags.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"

set -e -u -o pipefail

if [ "$#" -eq 0 ]; then
    echo "Usage: $0 <elf|linux-image|doom>..." >&2
    exit 2
fi

# The build system keeps its configuration in the repository root, not under
# build/. Clear it before fetching so each request below is driven purely by the
# flags it passes, and not by whatever mode a previous job left enabled.
rm -f .config build/.config

DOOM_IWAD_URL="https://raw.githubusercontent.com/sysprog21/rv32emu-prebuilt/doom-artifact/shareware_doom_iwad.zip"

for artifact in "$@"; do
    case "${artifact}" in
        elf)
            fetch_artifact ELF
            ;;
        linux-image)
            fetch_artifact Linux-Image ENABLE_SYSTEM=1

            # ENABLE_SYSTEM=1 leaves a system configuration behind; drop it so
            # the next request, and the job's first build, start clean.
            rm -f .config build/.config
            ;;
        doom)
            "${SCRIPT_DIR}/fetch.sh" -o build/shareware_doom_iwad.zip "${DOOM_IWAD_URL}"
            unzip -o -d build/ build/shareware_doom_iwad.zip

            # This download sidesteps mk/external.mk, and with it the checksum
            # the build would otherwise apply, while the archive comes from a
            # mutable branch. Verify against the same constant so there is one
            # source of truth for what the payload should be.
            expected=$(sed -n 's/^DOOM_DATA_SHA := //p' mk/external.mk)
            if [ -z "${expected}" ]; then
                print_error "DOOM_DATA_SHA not found in mk/external.mk"
                exit 1
            fi
            if command -v sha1sum > /dev/null 2>&1; then
                actual=$(sha1sum build/DOOM1.WAD | cut -d' ' -f1)
            else
                actual=$(shasum build/DOOM1.WAD | cut -d' ' -f1)
            fi
            if [ "${actual}" != "${expected}" ]; then
                print_error "DOOM1.WAD checksum mismatch: got ${actual}, want ${expected}"
                exit 1
            fi
            ;;
        *)
            print_error "Unknown artifact: ${artifact}"
            exit 2
            ;;
    esac
done
