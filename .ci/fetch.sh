#!/usr/bin/env bash

# Reliable download wrapper with retry logic and timeout handling
# Prefers curl over wget for better error handling and features

set -euo pipefail

# Source common utilities
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=.ci/common.sh
source "${SCRIPT_DIR}/common.sh"

# Configuration
RETRIES=3
TIMEOUT=30
READ_TIMEOUT=60
WAIT_RETRY=1

usage()
{
    local exit_code="${1:-0}"
    cat << EOF
Usage: $0 [OPTIONS] URL [OUTPUT_FILE]

Reliable download wrapper with automatic retry logic.

OPTIONS:
    -h, --help          Show this help message
    -r, --retries N     Number of retry attempts (default: $RETRIES)
    -t, --timeout N     Timeout in seconds (default: $TIMEOUT)
    -o, --output FILE   Output file path
    -q, --quiet         Quiet mode (minimal output)

EXAMPLES:
    $0 https://example.com/file.tar.gz
    $0 -o output.zip https://example.com/archive.zip
    $0 --retries 5 --timeout 30 https://example.com/large-file.bin

ENVIRONMENT:
    PREFER_WGET=1       Force using wget instead of curl
EOF
    exit "$exit_code"
}

log()
{
    echo -e "${GREEN}[reliable-download]${NC} $*" >&2
}

# Parse arguments
QUIET=0
OUTPUT=""
URL=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -h | --help)
            usage
            ;;
        -r | --retries)
            RETRIES="$2"
            shift 2
            ;;
        -t | --timeout)
            TIMEOUT="$2"
            shift 2
            ;;
        -o | --output)
            OUTPUT="$2"
            shift 2
            ;;
        -q | --quiet)
            QUIET=1
            shift
            ;;
        -*)
            print_error "Unknown option: $1"
            usage 1
            ;;
        *)
            if [[ -z "$URL" ]]; then
                URL="$1"
            elif [[ -z "$OUTPUT" ]]; then
                OUTPUT="$1"
            else
                print_error "Too many arguments"
                usage 1
            fi
            shift
            ;;
    esac
done

# Validate URL
if [[ -z "$URL" ]]; then
    print_error "URL is required"
    usage 1
fi

# Determine output file
if [[ -z "$OUTPUT" ]]; then
    OUTPUT=$(basename "$URL")
fi

# Check available tools
HAS_CURL=0
HAS_WGET=0

if command -v curl &> /dev/null; then
    HAS_CURL=1
fi

if command -v wget &> /dev/null; then
    HAS_WGET=1
fi

if [[ $HAS_CURL -eq 0 && $HAS_WGET -eq 0 ]]; then
    print_error "Neither curl nor wget is available"
    exit 1
fi

# Prefer curl unless PREFER_WGET is set
USE_CURL=0
if [[ $HAS_CURL -eq 1 && -z "${PREFER_WGET:-}" ]]; then
    USE_CURL=1
elif [[ $HAS_WGET -eq 0 ]]; then
    print_error "wget not available and PREFER_WGET is set"
    exit 1
fi

# Download function
download()
{
    local attempt=$1
    local url=$2
    local output=$3

    [[ $QUIET -eq 0 ]] && log "Attempt $attempt/$RETRIES: Downloading $url"

    if [[ $USE_CURL -eq 1 ]]; then
        # Verify curl is available
        if ! command -v curl > /dev/null 2>&1; then
            print_error "curl binary not found in PATH"
            return 1
        fi
        # curl options:
        # -f: Fail silently on HTTP errors
        # -L: Follow redirects
        # -S: Show error even with -s
        # -s: Silent mode (if quiet)
        # --retry: Number of retries
        # --retry-delay: Wait time between retries
        # --connect-timeout: Connection timeout
        # --max-time: Total operation timeout
        # -o: Output file
        local curl_opts=(
            -f
            -L
            --retry "$RETRIES"
            --retry-delay "$WAIT_RETRY"
            --retry-connrefused
            --connect-timeout "$TIMEOUT"
            --max-time "$READ_TIMEOUT"
            -o "$output"
        )

        if [[ $QUIET -eq 1 ]]; then
            curl_opts+=(-s -S)
        else
            curl_opts+=(--progress-bar)
        fi

        curl "${curl_opts[@]}" "$url"
    else
        # Verify wget is available
        if ! command -v wget > /dev/null 2>&1; then
            print_error "wget binary not found in PATH"
            return 1
        fi
        # wget options:
        # --retry-connrefused: Retry on connection refused
        # --waitretry: Wait between retries
        # --read-timeout: Read timeout
        # --timeout: Connection timeout
        # --tries: Number of attempts
        # -O: Output file
        local wget_opts=(
            --retry-connrefused
            --waitretry="$WAIT_RETRY"
            --read-timeout="$READ_TIMEOUT"
            --timeout="$TIMEOUT"
            --tries="$RETRIES"
            -O "$output"
        )

        if [[ $QUIET -eq 1 ]]; then
            wget_opts+=(-q)
        fi

        wget "${wget_opts[@]}" "$url"
    fi
}

# Main download logic with retry using atomic operations
SUCCESS=0
TEMP_OUTPUT="${OUTPUT}.downloading.$$"

# Cleanup trap for partial downloads
trap 'rm -f "$TEMP_OUTPUT"' EXIT INT TERM

for i in $(seq 1 "$RETRIES"); do
    if download "$i" "$URL" "$TEMP_OUTPUT"; then
        # Atomic move to final location
        if mv -f "$TEMP_OUTPUT" "$OUTPUT"; then
            SUCCESS=1
            [[ $QUIET -eq 0 ]] && log "Download successful: $OUTPUT"
            break
        else
            print_error "Failed to move temporary file to $OUTPUT"
        fi
    else
        EXIT_CODE=$?
        print_warning "Download failed (attempt $i/$RETRIES, exit code: $EXIT_CODE)"

        if [[ $i -lt $RETRIES ]]; then
            [[ $QUIET -eq 0 ]] && log "Retrying in ${WAIT_RETRY}s..."
            sleep "$WAIT_RETRY"
        fi
    fi
done

if [[ $SUCCESS -eq 0 ]]; then
    print_error "Download failed after $RETRIES attempts"
    rm -f "$TEMP_OUTPUT" # Clean up partial download
    exit 1
fi

# Verify file was created and is not empty
if [[ ! -f "$OUTPUT" ]]; then
    print_error "Output file not created: $OUTPUT"
    exit 1
fi

if [[ ! -s "$OUTPUT" ]]; then
    print_error "Output file is empty: $OUTPUT"
    rm -f "$OUTPUT"
    exit 1
fi

exit 0
