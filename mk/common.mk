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
NEGATIVE_WORDS = 0 false no n
define has
$(if $(filter $(firstword $(ENABLE_$(strip $1))),$(NEGATIVE_WORDS)),0,$(if $(filter $(firstword $(ENABLE_$(strip $1))),$(POSITIVE_WORDS)),1,$(call config-to-feature,$1)))
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
                  clean distclean env-check artifact fetch-checksum build-linux-image

# Targets where we can skip expensive dependency detection (pkg-config, llvm-config, etc.)
# This speeds up 'make clean', etc. significantly
SKIP_DEPS_TARGETS := clean distclean
SKIP_DEPS_CHECK := $(filter $(SKIP_DEPS_TARGETS),$(MAKECMDGOALS))

# Targets that generate .config
CONFIG_GENERATORS := config menuconfig defconfig oldconfig

# Detect *_defconfig pattern targets (e.g., jit_defconfig, mini_defconfig)
DEFCONFIG_GOALS := $(filter %_defconfig,$(MAKECMDGOALS))

# Check if we need configuration
# Note: Empty MAKECMDGOALS means default goal (all), which is a build goal
BUILD_GOALS := $(filter-out $(CONFIG_TARGETS) $(DEFCONFIG_GOALS),$(MAKECMDGOALS))
IS_DEFAULT_BUILD := $(if $(MAKECMDGOALS),,yes)
NEEDS_CONFIG := $(if $(or $(BUILD_GOALS),$(IS_DEFAULT_BUILD)),yes,)
HAS_CONFIG_GEN := $(filter $(CONFIG_GENERATORS),$(MAKECMDGOALS))$(DEFCONFIG_GOALS)

# Placeholder for require-config (actual dependency added in main Makefile)
# This macro is kept for compatibility but the real work is done via
# the .config prerequisite on $(BIN) in the main Makefile
define require-config
endef

# Build Directory
OUT ?= build
# Note: $(OUT) target is defined in main Makefile to avoid duplication

# Standard Phony Targets

.PHONY: config menuconfig defconfig oldconfig savedefconfig
.PHONY: clean distclean env-check

# Reusable Templates

# Directory creation template
# Usage: $(eval $(call make-dir,$(OUT)/subdir))
define make-dir
$(1):
	$$(Q)mkdir -p $$@
endef

# Conditional submodule/clone helper for tarball compatibility
# Handles both git repository (uses submodule) and release tarball (uses git clone)
# $(1): target directory
# $(2): repository URL
# $(3): branch/tag (optional, defaults to default branch)
define ensure-submodule
	@if [ -d .git ]; then \
	    git submodule update --init --depth=1 "$(1)" || { \
	        echo "Error: Failed to update submodule $(1)" >&2; \
	        exit 1; \
	    }; \
	else \
	    if [ -d "$(1)" ] && [ ! -d "$(1)/.git" ]; then \
	        echo "Warning: Directory $(1) exists without .git, removing..." >&2; \
	        rm -rf "$(1)"; \
	    fi; \
	    if [ ! -d "$(1)/.git" ]; then \
	        CLONE_OPTS="--depth=1"; \
	        if [ -n "$(3)" ]; then \
	            CLONE_OPTS="$$CLONE_OPTS --branch=$(3)"; \
	        fi; \
	        git clone $$CLONE_OPTS "$(2)" "$(1)" || { \
	            echo "Error: Failed to clone $(2) to $(1)" >&2; \
	            exit 1; \
	        }; \
	    fi; \
	fi
endef

# Generic object compilation rule
# $(1): output directory variable name (e.g., OUT)
# $(2): source directory (e.g., src)
# $(3): extra CFLAGS (optional)
# $(4): extra prerequisites (optional)
# Usage: $(eval $(call compile-rule,OUT,src,$(EXTRA_CFLAGS)))
define compile-rule
$$($(1))/%.o: $(2)/%.c $(4) $$(CONFIG_HEADER) | $$($(1))
	$$(Q)mkdir -p $$(dir $$@)
	$$(VECHO) "  CC\t$$@\n"
	$$(Q)$$(CC) -o $$@ $$(CFLAGS) $(3) -c -MMD -MF $$@.d $$<
endef

# Test Framework Templates

# Generic test framework
# $(1): test name (e.g., cache, map, path)
# $(2): list of test object basenames (e.g., test-cache.o)
# $(3): additional object dependencies from src/ (full paths)
# $(4): extra subdirectories to create (optional)
#
# Creates:
#   - $(1)_TEST_SRCDIR, $(1)_TEST_OUTDIR, $(1)_TEST_TARGET, $(1)_TEST_OBJS
#   - Compilation rules for test objects
#   - Link rule for test binary
#
# Usage: $(eval $(call test-framework,cache,test-cache.o,$(OUT)/cache.o $(OUT)/mpool.o))
define test-framework
$(1)_TEST_SRCDIR := tests/$(1)
$(1)_TEST_OUTDIR := $$(OUT)/$(1)
$(1)_TEST_TARGET := $$($(1)_TEST_OUTDIR)/test-$(1)
$(1)_TEST_OBJS := $$(addprefix $$($(1)_TEST_OUTDIR)/, $(2)) $(3)

# Create output directory
$$($(1)_TEST_OUTDIR) $(4):
	$$(Q)mkdir -p $$@

# Compile test objects
$$($(1)_TEST_OUTDIR)/%.o: $$($(1)_TEST_SRCDIR)/%.c $$(CONFIG_HEADER) | $$($(1)_TEST_OUTDIR) $(4)
	$$(VECHO) "  CC\t$$@\n"
	$$(Q)$$(CC) -o $$@ $$(CFLAGS) -I./src -c -MMD -MF $$@.d $$<

# Link test binary
$$($(1)_TEST_TARGET): $$($(1)_TEST_OBJS)
	$$(VECHO) "  LD\t$$@\n"
	$$(Q)$$(CC) $$^ -o $$@ $$(LDFLAGS)

# Track dependencies
deps += $$($(1)_TEST_OBJS:%.o=%.o.d)
endef

# Simple test runner (just runs binary and checks exit code)
# $(1): test name
# Usage: $(eval $(call run-test-simple,map))
define run-test-simple
run-test-$(1): $$($(1)_TEST_TARGET)
	$$(VECHO) "Running test-$(1) ... "
	$$(Q)$$< && $$(call notice, [OK]) || { $$(PRINTF) "Failed.\n"; exit 1; }
endef

# Single test action rule (generates one .out file)
# $(1): test name
# $(2): action name
define run-test-action
$$($(1)_TEST_OUTDIR)/$(2).out: $$($(1)_TEST_TARGET) $$($(1)_TEST_SRCDIR)/$(2).in $$($(1)_TEST_SRCDIR)/$(2).expect
	$$(Q)$$($(1)_TEST_TARGET) $$($(1)_TEST_SRCDIR)/$(2).in > $$@
endef

# Comparison test runner (compares output against expected files)
# $(1): test name
# $(2): list of test action names
# Usage: $(eval $(call run-test-compare,cache,cache-new cache-put cache-get))
define run-test-compare
$(1)_TEST_ACTIONS := $(2)
$(1)_TEST_OUT := $$(addprefix $$($(1)_TEST_OUTDIR)/, $$($(1)_TEST_ACTIONS:%=%.out))

# Generate individual rules for each action (enables parallelism)
$$(foreach e,$(2),$$(eval $$(call run-test-action,$(1),$$(e))))

run-test-$(1): $$($(1)_TEST_OUT)
	$$(Q)$$(foreach e,$$($(1)_TEST_ACTIONS),\
	    $$(PRINTF) "Running $$(e) ... "; \
	    if cmp $$($(1)_TEST_SRCDIR)/$$(e).expect $$($(1)_TEST_OUTDIR)/$$(e).out; then \
	        $$(call notice, [OK]); \
	    else \
	        $$(PRINTF) "Failed.\n"; \
	        exit 1; \
	    fi; \
	)
endef

# Feature Extension Templates

# Set feature flags for extensions
# $(1): list of extension names
# Usage: $(call set-features,EXT_M EXT_A EXT_F EXT_C)
define set-features
$(foreach ext,$(1),$(call set-feature,$(ext)))
endef

endif # _MK_COMMON_INCLUDED
