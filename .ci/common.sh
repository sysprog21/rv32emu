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

# Fetch latest release tag from rv32emu-prebuilt repository
# Usage: fetch_latest_release <artifact_type> [max_retries]
# Arguments:
#   artifact_type: One of "ELF", "Linux-Image", or "sail"
#   max_retries: Maximum retry attempts (default: 3, must be >= 1)
# Environment:
#   GH_TOKEN: GitHub token for authenticated API access (optional but recommended)
# Returns: Prints the release tag to stdout, exits with error if not found
fetch_latest_release()
{
    local artifact_type="$1"
    local max_retries="${2:-3}"
    local api_url="https://api.github.com/repos/sysprog21/rv32emu-prebuilt/releases"
    local release_tag
    local api_response
    local download_status
    local attempt

    # Validate artifact type
    case "$artifact_type" in
        ELF | Linux-Image | sail) ;;
        *)
            print_error "Invalid artifact type: $artifact_type (expected: ELF, Linux-Image, or sail)" >&2
            return 1
            ;;
    esac

    # Validate max_retries is a positive integer
    if ! [[ "$max_retries" =~ ^[1-9][0-9]*$ ]]; then
        print_warning "Invalid max_retries '$max_retries', using default (3)" >&2
        max_retries=3
    fi

    # Use C-style loop (Bash builtin) instead of 'seq' for portability
    for ((attempt = 1; attempt <= max_retries; attempt++)); do
        # Fetch releases with optional authentication
        # Capture stdout only; let stderr pass through to console for debugging
        if [ -n "${GH_TOKEN:-}" ]; then
            api_response=$(download_with_headers "$api_url" "Authorization: Bearer ${GH_TOKEN}") || download_status=$?
        else
            api_response=$(download_to_stdout "$api_url") || download_status=$?
        fi

        if [ -n "${download_status:-}" ] && [ "$download_status" -ne 0 ]; then
            print_warning "API request failed (exit code: $download_status)" >&2
            unset download_status
        else
            # Parse release tag from response (handle empty grep gracefully)
            release_tag=$(
                set +o pipefail
                echo "$api_response" \
                    | grep '"tag_name"' \
                    | grep "$artifact_type" \
                    | head -n 1 \
                    | sed -E 's/.*"tag_name": "([^"]+)".*/\1/'
            )

            if [ -n "$release_tag" ]; then
                echo "$release_tag"
                return 0
            fi
        fi

        if [ "$attempt" -lt "$max_retries" ]; then
            print_warning "Attempt $attempt/$max_retries failed for $artifact_type, retrying in 5s..." >&2
            sleep 5
        fi
    done

    print_error "Failed to fetch $artifact_type release tag after $max_retries attempts" >&2
    return 1
}

# Fetch and build artifact with automatic release tag resolution
# Usage: fetch_artifact <artifact_type> [make_options...]
# Arguments:
#   artifact_type: One of "ELF", "Linux-Image", or "sail"
#   make_options: Additional options to pass to make (e.g., ENABLE_SYSTEM=1)
# Environment:
#   GH_TOKEN: GitHub token for authenticated API access (optional but recommended)
#   FETCH_ARTIFACT_RETRIES: Max retries for make artifact (default: 2, must be >= 1)
fetch_artifact()
{
    local artifact_type="$1"
    shift
    local make_opts=("$@")
    local release_tag
    local max_retries="${FETCH_ARTIFACT_RETRIES:-2}"
    local attempt

    # Validate max_retries is a positive integer
    if ! [[ "$max_retries" =~ ^[1-9][0-9]*$ ]]; then
        print_warning "Invalid FETCH_ARTIFACT_RETRIES '$max_retries', using default (2)" >&2
        max_retries=2
    fi

    release_tag=$(fetch_latest_release "$artifact_type") || return 1

    # Use C-style loop (Bash builtin) instead of 'seq' for portability
    for ((attempt = 1; attempt <= max_retries; attempt++)); do
        echo "Fetching $artifact_type artifact (release: $release_tag, attempt $attempt/$max_retries)..." >&2
        if make LATEST_RELEASE="$release_tag" "${make_opts[@]}" artifact; then
            return 0
        fi

        if [ "$attempt" -lt "$max_retries" ]; then
            print_warning "make artifact failed for $artifact_type, retrying in 10s..." >&2
            sleep 10
        fi
    done

    print_error "Failed to fetch $artifact_type artifact after $max_retries attempts" >&2
    return 1
}
