#!/bin/bash
# Quick deploy script for rv32emu Web UI
# Usage:
#   ./tools/deploy-webui.sh user    # Deploy user mode only
#   ./tools/deploy-webui.sh system  # Deploy system mode only
#   ./tools/deploy-webui.sh all     # Deploy both modes
#   ./tools/deploy-webui.sh html    # Update HTML only (no rebuild)

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

log()
{
    echo -e "${GREEN}[*]${NC} $1"
}
error()
{
    echo -e "${RED}[ERROR]${NC} $1"
    exit 1
}

MODE="${1:-all}"
JOBS="$(getconf _NPROCESSORS_ONLN 2> /dev/null || echo 4)"

deploy_user_mode()
{
    make wasm_defconfig
    make clean
    make -j"$JOBS" prepare-web
}

deploy_system_mode()
{
    make clean
    make wasm_system_defconfig
    make -j"$JOBS" prepare-web
}

case "$MODE" in
    user)
        log "Deploying user mode..."
        deploy_user_mode
        log "User mode deployed to demo/user/"
        ;;

    system)
        log "Deploying system mode..."
        deploy_system_mode
        log "System mode deployed to demo/system/"
        ;;

    all)
        log "Deploying both user and system modes..."
        deploy_user_mode
        log "User mode deployed to demo/user/"
        deploy_system_mode
        log "System mode deployed to demo/system/"
        ;;

    html)
        log "Updating HTML files only (no rebuild)..."
        # Check every input before touching demo/, so a failure keeps the old pages.
        src=assets/wasm/html
        for f in user.html system.html demo-index.html; do
            [ -f "$src/$f" ] || error "Missing template: $src/$f"
        done
        [ -f .config ] || error "Missing .config; run 'make wasm_system_defconfig' first"
        # Same mapping as prepare-web in mk/wasm.mk; keep the two in sync.
        if grep -q '^CONFIG_ROOTFS_EXT4=y$' .config; then
            rootfs=rootfs.ext4
            rootfs_cli='-x vblk:rootfs.ext4,rootfs'
        else
            rootfs=rootfs.cpio.gz
            rootfs_cli='-i rootfs.cpio.gz'
        fi
        mkdir -p demo/{user,system}
        cp "$src/user.html" demo/user/index.html
        log "  ✓ User mode HTML updated"
        # Render to a temporary file so a sed failure cannot truncate the live page.
        sed -e "s|<ROOTFS>|$rootfs|g" -e "s|<ROOTFS_CLI>|$rootfs_cli|g" \
            "$src/system.html" > demo/system/index.html.tmp
        mv demo/system/index.html.tmp demo/system/index.html
        log "  ✓ System mode HTML updated"
        cp "$src/demo-index.html" demo/index.html
        log "  ✓ Demo index updated"
        ;;

    *)
        error "Unknown mode: $MODE\nUsage: $0 {user|system|all|html}"
        ;;
esac

log "Deployment complete!"
echo ""
echo "Start web server:"
echo "  python3 tools/dev-server.py --directory demo --port 8000"
echo ""
echo "URLs:"
echo "  http://localhost:8000/        - Main navigation"
echo "  http://localhost:8000/user/   - User mode"
echo "  http://localhost:8000/system/ - System mode"
