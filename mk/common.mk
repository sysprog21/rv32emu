# Common build rules and utilities
#
# Provides:
# - Verbosity control
# - Feature flag macros (for backward compatibility)
# - Colored output helpers
# - Configuration validation

ifndef _MK_COMMON_INCLUDED
_MK_COMMON_INCLUDED := 1

# Verbosity Control
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

# Feature Flag Macros (Backward Compatibility)

# These macros allow old-style ENABLE_* flags to work with CONFIG_* from Kconfig

# Convert CONFIG_* to internal feature flags
define config-to-feature
$(if $(filter y,$(CONFIG_$(strip $1))),1,0)
endef

# Get specified feature (supports both ENABLE_* and CONFIG_*)
POSITIVE_WORDS = 1 true yes y
define has
$(if $(filter $(firstword $(ENABLE_$(strip $1))), $(POSITIVE_WORDS)),1,$(call config-to-feature,$1))
endef

# Set compiler feature flag from config
define set-feature
$(eval CFLAGS += -DRV32_FEATURE_$(strip $1)=$(call has,$1))
endef

# Colored Output

GREEN = \033[32m
YELLOW = \033[33m
RED = \033[31m
BLUE = \033[34m
NC = \033[0m

notice = $(PRINTF) "$(GREEN)$(strip $1)$(NC)\n"
noticex = $(shell echo "$(GREEN)$(strip $1)$(NC)\n")
warn = $(PRINTF) "$(YELLOW)$(strip $1)$(NC)\n"
warnx = $(shell echo "$(YELLOW)$(strip $1)$(NC)\n")
error_msg = $(PRINTF) "$(RED)$(strip $1)$(NC)\n"

# Configuration Validation

# Targets that don't require .config
# Note: 'artifact' is included because it only downloads prebuilt binaries
# and determines what to download from ENABLE_* flags (via compat.mk)
CONFIG_TARGETS := config menuconfig defconfig oldconfig savedefconfig \
                  clean distclean help env-check artifact fetch-checksum build-linux-image

# Targets that generate .config
CONFIG_GENERATORS := config menuconfig defconfig oldconfig

# Detect *_defconfig pattern targets (e.g., jit_defconfig, mini_defconfig)
DEFCONFIG_GOALS := $(filter %_defconfig,$(MAKECMDGOALS))

# Check if we need configuration
BUILD_GOALS := $(filter-out $(CONFIG_TARGETS) $(DEFCONFIG_GOALS),$(MAKECMDGOALS))
HAS_CONFIG_GEN := $(filter $(CONFIG_GENERATORS),$(MAKECMDGOALS))$(DEFCONFIG_GOALS)

# Require .config for build targets (unless a config generator is present)
define require-config
ifneq ($(BUILD_GOALS),)
ifeq ($(HAS_CONFIG_GEN),)
ifneq "$(CONFIG_CONFIGURED)" "y"
    $$(info )
    $$(info *** Configuration file ".config" not found!)
    $$(info *** Please run 'make config' or 'make defconfig' first.)
    $$(info )
    $$(error Configuration required)
endif
endif
endif
endef

# Build Directory
OUT ?= build
# Note: $(OUT) target is defined in main Makefile to avoid duplication

# Standard Phony Targets

.PHONY: config menuconfig defconfig oldconfig savedefconfig
.PHONY: clean distclean help env-check

endif # _MK_COMMON_INCLUDED
