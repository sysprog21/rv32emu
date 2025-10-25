# Bash strict mode (enabled only when executed directly, not sourced)
if ! (return 0 2> /dev/null); then
    set -euo pipefail
fi

# Expect host is Linux/x86_64, Linux/aarch64, macOS/arm64

MACHINE_TYPE=$(uname -m)
OS_TYPE=$(uname -s)

check_platform()
{
    case "${MACHINE_TYPE}/${OS_TYPE}" in
        x86_64/Linux | aarch64/Linux | arm64/Darwin) ;;

        *)
            echo "Unsupported platform: ${MACHINE_TYPE}/${OS_TYPE}"
            exit 1
            ;;
    esac
}

if [ "${OS_TYPE}" = "Linux" ]; then
    PARALLEL=-j$(nproc)
else
    PARALLEL=-j$(sysctl -n hw.logicalcpu)
fi

# Color output helpers
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_success()
{
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_error()
{
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

print_warning()
{
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Assertion function for tests
# Usage: ASSERT <condition> <error_message>
ASSERT()
{
    local condition=$1
    shift
    local message="$*"

    if ! eval "${condition}"; then
        print_error "Assertion failed: ${message}"
        print_error "Condition: ${condition}"
        return 1
    fi
}

# Cleanup function registry
CLEANUP_FUNCS=()

register_cleanup()
{
    CLEANUP_FUNCS+=("$1")
}

cleanup()
{
    local func
    for func in "${CLEANUP_FUNCS[@]-}"; do
        [ -n "${func}" ] || continue
        eval "${func}" || true
    done
}

trap cleanup EXIT

# Universal download utility with curl/wget compatibility
# Provides consistent interface regardless of which tool is available

# Detect available download tool (lazy initialization)
detect_download_tool()
{
    if [ -n "${DOWNLOAD_TOOL:-}" ]; then
        return 0
    fi

    if command -v curl > /dev/null 2>&1; then
        DOWNLOAD_TOOL="curl"
    elif command -v wget > /dev/null 2>&1; then
        DOWNLOAD_TOOL="wget"
    else
        echo "Error: Neither curl nor wget is available" >&2
        return 1
    fi
}

# Download to stdout
# Usage: download_to_stdout <url>
download_to_stdout()
{
    detect_download_tool || return 1
    local url="$1"
    case "$DOWNLOAD_TOOL" in
        curl)
            curl -fS --retry 5 --retry-delay 2 --retry-max-time 60 -sL "$url"
            ;;
        wget)
            wget -qO- "$url"
            ;;
    esac
}

# Download to file
# Usage: download_to_file <url> <output_file>
download_to_file()
{
    detect_download_tool || return 1
    local url="$1"
    local output="$2"
    case "$DOWNLOAD_TOOL" in
        curl)
            curl -fS --retry 5 --retry-delay 2 --retry-max-time 60 -sL -o "$output" "$url"
            ;;
        wget)
            wget -q -O "$output" "$url"
            ;;
    esac
}

# Download with headers (for API calls)
# Usage: download_with_headers <url> <header1> <header2> ...
download_with_headers()
{
    detect_download_tool || return 1
    local url="$1"
    shift
    local headers=()

    case "$DOWNLOAD_TOOL" in
        curl)
            for header in "$@"; do
                headers+=(-H "$header")
            done
            curl -fS --retry 5 --retry-delay 2 --retry-max-time 60 -sL "${headers[@]}" "$url"
            ;;
        wget)
            for header in "$@"; do
                headers+=(--header="$header")
            done
            wget -qO- "${headers[@]}" "$url"
            ;;
    esac
}

# Download silently (no progress, suitable for CI)
# Usage: download_silent <url>
download_silent()
{
    detect_download_tool || return 1
    local url="$1"
    case "$DOWNLOAD_TOOL" in
        curl)
            curl -fS --retry 5 --retry-delay 2 --retry-max-time 60 -sL "$url"
            ;;
        wget)
            wget -qO- "$url"
            ;;
    esac
}

# Download with progress bar (for interactive use)
# Usage: download_with_progress <url> <output_file>
download_with_progress()
{
    detect_download_tool || return 1
    local url="$1"
    local output="$2"
    case "$DOWNLOAD_TOOL" in
        curl)
            curl -fS --retry 5 --retry-delay 2 --retry-max-time 60 -L -# -o "$output" "$url"
            ;;
        wget)
            wget -O "$output" "$url"
            ;;
    esac
}

# Check if URL is accessible
# Usage: check_url <url>
# Returns: 0 if accessible, 1 otherwise
check_url()
{
    detect_download_tool || return 1
    local url="$1"
    case "$DOWNLOAD_TOOL" in
        curl)
            curl -fS --retry 5 --retry-delay 2 --retry-max-time 60 -sL --head "$url" > /dev/null 2>&1
            ;;
        wget)
            wget --spider -q "$url" 2> /dev/null
            ;;
    esac
}
