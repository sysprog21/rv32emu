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
$(info   make defconfig               - Apply default configuration)
$(info   make config                  - Interactive configuration menu)
$(info   make ci_defconfig            - CI with architecture tests)
$(info   make jit_defconfig           - Enable JIT compilation)
$(info   make mini_defconfig          - Minimal build for embedded use)
$(info   make system_defconfig        - System emulation mode)
$(info   make wasm_defconfig          - WebAssembly build)
$(info   make wasm_system_defconfig   - WebAssembly build for system emulation mode)
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

# Define the effective-config stamp before including mk/system.mk so device
# object rules can depend on it. The stamp tracks effective feature values,
# including legacy ENABLE_* overrides, and forces stale objects to rebuild
# when command-line configuration changes.
EFFECTIVE_CONFIG_STAMP := $(OUT)/.effective-config

CFLAGS = -std=gnu11 $(KCONFIG_CFLAGS) -Wall -Wextra -Werror
# Cross-TU entry points must be declared in a header rather than by a local
# extern at each use site, and an empty parameter list must not stand in for
# (void), which would disable argument checking.
CFLAGS += -Wmissing-prototypes -Wstrict-prototypes
CFLAGS += -Wno-unused-label -include src/common.h -Isrc/ $(CFLAGS_NO_CET)
LDFLAGS += $(KCONFIG_LDFLAGS)
OBJS_EXT :=
deps :=

# Feature Flags (Kconfig -> RV32_FEATURE_*)
$(call set-features, ELF_LOADER MOP_FUSION BLOCK_CHAINING LOG_COLOR)
$(call set-features, SYSTEM GOLDFISH_RTC ARCH_TEST)
$(call set-features, VIRTIO_NET VIRTIO_NET_TAP VIRTIO_NET_USER VIRTIO_NET_VMNET)
$(call set-features, EXT_M EXT_A EXT_F EXT_C EXT_V RV32E)
$(call set-features, Zicsr Zifencei Zba Zbb Zbc Zbs)
$(call set-features, SDL SDL_MIXER GDBSTUB JIT LINK_ZLIB)

# Extension: Floating Point
ifeq ($(CONFIG_EXT_F),y)
AR := ar
ifeq ("$(CC_IS_CLANG)", "1")
    ifeq ($(UNAME_S),Darwin)
        # macOS: system ar is sufficient
    else
        # Match llvm-ar to the same install as the LLVM we'd compile against.
        # Honor an explicit LLVM_CONFIG=/path/to/llvm-config override first,
        # then fall back to auto-detection from mk/toolchain.mk. Without this,
        # hosts with multiple LLVM majors can leave the unversioned `llvm-ar`
        # pointing at an older install, and a newer clang's LTO bitcode .o
        # files fail to archive (e.g. clang-20 bitcode + llvm-ar-18 ->
        # "Invalid attribute group entry" on `ar crs`).
        LLVM_CONFIG_FOR_AR := $(if $(LLVM_CONFIG),$(LLVM_CONFIG),$(call detect-llvm-config))
        ifneq ($(LLVM_CONFIG_FOR_AR),)
            LLVM_BINDIR_FOR_AR := $(shell $(LLVM_CONFIG_FOR_AR) --bindir 2>/dev/null)
        endif
        ifeq ($(CONFIG_LTO),y)
            ifneq ($(LLVM_BINDIR_FOR_AR),)
                ifneq ($(wildcard $(LLVM_BINDIR_FOR_AR)/llvm-ar),)
                    AR := $(LLVM_BINDIR_FOR_AR)/llvm-ar
                else
                    $(error llvm-ar not found at $(LLVM_BINDIR_FOR_AR)/llvm-ar. Install matching LLVM dev package or disable LTO.)
                endif
            else
                LLVM_AR := $(shell which llvm-ar 2>/dev/null)
                ifeq ($(LLVM_AR),)
                    $(error llvm-ar required for LTO with Clang. Install LLVM or disable LTO.)
                endif
                AR = llvm-ar
            endif
        else
            ifneq ($(LLVM_BINDIR_FOR_AR),)
                ifneq ($(wildcard $(LLVM_BINDIR_FOR_AR)/llvm-ar),)
                    AR := $(LLVM_BINDIR_FOR_AR)/llvm-ar
                endif
            else
                LLVM_AR := $(shell which llvm-ar 2>/dev/null)
                ifneq ($(LLVM_AR),)
                    AR = llvm-ar
                endif
            endif
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
OBJS_NEED_SOFTFLOAT += $(DEV_OUT)/uart.o $(DEV_OUT)/plic.o $(DEV_OUT)/virtio-net.o
endif
$(OBJS_NEED_SOFTFLOAT): $(SOFTFLOAT_LIB)
LDFLAGS += $(SOFTFLOAT_LIB) -lm
endif

# Extension: Vector
ifeq ($(CONFIG_EXT_V),y)
VLEN ?= 128
CFLAGS += -DVLEN=$(VLEN)
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
	git submodule update --init $(dir $@)
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

# VirtIO networking
include mk/virtio-net.mk

# VirtIO sound
include mk/virtio-snd.mk

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
endif
endif

OBJS := map.o utils.o decode.o io.o syscall.o trace_match.o
ifeq ($(CC_IS_EMCC), 1)
OBJS += em_runtime.o
endif
OBJS += emulate.o riscv.o log.o elf.o cache.o mpool.o $(OBJS_EXT) main.o
OBJS := $(addprefix $(OUT)/, $(OBJS))
deps += $(OBJS:%.o=%.o.d)

EFFECTIVE_CONFIG_VARS := \
	CONFIG_BUILD_WASM CONFIG_SYSTEM CONFIG_GOLDFISH_RTC CONFIG_ELF_LOADER \
	CONFIG_VIRTIO_NET CONFIG_VIRTIO_NET_TAP CONFIG_VIRTIO_NET_USER \
	CONFIG_VIRTIO_NET_VMNET CONFIG_VIRTIO_SND CONFIG_HAVE_PORTAUDIO \
	CONFIG_EXT_M CONFIG_EXT_A CONFIG_EXT_F CONFIG_EXT_C CONFIG_EXT_V CONFIG_RV32E \
	CONFIG_Zicsr CONFIG_Zifencei CONFIG_Zba CONFIG_Zbb CONFIG_Zbc CONFIG_Zbs \
	CONFIG_MOP_FUSION CONFIG_BLOCK_CHAINING CONFIG_LOG_COLOR CONFIG_ARCH_TEST \
	CONFIG_SDL CONFIG_SDL_MIXER CONFIG_GDBSTUB CONFIG_JIT CONFIG_T2C \
	CONFIG_INTERPRETER_ONLY CONFIG_OPTIMIZE_LEVEL CONFIG_OPTIMIZE_SIZE \
	CONFIG_LTO CONFIG_DEBUG_SYMBOLS CONFIG_UBSAN CONFIG_PREBUILT CONFIG_LINK_ZLIB \
	MEM_START MEM_SIZE DTB_SIZE INITRD_SIZE USER_MEM_SIZE \
	INITRD_ACTUAL_BYTES REAL_MEM_SIZE REAL_DTB_SIZE REAL_INITRD_SIZE \
	VLEN

ifeq ($(CONFIG_EXT_F),y)
$(OBJS): $(SOFTFLOAT_LIB)
endif
ifeq ($(CONFIG_GDBSTUB),y)
$(OBJS): $(GDBSTUB_LIB)
endif

$(EFFECTIVE_CONFIG_STAMP): FORCE | $(OUT)
	$(Q){ \
		printf 'CC=%s\n' '$(CC)'; \
		printf 'CC_IS_CLANG=%s\n' '$(CC_IS_CLANG)'; \
		printf 'CC_IS_EMCC=%s\n' '$(CC_IS_EMCC)'; \
		printf 'UNAME_S=%s\n' '$(UNAME_S)'; \
		printf 'CROSS_COMPILE=%s\n' '$(CROSS_COMPILE)'; \
		$(foreach var,$(EFFECTIVE_CONFIG_VARS),printf '$(var)=%s\n' '$($(var))';) \
		$(foreach var,$(EFFECTIVE_VNET_FEATURES),printf 'EFFECTIVE_$(var)=%s\n' '$(call has,$(var))';) \
		$(foreach var,$(EFFECTIVE_VSND_FEATURES),printf 'EFFECTIVE_$(var)=%s\n' '$(call has,$(var))';) \
		$(foreach var,$(EFFECTIVE_VSND_VARS),printf '$(var)=%s\n' '$($(var))';) \
	} > $@.tmp
	$(Q)if ! cmp -s $@.tmp $@ 2>/dev/null; then \
		mv $@.tmp $@; \
	else \
		rm -f $@.tmp; \
	fi

# Auto-generate decoder from ISA descriptor.
#
# src/decode.c is a tracked source file, so it is generated into a
# temporary and moved into place only once both the generator and the
# formatter have succeeded: a failed run must never leave a truncated
# src/decode.c behind with a fresh timestamp.  The style file is named
# explicitly, because clang-format otherwise searches upward from the
# temporary, and a temporary outside the tree would silently be
# formatted in clang-format's built-in style instead.
#
# Formatting is skipped when the pinned clang-format is unavailable,
# rather than failing the build; "make format" and the CI format check
# remain the enforcement points.  A clang-format that is present but
# fails is a different matter: that must abort rather than install an
# unformatted file.
#
# $(1): output path
define gen-decoder
	$(Q)python3 $(DECODER_GEN) $(DECODER_DESC) > $(1).tmp || \
		{ rm -f $(1).tmp; exit 1; }
	$(Q)if command -v $(CLANG_FORMAT) >/dev/null; then \
		$(CLANG_FORMAT) --style=file:$(CURDIR)/.clang-format -i $(1).tmp \
			|| { rm -f $(1).tmp; exit 1; }; \
	fi
	$(Q)mv $(1).tmp $(1)
endef

DECODER_DESC := src/instructions.in
DECODER_GEN := scripts/gen-decoder.py
DECODER_VERIFY := scripts/verify-tree.py

src/decode.c: $(DECODER_DESC) $(DECODER_GEN)
	$(VECHO) "  GEN\t$@\n"
	$(call gen-decoder,$@)

# Verify that the committed decoder matches what the descriptor generates,
# so src/decode.c and src/instructions.in cannot drift apart.  Note this
# only reports drift before a build: a plain "make" regenerates the file
# in place, after which it trivially matches.  CI runs it on a fresh
# checkout, which is where it does its job.
.PHONY: check-decoder
check-decoder: $(DECODER_DESC) $(DECODER_GEN) $(DECODER_VERIFY) | $(OUT)
	$(Q)python3 $(DECODER_VERIFY) $(DECODER_DESC)
	$(Q)command -v $(CLANG_FORMAT) >/dev/null || \
		{ echo "$(CLANG_FORMAT) not found."; exit 1; }
	$(call gen-decoder,$(OUT)/decode.gen.c)
	$(Q)if ! diff -u src/decode.c $(OUT)/decode.gen.c; then \
		echo "src/decode.c is stale; re-run make to regenerate it."; \
		exit 1; \
	fi
	$(Q)rm -f $(OUT)/decode.gen.c
	$(VECHO) "  DECODER\tup to date\n"

$(OUT)/%.o: src/%.c $(deps_emcc) $(CONFIG_HEADER) $(EFFECTIVE_CONFIG_STAMP) | $(OUT)
	$(Q)mkdir -p $(dir $@)
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) $(CFLAGS_emcc) -c -MMD -MF $@.d $<

$(OUT):
	$(Q)mkdir -p $@

# Link the final binary
$(BIN): $(OBJS) $(DEV_OBJS) $(DTB_DEPS) $(EFFECTIVE_CONFIG_STAMP) | $(OUT)
	$(VECHO) "  LD\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS_emcc) $(OBJS) $(DEV_OBJS) $(LDFLAGS)

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
	$(Q)$(RM) $(BIN) $(OBJS) $(DEV_OBJS_ALL) $(BUILD_DTB) $(BUILD_DTB2C) \
	    $(HIST_BIN) $(HIST_OBJS) $(deps) $(DEV_DEPS_ALL) $(WEB_FILES) \
	    $(CACHE_OUT) $(EFFECTIVE_CONFIG_STAMP) $(VIRTIO_NET_CLEAN_FILES)
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

.PHONY: all tool clean cleanconfig distclean gdbstub-test FORCE

-include $(deps)
