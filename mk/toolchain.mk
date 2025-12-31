# Compiler and toolchain detection with Kconfig integration
#
# This file detects the compiler type and sets up appropriate flags.
# Works with Kconfig-based configuration via CONFIG_* variables.

ifndef _MK_TOOLCHAIN_INCLUDED
_MK_TOOLCHAIN_INCLUDED := 1

# Cross-compilation support
# Note: CROSS_COMPILE is auto-detected below if not provided by user
SYSROOT ?=

# Platform Detection

UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

ifeq ($(UNAME_S),Darwin)
    PRINTF = printf
else
    PRINTF = env printf
endif

ifeq ($(UNAME_M),x86_64)
    HOST_PLATFORM := x86
else ifeq ($(UNAME_M),aarch64)
    HOST_PLATFORM := aarch64
else ifeq ($(UNAME_M),arm64)
    HOST_PLATFORM := arm64
else
    HOST_PLATFORM := unknown
endif

# Compiler Detection

# Set defaults
CC      ?= cc
AR      ?= ar
RANLIB  ?= ranlib
STRIP   ?= strip

# Apply cross-compile prefix
ifneq ($(CROSS_COMPILE),)
    ifeq ($(origin CC),default)
        CC      := $(CROSS_COMPILE)$(CC)
        AR      := $(CROSS_COMPILE)$(AR)
        RANLIB  := $(CROSS_COMPILE)$(RANLIB)
        STRIP   := $(CROSS_COMPILE)$(STRIP)
    endif
endif

# Host toolchain for build-time tools
HOSTCC  ?= cc
HOSTAR  ?= ar

# Detect compiler type from version string
# Note: Pipe directly to grep instead of storing in variable to avoid shell escaping issues
CC_IS_EMCC  :=
CC_IS_CLANG :=
CC_IS_GCC   :=

ifneq ($(shell $(CC) --version | head -n 1 | grep emcc),)
    CC_IS_EMCC := 1
else ifneq ($(shell $(CC) --version | head -n 1 | grep clang),)
    CC_IS_CLANG := 1
else ifneq ($(shell $(CC) --version 2>&1 | grep "Free Software Foundation"),)
    CC_IS_GCC := 1
endif

# Verify supported compiler
ifeq ("$(CC_IS_CLANG)$(CC_IS_GCC)$(CC_IS_EMCC)", "")
$(error Unsupported compiler. Only GCC, Clang, and Emscripten are supported.)
endif

# Emscripten version detection
ifeq ("$(CC_IS_EMCC)", "1")
    EMCC_VERSION := $(shell $(CC) --version | head -n 1 | cut -f10 -d ' ')
    EMCC_MAJOR := $(shell echo $(EMCC_VERSION) | cut -f1 -d.)
    EMCC_MINOR := $(shell echo $(EMCC_VERSION) | cut -f2 -d.)
    EMCC_PATCH := $(shell echo $(EMCC_VERSION) | cut -f3 -d.)

    # Override toolchain for Emscripten
    AR     := emar
    RANLIB := emranlib
    STRIP  := emstrip
endif

# RISC-V Cross-Compiler Detection

TOOLCHAIN_LIST := riscv-none-elf- \
                  riscv32-unknown-elf- \
                  riscv64-unknown-elf- \
                  riscv-none-embed-

define check-cross-tools
$(shell which $(1)gcc >/dev/null 2>&1 && \
        which $(1)cpp >/dev/null 2>&1 && \
        echo | $(1)cpp -dM - 2>/dev/null | grep __riscv >/dev/null && \
        echo "$(1) ")
endef

# Auto-detect RISC-V toolchain if not provided by user
ifeq ($(CROSS_COMPILE),)
    CROSS_COMPILE := $(word 1,$(foreach prefix,$(TOOLCHAIN_LIST),$(call check-cross-tools,$(prefix))))
endif
export CROSS_COMPILE

# CET Protection Flags

CFLAGS_NO_CET :=
processor := $(shell uname -m)
ifeq ($(processor),$(filter $(processor),i386 x86_64))
    # Disable Intel's Control-flow Enforcement Technology for JIT
    CFLAGS_NO_CET := -fcf-protection=none
endif

# macOS Linker Compatibility

ifeq ($(UNAME_S),Darwin)
    ifneq ("$(CC_IS_CLANG)$(CC_IS_GCC)", "")
        # Xcode 15+ warns about duplicate -l options
        LD_VERSION := $(shell ld -version_details 2>/dev/null | head -n 1)
        ifneq ($(shell echo "$(LD_VERSION)" | grep -E "15\.[0-9]"),)
            LDFLAGS += -Wl,-no_warn_duplicate_libraries
        endif
    endif
endif

# Kconfig-derived build flags

KCONFIG_CFLAGS :=
KCONFIG_LDFLAGS :=

# Optimization level
ifeq ($(CONFIG_OPTIMIZE_SIZE),y)
    KCONFIG_CFLAGS += -Os
else
    OPT_LEVEL := $(if $(CONFIG_OPTIMIZE_LEVEL),-O$(CONFIG_OPTIMIZE_LEVEL),-O2)
    KCONFIG_CFLAGS += $(OPT_LEVEL)
endif

# Debug symbols
ifeq ($(CONFIG_DEBUG_SYMBOLS),y)
    KCONFIG_CFLAGS += -g
endif

# Link-time optimization
ifeq ($(CONFIG_LTO),y)
    ifeq ("$(CC_IS_EMCC)", "1")
        ifeq ($(CONFIG_SDL),y)
            $(warning LTO is not supported for SDL builds with Emscripten)
        else
            KCONFIG_CFLAGS += -flto
            KCONFIG_LDFLAGS += -flto
        endif
    else ifeq ("$(CC_IS_GCC)", "1")
        KCONFIG_CFLAGS += -flto=auto
        KCONFIG_LDFLAGS += -flto=auto
    else ifeq ("$(CC_IS_CLANG)", "1")
        KCONFIG_CFLAGS += -flto=thin -fsplit-lto-unit
        KCONFIG_LDFLAGS += -flto=thin
    endif
endif

# Undefined behavior sanitizer
ifeq ($(CONFIG_UBSAN),y)
    KCONFIG_CFLAGS += -fsanitize=undefined -fno-sanitize=alignment -fno-sanitize-recover=all
    KCONFIG_LDFLAGS += -fsanitize=undefined -fno-sanitize=alignment -fno-sanitize-recover=all
endif

# Sysroot support
ifneq ($(SYSROOT),)
    KCONFIG_CFLAGS += --sysroot=$(SYSROOT)
    KCONFIG_LDFLAGS += --sysroot=$(SYSROOT)
endif

# LLVM 18 Detection for T2C

# Find LLVM 18 config (centralized detection)
# Priority: llvm-config-18 > Homebrew llvm@18 > llvm-config with version check
# Note: Each fallback is independent - brew presence doesn't block llvm-config check
define find-llvm-config
$(strip \
    $(or \
        $(shell which llvm-config-18 2>/dev/null),\
        $(shell brew --prefix llvm@18 2>/dev/null | xargs -I{} sh -c 'test -x {}/bin/llvm-config && echo {}/bin/llvm-config' 2>/dev/null),\
        $(shell which llvm-config 2>/dev/null | xargs -I{} sh -c '{} --version 2>/dev/null | grep -q "^18\." && echo {}' 2>/dev/null)))
endef

# Simpler Homebrew detection for library path
HOMEBREW_LLVM_PREFIX := $(shell which brew >/dev/null 2>&1 && brew --prefix llvm@18 2>/dev/null)

# Export for use in Makefile
LLVM_CONFIG := $(call find-llvm-config)
LLVM_FROM_HOMEBREW := $(if $(and $(HOMEBREW_LLVM_PREFIX),$(findstring $(HOMEBREW_LLVM_PREFIX),$(LLVM_CONFIG))),yes,)

# Add Homebrew library path if using Homebrew LLVM
ifeq ($(LLVM_FROM_HOMEBREW),yes)
    LDFLAGS += -L$(HOMEBREW_LLVM_PREFIX)/lib
endif

# Version Comparison Utilities

version_num = $(shell printf "%d%03d%03d" $(1) $(2) $(3) 2>/dev/null || echo 0)
version_eq = $(shell echo "$$(($(call version_num,$(1),$(2),$(3)) == $(call version_num,$(4),$(5),$(6))))")
version_lt = $(shell echo "$$(($(call version_num,$(1),$(2),$(3)) < $(call version_num,$(4),$(5),$(6))))")
version_lte = $(shell echo "$$(($(call version_num,$(1),$(2),$(3)) <= $(call version_num,$(4),$(5),$(6))))")
version_gt = $(shell echo "$$(($(call version_num,$(1),$(2),$(3)) > $(call version_num,$(4),$(5),$(6))))")
version_gte = $(shell echo "$$(($(call version_num,$(1),$(2),$(3)) >= $(call version_num,$(4),$(5),$(6))))")

endif # _MK_TOOLCHAIN_INCLUDED
