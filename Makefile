# Build system for rv32emu
#
# Quick start:
#   make defconfig    # Apply default configuration
#   make              # Build rv32emu
#   make check        # Run tests
#
# See README for more options.

.DEFAULT_GOAL := all

# Verify GNU Make version (3.80+ required for order-only prerequisites)
ifeq ($(filter 3.80 3.81 3.82 3.83 3.84 4.% 5.% 6.% 7.% 8.% 9.%,$(MAKE_VERSION)),)
$(error GNU Make 3.80 or higher is required. Current version: $(MAKE_VERSION))
endif

# Build Framework
include mk/common.mk

# Kconfig Integration
include mk/kconfig.mk

# Load configuration (before toolchain.mk so CONFIG_BUILD_WASM affects CC)
# .config is required for build targets - run 'make defconfig' to generate
ifeq ($(NEEDS_CONFIG),yes)
ifeq ($(wildcard .config),)
$(info No .config found. Please run one of:)
$(info )
$(info   make defconfig        - Apply default configuration)
$(info   make config           - Interactive configuration menu)
$(info   make ci_defconfig     - CI with architecture tests)
$(info   make jit_defconfig    - Enable JIT compilation)
$(info   make mini_defconfig   - Minimal build for embedded use)
$(info   make system_defconfig - System emulation mode)
$(info   make wasm_defconfig   - WebAssembly build)
$(info )
$(error .config required. Run 'make defconfig' first.)
endif
endif
-include .config
include mk/compat.mk

# Toolchain detection (after .config to support BUILD_WASM)
include mk/toolchain.mk
include mk/deps.mk
$(eval $(require-config))

# Build Configuration
OUT ?= build
BIN := $(OUT)/rv32emu

CFLAGS = -std=gnu11 $(KCONFIG_CFLAGS) -Wall -Wextra -Werror
CFLAGS += -Wno-unused-label -include src/common.h -Isrc/ $(CFLAGS_NO_CET)
LDFLAGS += $(KCONFIG_LDFLAGS)
OBJS_EXT :=
deps :=

# Feature Flags (Kconfig -> RV32_FEATURE_*)
$(call set-features, ELF_LOADER MOP_FUSION BLOCK_CHAINING LOG_COLOR)
$(call set-features, SYSTEM GOLDFISH_RTC ARCH_TEST)
$(call set-features, EXT_M EXT_A EXT_F EXT_C RV32E)
$(call set-features, Zicsr Zifencei Zba Zbb Zbc Zbs)
$(call set-features, SDL SDL_MIXER GDBSTUB JIT)

# Extension: Floating Point
ifeq ($(CONFIG_EXT_F),y)
AR := ar
ifeq ("$(CC_IS_CLANG)", "1")
    ifeq ($(UNAME_S),Darwin)
        # macOS: system ar is sufficient
    else ifeq ($(CONFIG_LTO),y)
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
ifeq ("$(CC_IS_EMCC)", "1")
AR = emar
endif
include mk/softfloat.mk
OBJS_NEED_SOFTFLOAT := $(OUT)/decode.o $(OUT)/riscv.o
ifeq ($(CONFIG_SYSTEM),y)
DEV_OUT := $(OUT)/devices
OBJS_NEED_SOFTFLOAT += $(DEV_OUT)/uart.o $(DEV_OUT)/plic.o
endif
$(OBJS_NEED_SOFTFLOAT): $(SOFTFLOAT_LIB)
LDFLAGS += $(SOFTFLOAT_LIB) -lm
endif

# Extension: SDL Graphics
ifeq ($(CONFIG_SDL),y)
ifneq ("$(CC_IS_EMCC)", "1")
    ifeq ($(SKIP_DEPS_CHECK),)
    ifeq ($(HAVE_SDL2),)
        $(warning SDL2 not found. Run 'make config' to disable SDL.)
    endif
    endif
    ifneq ($(HAVE_SDL2),)
        OBJS_EXT += syscall_sdl.o
        $(OUT)/syscall_sdl.o: CFLAGS += $(SDL2_CFLAGS)
        LDFLAGS += $(SDL2_LIBS) -pthread
        ifeq ($(CONFIG_SDL_MIXER),y)
            ifneq ($(HAVE_SDL2_MIXER),)
                LDFLAGS += $(SDL2_MIXER_LIBS)
            else ifeq ($(SKIP_DEPS_CHECK),)
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
	$(call ensure-submodule,src/mini-gdbstub,https://github.com/RinHizakura/mini-gdbstub)
GDBSTUB_LIB := $(GDBSTUB_OUT)/libgdbstub.a
$(GDBSTUB_LIB): src/mini-gdbstub/Makefile
	$(MAKE) -C $(dir $<) O=$(dir $@)
OBJS_EXT += gdbstub.o breakpoint.o
CFLAGS += -D'GDBSTUB_COMM="$(GDBSTUB_COMM)"'
LDFLAGS += $(GDBSTUB_LIB) -pthread
gdbstub-test: $(BIN) artifact
	$(Q).ci/gdbstub-test.sh && $(call notice, [OK])
endif

# Extension: JIT Compilation
ifeq ($(CONFIG_JIT),y)
    OBJS_EXT += jit.o
    T2C_ENABLED := 0
    ifeq ($(CONFIG_T2C),y)
        # LLVM detection using helpers from mk/toolchain.mk
        # User can override with: make LLVM_CONFIG=/path/to/llvm-config
        ifndef LLVM_CONFIG
            LLVM_CONFIG := $(call detect-llvm-config)
        endif
        ifneq ($(LLVM_CONFIG),)
            LLVM_VERSION := $(call llvm-version,$(LLVM_CONFIG))
            ifeq ($(call llvm-check-libs,$(LLVM_CONFIG)),0)
                T2C_ENABLED := 1
                OBJS_EXT += t2c.o
                CFLAGS += -g $(call llvm-cflags,$(LLVM_CONFIG))
                LDFLAGS += $(call llvm-libfiles,$(LLVM_CONFIG))
                # Add Homebrew library path if needed
                HOMEBREW_LLVM_PREFIX := $(call detect-homebrew-llvm-prefix)
                ifneq ($(HOMEBREW_LLVM_PREFIX),)
                ifneq ($(findstring $(HOMEBREW_LLVM_PREFIX),$(LLVM_CONFIG)),)
                    LDFLAGS += -L$(HOMEBREW_LLVM_PREFIX)/lib
                endif
                endif
            else
                $(warning LLVM $(LLVM_VERSION) libraries not found. T2C disabled.)
            endif
        else
            $(warning llvm-config ($(LLVM_MIN_VERSION)-$(LLVM_MAX_VERSION)) not found. T2C disabled.)
        endif
    endif
    CFLAGS += -DRV32_FEATURE_T2C=$(T2C_ENABLED)
    # JIT requires x86_64 or ARM64; skip check for WASM builds (emcc cross-compiles)
    ifneq ($(CC_IS_EMCC),1)
        ifneq ($(UNAME_M),$(filter $(UNAME_M),x86_64 aarch64 arm64))
            $(error JIT only supports x86_64 and ARM64 platforms.)
        endif
    endif
$(OUT)/jit.o: src/jit.c src/rv32_jit.c $(CONFIG_HEADER)
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) -c -MMD -MF $@.d $<
# T2C optimization level from Kconfig (0-3, default 3)
T2C_OPT_LEVEL ?= $(or $(CONFIG_T2C_OPT_LEVEL),3)
$(OUT)/t2c.o: src/t2c.c src/t2c_template.c $(CONFIG_HEADER)
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) -DCONFIG_T2C_OPT_LEVEL=$(T2C_OPT_LEVEL) -c -MMD -MF $@.d $<
else
    CFLAGS += -DRV32_FEATURE_T2C=0
endif

# Tail-call optimization
$(OUT)/emulate.o: CFLAGS += -foptimize-sibling-calls -fomit-frame-pointer -fno-stack-check -fno-stack-protector

# HTTP Utilities (shared by external.mk and artifact.mk)
include mk/http.mk

# External Dependencies & System Emulation
include mk/external.mk
include mk/artifact.mk
include mk/system.mk
include mk/wasm.mk

# Build Targets
DTB_DEPS :=
ifeq ($(CONFIG_SYSTEM),y)
ifneq ($(CONFIG_ELF_LOADER),y)
DTB_DEPS := $(BUILD_DTB) $(BUILD_DTB2C)
# Ensure DTC is cloned before building DTB
$(BUILD_DTB) $(BUILD_DTB2C): $(DTC_SENTINEL)
endif
endif

OBJS := map.o utils.o decode.o io.o syscall.o
ifeq ($(CC_IS_EMCC), 1)
OBJS += em_runtime.o
endif
OBJS += emulate.o riscv.o log.o elf.o cache.o mpool.o $(OBJS_EXT) main.o
OBJS := $(addprefix $(OUT)/, $(OBJS))
deps += $(OBJS:%.o=%.o.d)

ifeq ($(CONFIG_EXT_F),y)
$(OBJS): $(SOFTFLOAT_LIB)
endif
ifeq ($(CONFIG_GDBSTUB),y)
$(OBJS): $(GDBSTUB_LIB)
endif

$(OUT)/%.o: src/%.c $(deps_emcc) $(CONFIG_HEADER) | $(OUT)
	$(Q)mkdir -p $(dir $@)
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) $(CFLAGS_emcc) -c -MMD -MF $@.d $<

$(OUT):
	$(Q)mkdir -p $@

# Link the final binary
$(BIN): $(OBJS) $(DEV_OBJS) | $(OUT)
	$(VECHO) "  LD\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS_emcc) $^ $(LDFLAGS)

all: $(DTB_DEPS) $(BIN)
	@$(call notice, Build complete: $(BIN))

# Tools & Testing
include mk/tools.mk
include mk/riscv-arch-test.mk
include mk/tests.mk

tool: $(TOOLS_BIN)

# Clean Targets
clean:
	$(VECHO) "Cleaning... "
	$(Q)$(RM) $(BIN) $(OBJS) $(DEV_OBJS) $(BUILD_DTB) $(BUILD_DTB2C) $(HIST_BIN) $(HIST_OBJS) $(deps) $(WEB_FILES) $(CACHE_OUT)
	$(Q)-$(RM) $(SOFTFLOAT_LIB)
	$(Q)$(call notice, [OK])

# Clean build objects and config (preserves artifacts for CI efficiency)
cleanconfig: clean
	$(VECHO) "Removing config files... "
	$(Q)-$(RM) .config $(CONFIG_HEADER)
	$(Q)-$(RM) -r $(SOFTFLOAT_DUMMY_PLAT) $(OUT)/softfloat
	$(Q)$(call notice, [OK])

distclean: cleanconfig
	$(VECHO) "Deleting all generated files... "
	$(Q)$(RM) -r $(OUT)/id1 $(DEMO_DIR) $(OUT)/mini-gdbstub $(OUT)/devices
	$(Q)$(RM) *.zip
	$(Q)$(RM) -r $(OUT)/linux-x86-softfp $(OUT)/riscv32 $(OUT)/linux-image
	$(Q)$(RM) $(OUT)/sha1sum-* $(OUT)/.stamp-* $(OUT)/.verify_result
	$(Q)$(RM) $(OUT)/rv32emu-prebuilt*.tar.gz $(OUT)/rv32emu-prebuilt-sail-*
	$(Q)$(call notice, [OK])

.PHONY: all tool clean cleanconfig distclean gdbstub-test

-include $(deps)
