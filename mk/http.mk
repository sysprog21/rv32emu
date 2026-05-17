# HTTP download utilities
#
# Provides unified HTTP download macros for curl/wget with retry logic.
# Include this file before any mk file that needs HTTP downloads.

ifndef _MK_HTTP_INCLUDED
_MK_HTTP_INCLUDED := 1

# HTTP download tool detection (prefer curl, fallback to wget)
CURL := $(shell command -v curl 2>/dev/null)
WGET := $(shell command -v wget 2>/dev/null)

# Detect curl --retry-all-errors support (curl 7.71+)
CURL_HAS_RETRY_ALL_ERRORS := $(if $(CURL),$(shell $(CURL) --help all 2>&1 | grep -q -- '--retry-all-errors' && echo 1))

# Detect wget --show-progress support (GNU wget extension)
WGET_HAS_PROGRESS := $(if $(WGET),$(shell $(WGET) --help 2>&1 | grep -q show-progress && echo 1))

# Detect wget --retry-on-http-error support (wget 1.20+, Dec 2018).
# RHEL/CentOS 8 ships wget 1.19; without this guard a curl-less host
# there would die with "unrecognized option".
WGET_HAS_RETRY_ON_HTTP_ERROR := $(if $(WGET),$(shell $(WGET) --help 2>&1 | grep -q -- '--retry-on-http-error' && echo 1))

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
    # --retry-all-errors (curl 7.71+) extends --retry to transient HTTP 5xx
    # (502/503/504); without it a single GitHub Releases CDN hiccup fails
    # the build instead of being absorbed by the retry loop.
    # $(1): URL, $(2): output file
    HTTP_DOWNLOAD = curl -fSL --retry 5 --retry-delay 2 $(if $(CURL_HAS_RETRY_ALL_ERRORS),--retry-all-errors) --progress-bar $(1) -o $(2)
    # Download file silently with retry
    # $(1): URL, $(2): output file
    HTTP_DOWNLOAD_QUIET = curl -fsSL --retry 5 --retry-delay 2 $(if $(CURL_HAS_RETRY_ALL_ERRORS),--retry-all-errors) $(1) -o $(2)
else ifdef WGET
    HTTP_TOOL := wget
    HTTP_GET = wget -q $(if $(GH_TOKEN),--header="Authorization: Bearer $(GH_TOKEN)") -O- $(1) 2>/dev/null
    # --retry-on-http-error covers transient 5xx that --tries alone ignores;
    # gated on wget 1.20+ so older wget (e.g. RHEL 8) still works.
    HTTP_DOWNLOAD = wget -q $(if $(WGET_HAS_PROGRESS),--show-progress) --tries=5 --waitretry=2 $(if $(WGET_HAS_RETRY_ON_HTTP_ERROR),--retry-on-http-error=500,502,503,504) $(1) -O $(2)
    HTTP_DOWNLOAD_QUIET = wget -q --tries=5 --waitretry=2 $(if $(WGET_HAS_RETRY_ON_HTTP_ERROR),--retry-on-http-error=500,502,503,504) $(1) -O $(2)
else
    HTTP_TOOL :=
endif

endif # _MK_HTTP_INCLUDED
