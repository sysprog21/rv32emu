# Backward compatibility layer for ENABLE_* flags
# Converts legacy ENABLE_* variables to CONFIG_* for Kconfig integration
#
# This allows existing CI/CD scripts and build commands to continue working
# with the new Kconfig-based build system.
#
# Usage: ENABLE_JIT=1 make  ->  Automatically sets CONFIG_JIT=y
#
# Conflict Resolution:
#   When the same flag appears multiple times (e.g., 'make ENABLE_JIT=0 ENABLE_JIT=1'),
#   GNU Make uses the LAST value specified. This layer respects that behavior.
#
# Accepted Values (whitespace is trimmed):
#   Enable:  1, y, yes, true, Y, YES, TRUE, Yes, True
#   Disable: 0, n, no, false, N, NO, FALSE, No, False
#
# Deprecation: This compatibility layer will be removed in a future release.
# Please migrate to using 'make defconfig' or 'make <name>_defconfig'

# Normalize a value to either 'y', 'n', or empty (unknown)
# Usage: $(call normalize-bool,VALUE)
# Returns: y (enabled), n (disabled), or empty (invalid/unknown)
# Note: Strips whitespace and handles common case variations
normalize-bool = $(strip \
    $(if $(filter 1 y yes true Y YES TRUE Yes True,$(strip $(1))),y,\
    $(if $(filter 0 n no false N NO FALSE No False,$(strip $(1))),n,)))

# Helper to convert ENABLE_* to CONFIG_* with value normalization
# Usage: $(call enable-to-config,FEATURE)
# Note: Uses 'override' to ensure command-line ENABLE_* takes precedence over .config
define enable-to-config
ifdef ENABLE_$(1)
    ENABLE_$(1)_NORMALIZED := $$(call normalize-bool,$$(ENABLE_$(1)))
    ifeq ($$(ENABLE_$(1)_NORMALIZED),y)
        override CONFIG_$(1) := y
    else ifeq ($$(ENABLE_$(1)_NORMALIZED),n)
        override CONFIG_$(1) := n
    else
        $$(warning Invalid value for ENABLE_$(1)='$$(ENABLE_$(1))'. Use 0/1, y/n, yes/no, or true/false.)
    endif
endif
endef

# Core extensions
$(eval $(call enable-to-config,EXT_M))
$(eval $(call enable-to-config,EXT_A))
$(eval $(call enable-to-config,EXT_F))
$(eval $(call enable-to-config,EXT_C))

# RV32E mode (Embedded, 16 registers)
$(eval $(call enable-to-config,RV32E))

# Bit manipulation extensions
$(eval $(call enable-to-config,Zba))
$(eval $(call enable-to-config,Zbb))
$(eval $(call enable-to-config,Zbc))
$(eval $(call enable-to-config,Zbs))

# CSR and fence extensions
$(eval $(call enable-to-config,Zicsr))
$(eval $(call enable-to-config,Zifencei))

# Execution modes
# Note: JIT is handled separately below (requires CONFIG_INTERPRETER_ONLY coordination)
$(eval $(call enable-to-config,SYSTEM))
$(eval $(call enable-to-config,GOLDFISH_RTC))
$(eval $(call enable-to-config,ELF_LOADER))
$(eval $(call enable-to-config,T2C))

# Performance options
$(eval $(call enable-to-config,MOP_FUSION))
$(eval $(call enable-to-config,BLOCK_CHAINING))
$(eval $(call enable-to-config,LTO))

# Debugging
$(eval $(call enable-to-config,GDBSTUB))
$(eval $(call enable-to-config,UBSAN))

# Graphics and audio
$(eval $(call enable-to-config,SDL))
$(eval $(call enable-to-config,SDL_MIXER))

# Testing
$(eval $(call enable-to-config,ARCH_TEST))

# Build options
$(eval $(call enable-to-config,PREBUILT))

# Special handling for JIT mode - set INTERPRETER_ONLY based on JIT
# JIT requires coordinated CONFIG_INTERPRETER_ONLY setting, so handled separately
ifdef ENABLE_JIT
    ENABLE_JIT_NORMALIZED := $(call normalize-bool,$(ENABLE_JIT))
    ifeq ($(ENABLE_JIT_NORMALIZED),y)
        override CONFIG_INTERPRETER_ONLY := n
        override CONFIG_JIT := y
    else ifeq ($(ENABLE_JIT_NORMALIZED),n)
        override CONFIG_INTERPRETER_ONLY := y
        override CONFIG_JIT := n
    else
        $(warning Invalid value for ENABLE_JIT='$(ENABLE_JIT)'. Use 0/1, y/n, yes/no, or true/false.)
    endif
endif

# Handle optimization level
ifdef OPT_LEVEL
    # Convert -O0, -O2, -Ofast to numeric levels
    ifeq ($(OPT_LEVEL),-O0)
        override CONFIG_OPTIMIZE_LEVEL := 0
    else ifeq ($(OPT_LEVEL),-O1)
        override CONFIG_OPTIMIZE_LEVEL := 1
    else ifeq ($(OPT_LEVEL),-O2)
        override CONFIG_OPTIMIZE_LEVEL := 2
    else ifeq ($(OPT_LEVEL),-O3)
        override CONFIG_OPTIMIZE_LEVEL := 3
    else ifeq ($(OPT_LEVEL),-Ofast)
        override CONFIG_OPTIMIZE_LEVEL := 3
    else ifeq ($(OPT_LEVEL),-Os)
        override CONFIG_OPTIMIZE_SIZE := y
    else
        $(warning Invalid OPT_LEVEL='$(OPT_LEVEL)'. Use -O0, -O1, -O2, -O3, -Ofast, or -Os.)
    endif
endif

# Handle INITRD_SIZE for system emulation
ifdef INITRD_SIZE
    override CONFIG_INITRD_SIZE := $(INITRD_SIZE)
endif
