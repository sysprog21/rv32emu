UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    PRINTF = printf
else
    PRINTF = env printf
endif

UNAME_M := $(shell uname -m)
ifeq ($(UNAME_M),x86_64)
    HOST_PLATFORM := x86
else ifeq ($(UNAME_M),aarch64)
    HOST_PLATFORM := aarch64
else ifeq ($(UNAME_M),arm64) # macOS
    HOST_PLATFORM := arm64
else
    $(error Unsupported platform.)
endif

# Control the build verbosity
# 'make V=1' equals to 'make VERBOSE=1'
ifeq ("$(origin V)", "command line")
    VERBOSE = $(V)
endif
ifeq ("$(VERBOSE)","1")
    Q :=
    VECHO = @true
    REDIR =
else
    Q := @
    VECHO = @$(PRINTF)
    REDIR = >/dev/null
endif

# Get specified feature
POSITIVE_WORDS = 1 true yes
define has
$(if $(filter $(firstword $(ENABLE_$(strip $1))), $(POSITIVE_WORDS)),1,0)
endef

# Set specified feature
define set-feature
$(eval CFLAGS += -D RV32_FEATURE_$(strip $1)=$(call has, $1))
endef

# Test suite
GREEN = \033[32m
YELLOW = \033[33m
NC = \033[0m

notice = $(PRINTF) "$(GREEN)$(strip $1)$(NC)\n"
noticex = $(shell echo "$(GREEN)$(strip $1)$(NC)\n")
warn = $(PRINTF) "$(YELLOW)$(strip $1)$(NC)\n"
# Used inside $(warning) or $(error)
warnx = $(shell echo "$(YELLOW)$(strip $1)$(NC)\n")

# File utilities
SHA1SUM = sha1sum
SHA1SUM := $(shell which $(SHA1SUM))
ifndef SHA1SUM
    SHA1SUM = shasum
    SHA1SUM := $(shell which $(SHA1SUM))
    ifndef SHA1SUM
        $(warning No shasum found. Disable checksums)
        SHA1SUM := echo
    endif
endif

SHA256SUM = sha256sum
SHA256SUM := $(shell which $(SHA256SUM))
ifndef SHA256SUM
    SHA256SUM = shasum -a 256
    SHA256SUM := $(shell which shasum)
    ifndef SHA256SUM
        $(warning No sha256sum found. Disable checksums)
        SHA256SUM := echo
    endif
endif
