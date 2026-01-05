# HTTP download utilities
#
# Provides unified HTTP download macros for curl/wget with retry logic.
# Include this file before any mk file that needs HTTP downloads.

ifndef _MK_HTTP_INCLUDED
_MK_HTTP_INCLUDED := 1

# HTTP download tool detection (prefer curl, fallback to wget)
CURL := $(shell command -v curl 2>/dev/null)
WGET := $(shell command -v wget 2>/dev/null)

# Detect wget --show-progress support (GNU wget extension)
WGET_HAS_PROGRESS := $(if $(WGET),$(shell $(WGET) --help 2>&1 | grep -q show-progress && echo 1))

# Select HTTP tool and define unified commands
# Note: GH_TOKEN environment variable enables authenticated GitHub API requests
# (avoids 60 requests/hour rate limit for unauthenticated requests)
ifdef CURL
    HTTP_TOOL := curl
    # Fetch URL to stdout (for parsing API responses)
    # Uses GH_TOKEN if available for authenticated requests
    # $(1): URL
    HTTP_GET = curl -fsSL $(if $(GH_TOKEN),-H "Authorization: Bearer $(GH_TOKEN)") $(1) 2>/dev/null
    # Download file with progress bar and retry
    # $(1): URL, $(2): output file
    HTTP_DOWNLOAD = curl -fSL --retry 3 --retry-delay 2 --progress-bar $(1) -o $(2)
    # Download file silently with retry
    # $(1): URL, $(2): output file
    HTTP_DOWNLOAD_QUIET = curl -fsSL --retry 3 --retry-delay 2 $(1) -o $(2)
else ifdef WGET
    HTTP_TOOL := wget
    HTTP_GET = wget -q $(if $(GH_TOKEN),--header="Authorization: Bearer $(GH_TOKEN)") -O- $(1) 2>/dev/null
    HTTP_DOWNLOAD = wget -q $(if $(WGET_HAS_PROGRESS),--show-progress) --tries=3 --waitretry=2 $(1) -O $(2)
    HTTP_DOWNLOAD_QUIET = wget -q --tries=3 --waitretry=2 $(1) -O $(2)
else
    HTTP_TOOL :=
endif

endif # _MK_HTTP_INCLUDED
