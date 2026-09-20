#!/usr/bin/env bash

# Build the WebAssembly demo and stage its files under /tmp for the deploy job.
#
# Usage:
#   .ci/wasm-build.sh <system|user>

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${SCRIPT_DIR}/common.sh"

set -e -u -o pipefail

MODE=${1:-}

case "${MODE}" in
    system)
        make wasm_system_defconfig

        STAGE=/tmp/rv32emu-system-demo
        make ENABLE_SYSTEM=1 ENABLE_GOLDFISH_RTC=1 ${PARALLEL}
        make assets/wasm/vendor/xterm.min.js assets/wasm/vendor/xterm.min.css

        # Concatenate the S99automount overlay onto the upstream rootfs so the
        # guest mounts /dev/vda at boot.
        make build/linux-image/rootfs.web.cpio ENABLE_SYSTEM=1

        # Ship the timidity instrument set twice: browsers with
        # DecompressionStream take the gzip payload, older Safari and Firefox
        # fall back to the plain tar.
        make build/timidity.tar build/timidity.tar.gz

        mkdir -p "${STAGE}"
        cp assets/wasm/html/system.html "${STAGE}/index.html"
        cp assets/wasm/js/coi-serviceworker.min.js "${STAGE}/"
        cp assets/wasm/js/common.js "${STAGE}/"
        cp assets/wasm/vendor/xterm.min.js "${STAGE}/"
        cp assets/wasm/vendor/xterm.min.css "${STAGE}/"
        cp build/rv32emu.js "${STAGE}/"
        cp build/rv32emu.wasm "${STAGE}/"
        # Only emitted for pthread-enabled builds.
        cp build/rv32emu.worker.js "${STAGE}/" || true
        cp build/linux-image/Image.gz "${STAGE}/"
        gzip -9 -c build/linux-image/rootfs.web.cpio > "${STAGE}/rootfs.cpio.gz"
        cp build/timidity.tar "${STAGE}/"
        cp build/timidity.tar.gz "${STAGE}/"
        ;;
    user)
        make wasm_defconfig

        STAGE=/tmp/rv32emu-demo
        make ${PARALLEL}
        make assets/wasm/vendor/xterm.min.js assets/wasm/vendor/xterm.min.css

        # Ship the timidity instrument set twice: browsers with
        # DecompressionStream take the gzip payload, older Safari and Firefox
        # fall back to the plain tar.
        make build/timidity.tar build/timidity.tar.gz

        mkdir -p "${STAGE}"

        # The landing page is committed at the demo repo root, the rest below
        # /user.
        cp assets/wasm/html/demo-index.html "${STAGE}/landing.html"
        cp assets/wasm/html/user.html "${STAGE}/index.html"
        cp assets/wasm/js/coi-serviceworker.min.js "${STAGE}/"
        cp assets/wasm/js/common.js "${STAGE}/"
        cp assets/wasm/vendor/xterm.min.js "${STAGE}/"
        cp assets/wasm/vendor/xterm.min.css "${STAGE}/"
        cp build/elf_list.js "${STAGE}/"
        cp build/rv32emu.js "${STAGE}/"
        cp build/rv32emu.wasm "${STAGE}/"
        cp build/rv32emu.worker.js "${STAGE}/" || true
        # Game data, fetched on demand by user.html.
        cp build/DOOM1.WAD "${STAGE}/"
        cp build/id1/pak0.pak "${STAGE}/"
        cp build/timidity.tar "${STAGE}/"
        cp build/timidity.tar.gz "${STAGE}/"
        ;;
    *)
        echo "Usage: $0 <system|user>" >&2
        exit 2
        ;;
esac

ls -al "${STAGE}"
