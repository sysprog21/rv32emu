# Build system for rv32emu
#
# Quick start:
#   make defconfig    # Apply default configuration
#   make              # Build rv32emu
#   make check        # Run tests
#
# Configuration:
#   make config       # Interactive menuconfig
#   make defconfig    # Default (SDL, all extensions)
#   make oldconfig    # Update config after Kconfig changes
#   make savedefconfig # Save current config as defconfig
#
# Named configurations (in configs/):
#   make defconfig CONFIG=mini       # Apply configs/mini_defconfig
#   make defconfig CONFIG=system     # Apply configs/system_defconfig
#   make defconfig CONFIG=jit        # Apply configs/jit_defconfig
#   make defconfig CONFIG=wasm       # Apply configs/wasm_defconfig
#   make defconfig CONFIG=ci         # Apply configs/ci_defconfig

.DEFAULT_GOAL := all

# Build Framework Setup

# Include build utilities first (sets Q, VECHO, OUT, etc.)
include mk/common.mk
include mk/toolchain.mk
include mk/deps.mk

# Verify GNU Make version (3.80+ required for order-only prerequisites)
ifeq ($(filter 3.80 3.81 3.82 3.83 3.84 4.% 5.%,$(MAKE_VERSION)),)
$(error GNU Make 3.80 or higher is required. Current version: $(MAKE_VERSION))
endif

# Kconfig Integration

KCONFIG_DIR := tools/kconfig
KCONFIG := configs/Kconfig
CONFIG_HEADER := src/rv32emu_config.h

# Kconfig tool source (kconfiglib)
KCONFIGLIB_REPO := https://github.com/sysprog21/Kconfiglib

# Download and setup Kconfig tools if missing
$(KCONFIG_DIR)/kconfiglib.py:
	$(VECHO) "Downloading Kconfig tools...\n"
	$(Q)git clone --depth=1 -q $(KCONFIGLIB_REPO) $(KCONFIG_DIR)
	@echo "Kconfig tools installed to $(KCONFIG_DIR)"

# Ensure all Kconfig tools exist
$(KCONFIG_DIR)/menuconfig.py $(KCONFIG_DIR)/defconfig.py $(KCONFIG_DIR)/genconfig.py \
$(KCONFIG_DIR)/oldconfig.py $(KCONFIG_DIR)/savedefconfig.py: $(KCONFIG_DIR)/kconfiglib.py

# Load existing configuration (safe include)
-include .config

# Auto-generate .config at parse time if missing (for ENABLE_* backward compatibility)
# This ensures CONFIG_* values from defconfig are available during set-feature evaluation.
# Without this, features not explicitly passed via ENABLE_* would default to 0 instead
# of their defconfig values.
# Skip for config-related targets that don't need or will generate .config themselves.
# Note: Empty MAKECMDGOALS means default goal (all), which needs .config
ifneq ($(CONFIG_CONFIGURED),y)
ifeq ($(filter $(CONFIG_TARGETS) $(DEFCONFIG_GOALS),$(MAKECMDGOALS)),)
ifneq ($(wildcard $(KCONFIG_DIR)/defconfig.py),)
$(shell python3 $(KCONFIG_DIR)/defconfig.py --kconfig $(KCONFIG) configs/defconfig >/dev/null 2>&1)
# Re-include .config now that it's been generated
-include .config
endif
endif
endif

# Backward compatibility for ENABLE_* flags (allows CI to work without changes)
include mk/compat.mk

# Configuration validation (only for build targets)
$(eval $(require-config))

# Kconfig Targets

# Run environment detection before showing menu
env-check:
	@echo "Checking build environment..."
	@python3 tools/detect-env.py --summary
	@echo ""

# Interactive configuration (depends on Kconfig tools)
config: env-check $(KCONFIG_DIR)/menuconfig.py
	@python3 $(KCONFIG_DIR)/menuconfig.py $(KCONFIG)
	@python3 $(KCONFIG_DIR)/genconfig.py --header-path $(CONFIG_HEADER) $(KCONFIG)
	@echo "Configuration saved to .config and $(CONFIG_HEADER)"

# Apply default configuration (supports CONFIG=name for named configs)
defconfig: $(KCONFIG_DIR)/defconfig.py
	@if [ -n "$(CONFIG)" ]; then \
		if [ -f "configs/$(CONFIG)_defconfig" ]; then \
			echo "Applying configs/$(CONFIG)_defconfig..."; \
			python3 $(KCONFIG_DIR)/defconfig.py --kconfig $(KCONFIG) configs/$(CONFIG)_defconfig; \
		else \
			echo "Error: configs/$(CONFIG)_defconfig not found"; \
			exit 1; \
		fi; \
	else \
		echo "Applying default configuration..."; \
		python3 $(KCONFIG_DIR)/defconfig.py --kconfig $(KCONFIG) configs/defconfig; \
	fi
	@python3 $(KCONFIG_DIR)/genconfig.py --header-path $(CONFIG_HEADER) $(KCONFIG)
	@echo "Configuration applied."

# Pattern rule for named defconfigs (e.g., make jit_defconfig, make mini_defconfig)
%_defconfig: $(KCONFIG_DIR)/defconfig.py
	@if [ -f "configs/$*_defconfig" ]; then \
		echo "Applying configs/$*_defconfig..."; \
		python3 $(KCONFIG_DIR)/defconfig.py --kconfig $(KCONFIG) configs/$*_defconfig; \
		python3 $(KCONFIG_DIR)/genconfig.py --header-path $(CONFIG_HEADER) $(KCONFIG); \
		echo "Configuration applied."; \
	else \
		echo "Error: configs/$*_defconfig not found"; \
		exit 1; \
	fi

# Update configuration after Kconfig changes
oldconfig: $(KCONFIG_DIR)/oldconfig.py
	@python3 $(KCONFIG_DIR)/oldconfig.py $(KCONFIG)
	@python3 $(KCONFIG_DIR)/genconfig.py --header-path $(CONFIG_HEADER) $(KCONFIG)

# Save current configuration as minimal defconfig
savedefconfig: $(KCONFIG_DIR)/savedefconfig.py
	@python3 $(KCONFIG_DIR)/savedefconfig.py --kconfig $(KCONFIG) --out defconfig.new
	@echo "Saved minimal config to defconfig.new"

# Explicit target to download/update Kconfig tools
kconfig-tools: $(KCONFIG_DIR)/kconfiglib.py
	@echo "Kconfig tools are ready in $(KCONFIG_DIR)"

# Explicit rule to generate .config if missing (fallback for cases where
# parse-time generation didn't run, e.g., kconfig tools weren't present yet)
# Use order-only prerequisite (|) to only check existence, not timestamp,
# so user-customized .config isn't overwritten when defconfig.py is updated.
.config: | $(KCONFIG_DIR)/defconfig.py
	@python3 $(KCONFIG_DIR)/defconfig.py --kconfig $(KCONFIG) configs/defconfig
	@echo "Generated .config from default configuration"

# Auto-generate config header from .config
$(CONFIG_HEADER): .config $(KCONFIG_DIR)/genconfig.py
	@python3 $(KCONFIG_DIR)/genconfig.py --header-path $(CONFIG_HEADER) $(KCONFIG)
	@echo "Generated $(CONFIG_HEADER)"

# Build Configuration

OUT ?= build
BIN := $(OUT)/rv32emu

CFLAGS = -std=gnu99 $(KCONFIG_CFLAGS) -Wall -Wextra -Werror
CFLAGS += -Wno-unused-label
CFLAGS += -include src/common.h -Isrc/

# Disable Intel CET for JIT compatibility
CFLAGS += $(CFLAGS_NO_CET)

LDFLAGS += $(KCONFIG_LDFLAGS)
OBJS_EXT :=
deps :=

# Feature Flags from Kconfig

# Convert Kconfig options to RV32_FEATURE_* compiler flags
$(call set-feature, ELF_LOADER)
$(call set-feature, MOP_FUSION)
$(call set-feature, BLOCK_CHAINING)
$(call set-feature, LOG_COLOR)
$(call set-feature, SYSTEM)
$(call set-feature, ARCH_TEST)
$(call set-feature, EXT_M)
$(call set-feature, EXT_A)
$(call set-feature, EXT_F)
$(call set-feature, EXT_C)
$(call set-feature, RV32E)
$(call set-feature, Zicsr)
$(call set-feature, Zifencei)
$(call set-feature, Zba)
$(call set-feature, Zbb)
$(call set-feature, Zbc)
$(call set-feature, Zbs)
$(call set-feature, SDL)
$(call set-feature, SDL_MIXER)
$(call set-feature, GDBSTUB)
$(call set-feature, JIT)
# Note: T2C is set conditionally after LLVM detection in JIT section

# Extension: Floating Point (F)

ifeq ($(CONFIG_EXT_F),y)
AR := ar
ifeq ("$(CC_IS_CLANG)", "1")
    ifeq ($(UNAME_S),Darwin)
        # macOS: system ar is sufficient
    else
        ifeq ($(CONFIG_LTO),y)
            LLVM_AR := $(shell which llvm-ar 2>/dev/null)
            ifeq ($(LLVM_AR),)
                $(error llvm-ar required for LTO with Clang. Install LLVM or disable LTO.)
            endif
            AR = llvm-ar
        else
            LLVM_AR := $(shell which llvm-ar 2>/dev/null)
            ifneq ($(LLVM_AR),)
                AR = llvm-ar
            endif
        endif
    endif
endif
ifeq ("$(CC_IS_EMCC)", "1")
AR = emar
endif

# Berkeley SoftFloat
include mk/softfloat.mk

OBJS_NEED_SOFTFLOAT := $(OUT)/decode.o $(OUT)/riscv.o
ifeq ($(CONFIG_SYSTEM),y)
DEV_OUT := $(OUT)/devices
OBJS_NEED_SOFTFLOAT += $(DEV_OUT)/uart.o $(DEV_OUT)/plic.o
endif
$(OBJS_NEED_SOFTFLOAT): $(SOFTFLOAT_LIB)
LDFLAGS += $(SOFTFLOAT_LIB)
LDFLAGS += -lm
endif

# Extension: SDL Graphics

ifeq ($(CONFIG_SDL),y)
ifneq ("$(CC_IS_EMCC)", "1")
    # Native SDL
    ifeq ($(HAVE_SDL2),)
        $(warning SDL2 not found. Run 'make config' to disable SDL.)
    else
        OBJS_EXT += syscall_sdl.o
        $(OUT)/syscall_sdl.o: CFLAGS += $(SDL2_CFLAGS)
        LDFLAGS += $(SDL2_LIBS) -pthread
        ifeq ($(CONFIG_SDL_MIXER),y)
            ifeq ($(HAVE_SDL2_MIXER),y)
                LDFLAGS += $(SDL2_MIXER_LIBS)
            else
                $(warning SDL2_mixer not found. Audio disabled.)
            endif
        endif
    endif
endif
endif

# Extension: GDB Stub

ifeq ($(CONFIG_GDBSTUB),y)
GDBSTUB_OUT = $(abspath $(OUT)/mini-gdbstub)
GDBSTUB_COMM = 127.0.0.1:1234

src/mini-gdbstub/Makefile:
	git submodule update --init $(dir $@)

GDBSTUB_LIB := $(GDBSTUB_OUT)/libgdbstub.a
$(GDBSTUB_LIB): src/mini-gdbstub/Makefile
	$(MAKE) -C $(dir $<) O=$(dir $@)

OBJS_EXT += gdbstub.o breakpoint.o
CFLAGS += -D'GDBSTUB_COMM="$(GDBSTUB_COMM)"'
LDFLAGS += $(GDBSTUB_LIB) -pthread

gdbstub-test: $(BIN)
	$(Q).ci/gdbstub-test.sh && $(call notice, [OK])
endif

# Extension: JIT Compilation

ifeq ($(CONFIG_JIT),y)
    OBJS_EXT += jit.o

    # Tier-2 LLVM compiler - uses centralized detection from mk/toolchain.mk
    T2C_ENABLED := 0
    ifeq ($(CONFIG_T2C),y)
        ifneq ($(LLVM_CONFIG),)
            CHECK_LLVM := $(shell $(LLVM_CONFIG) --libs 2>/dev/null 1>&2; echo $$?)
            ifeq ($(CHECK_LLVM),0)
                T2C_ENABLED := 1
                OBJS_EXT += t2c.o
                CFLAGS += -g $(shell $(LLVM_CONFIG) --cflags)
                LDFLAGS += $(shell $(LLVM_CONFIG) --libfiles)
            else
                $(warning LLVM 18 libraries not found. T2C disabled.)
            endif
        else
            $(warning llvm-config-18 not found. T2C disabled.)
        endif
    endif
    CFLAGS += -DRV32_FEATURE_T2C=$(T2C_ENABLED)

    # Platform check (use UNAME_M for raw architecture, not HOST_PLATFORM which maps for artifacts)
    ifneq ($(UNAME_M),$(filter $(UNAME_M),x86_64 aarch64 arm64))
        $(error JIT only supports x86_64 and ARM64 platforms.)
    endif

$(OUT)/jit.o: src/jit.c src/rv32_jit.c $(CONFIG_HEADER)
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) -c -MMD -MF $@.d $<

$(OUT)/t2c.o: src/t2c.c src/t2c_template.c $(CONFIG_HEADER)
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) -c -MMD -MF $@.d $<
else
    # T2C disabled when JIT is disabled
    CFLAGS += -DRV32_FEATURE_T2C=0
endif

# Tail-call optimization flags
$(OUT)/emulate.o: CFLAGS += -foptimize-sibling-calls -fomit-frame-pointer -fno-stack-check -fno-stack-protector

# External Dependencies
include mk/external.mk
include mk/artifact.mk
include mk/system.mk
include mk/wasm.mk

# Build Targets

DTB_DEPS :=
ifeq ($(CONFIG_SYSTEM),y)
ifneq ($(CONFIG_ELF_LOADER),y)
DTB_DEPS := $(BUILD_DTB) $(BUILD_DTB2C)
endif
endif

# Core objects
OBJS := \
    map.o \
    utils.o \
    decode.o \
    io.o \
    syscall.o

# Emscripten runtime (must precede emulate.o)
ifeq ($(CC_IS_EMCC), 1)
OBJS += em_runtime.o
endif

OBJS += \
    emulate.o \
    riscv.o \
    log.o \
    elf.o \
    cache.o \
    mpool.o \
    $(OBJS_EXT) \
    main.o

OBJS := $(addprefix $(OUT)/, $(OBJS))
deps += $(OBJS:%.o=%.o.d)

# Object dependencies
ifeq ($(CONFIG_EXT_F),y)
$(OBJS): $(SOFTFLOAT_LIB)
endif

ifeq ($(CONFIG_GDBSTUB),y)
$(OBJS): $(GDBSTUB_LIB)
endif

# Compilation rules
$(OUT)/%.o: src/%.c $(deps_emcc) $(CONFIG_HEADER) | $(OUT)
	$(Q)mkdir -p $(dir $@)
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) $(CFLAGS_emcc) -c -MMD -MF $@.d $<

$(OUT):
	$(Q)mkdir -p $@

$(BIN): $(OBJS) $(DEV_OBJS) | $(OUT)
	$(VECHO) "  LD\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS_emcc) $^ $(LDFLAGS)

# Main target
all: $(DTB_DEPS) $(BIN)
	@$(call notice, Build complete: $(BIN))

# Tools
include mk/tools.mk
tool: $(TOOLS_BIN)

# Testing

include mk/riscv-arch-test.mk
include mk/tests.mk

CHECK_ELF_FILES :=
ifeq ($(CONFIG_EXT_M),y)
CHECK_ELF_FILES += puzzle fcalc pi
endif

EXPECTED_hello = Hello World!
EXPECTED_puzzle = success in 2005 trials
EXPECTED_fcalc = Performed 12 tests, 0 failures, 100% success rate.
EXPECTED_pi = 3.141592653589793238462643383279502884197169399375105820974944592307816406286208998628034825342117067982148086

LOG_FILTER=sed -E '/^[0-9]{2}:[0-9]{2}:[0-9]{2} /d'

define check-test
$(Q)true; \
$(PRINTF) "Running $(3) ... "; \
OUTPUT_FILE="$$(mktemp)"; \
if (LC_ALL=C $(BIN) $(1) $(2) > "$$OUTPUT_FILE") && \
   [ "$$(cat "$$OUTPUT_FILE" | $(LOG_FILTER) | $(4))" = "$(5)" ]; then \
    $(call notice, [OK]); \
else \
    $(PRINTF) "Failed.\n"; \
    exit 1; \
fi; \
$(RM) "$$OUTPUT_FILE"
endef

check-hello: $(BIN)
	$(call check-test, , $(OUT)/hello.elf, hello.elf, uniq,$(EXPECTED_hello))

check: $(BIN) check-hello artifact
	$(Q)$(foreach e, $(CHECK_ELF_FILES), $(call check-test, , $(OUT)/riscv32/$(e), $(e), uniq,$(EXPECTED_$(e))))

EXPECTED_aes_sha1 = 89169ec034bec1c6bb2c556b26728a736d350ca3  -
misalign: $(BIN) artifact
	$(call check-test, -m, $(OUT)/riscv32/uaes, uaes.elf, $(SHA1SUM),$(EXPECTED_aes_sha1))

EXPECTED_misalign = MISALIGNED INSTRUCTION FETCH TEST PASSED!
misalign-in-blk-emu: $(BIN)
	$(call check-test, , tests/system/alignment/misalign.elf, misalign.elf, tail -n 1,$(EXPECTED_misalign))

EXPECTED_mmu = Store page fault test passed!
mmu-test: $(BIN)
	$(call check-test, , tests/system/mmu/vm.elf, vm.elf, tail -n 1,$(EXPECTED_mmu))

# Demo Applications

ifeq ($(CONFIG_SDL),y)
doom_action := (cd $(OUT); LC_ALL=C ../$(BIN) riscv32/doom)
doom_deps += $(DOOM_DATA) $(BIN)
doom: artifact $(doom_deps)
	$(doom_action)

ifeq ($(CONFIG_EXT_F),y)
quake_action := (cd $(OUT); LC_ALL=C ../$(BIN) riscv32/quake)
quake_deps += $(QUAKE_DATA) $(BIN)
quake: artifact $(quake_deps)
	$(quake_action)
endif
endif

# Code Formatting

CLANG_FORMAT := $(shell which clang-format-20 2>/dev/null)
SHFMT := $(shell which shfmt 2>/dev/null)
DTSFMT := $(shell which dtsfmt 2>/dev/null)
BLACK := $(shell which black 2>/dev/null)

SUBMODULES := $(shell git config --file .gitmodules --get-regexp path 2>/dev/null | awk '{ print $$2 }')
SUBMODULES_PRUNE_PATHS := $(shell for subm in $(SUBMODULES); do echo -n "-path \"./$$subm\" -o "; done | sed 's/ -o $$//')

format:
ifeq ($(CLANG_FORMAT),)
	$(error clang-format-20 not found. Install clang-format version 20.)
endif
	$(Q)$(CLANG_FORMAT) -i $(shell find . \( $(SUBMODULES_PRUNE_PATHS) -o -path "./$(OUT)" \) \
	                                   -prune -o -name "*.[ch]" -print)
ifeq ($(SHFMT),)
	$(error shfmt not found.)
endif
	$(Q)$(SHFMT) -w $(shell find . \( $(SUBMODULES_PRUNE_PATHS) -o -path "./$(OUT)" \) \
	                            -prune -o -name "*.sh" -print)
ifeq ($(DTSFMT),)
	$(error dtsfmt not found.)
endif
	$(Q)for dts_src in $$(find . \( $(SUBMODULES_PRUNE_PATHS) -o -path "./$(OUT)" \) \
	                -prune -o \( -name "*.dts" -o -name "*.dtsi" \) -print); do \
		$(DTSFMT) $$dts_src; \
	done
ifeq ($(BLACK),)
	$(error black not found. Install black version 25.1.0+.)
endif
	$(Q)$(BLACK) --quiet $(shell find . \( $(SUBMODULES_PRUNE_PATHS) -o -path "./$(OUT)" \) \
	                             -prune -o \( -name "*.py" -o -name "*.pyi" \) -print)
	$(Q)$(call notice, All files formatted.)

# Clean Targets

clean:
	$(VECHO) "Cleaning... "
	$(Q)$(RM) $(BIN) $(OBJS) $(DEV_OBJS) $(BUILD_DTB) $(BUILD_DTB2C) $(HIST_BIN) $(HIST_OBJS) $(deps) $(WEB_FILES) $(CACHE_OUT)
	$(Q)-$(RM) $(SOFTFLOAT_LIB)
	$(Q)$(call notice, [OK])

distclean: clean
	$(VECHO) "Deleting all generated files... "
	$(Q)$(RM) -r $(OUT)/id1
	$(Q)$(RM) -r $(DEMO_DIR)
	$(Q)$(RM) *.zip
	$(Q)$(RM) -r $(OUT)/mini-gdbstub
	$(Q)$(RM) -r $(OUT)/devices
	$(Q)-$(RM) .config $(CONFIG_HEADER)
	$(Q)-$(RM) -r $(SOFTFLOAT_DUMMY_PLAT) $(OUT)/softfloat
	$(Q)$(call notice, [OK])

.PHONY: all config defconfig oldconfig savedefconfig env-check kconfig-tools
.PHONY: tool check check-hello misalign misalign-in-blk-emu mmu-test
.PHONY: gdbstub-test doom quake format clean distclean artifact

-include $(deps)
