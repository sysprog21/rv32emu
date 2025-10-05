include mk/common.mk
include mk/toolchain.mk

# Verify GNU make version (3.80+ required for order-only prerequisites)
ifeq ($(filter 3.80 3.81 3.82 3.83 3.84 4.% 5.%,$(MAKE_VERSION)),)
$(error GNU make 3.80 or higher is required. Current version: $(MAKE_VERSION))
endif

OUT ?= build
BIN := $(OUT)/rv32emu

CONFIG_FILE := $(OUT)/.config
-include $(CONFIG_FILE)

OPT_LEVEL ?= -O2

CFLAGS = -std=gnu99 $(OPT_LEVEL) -Wall -Wextra -Werror
CFLAGS += -Wno-unused-label
CFLAGS += -include src/common.h -Isrc/

OBJS_EXT :=

# In the system test suite, the executable is an ELF file (e.g., MMU).
# However, the Linux kernel emulation includes the Image, DT, and
# root filesystem (rootfs). Therefore, the test suite needs this
# flag to load the ELF and differentiate it from the kernel emulation.
ENABLE_ELF_LOADER ?= 0
$(call set-feature, ELF_LOADER)

# Enable MOP fusion, easier for ablation study
ENABLE_MOP_FUSION ?= 1
$(call set-feature, MOP_FUSION)

# Enable block chaining, easier for ablation study
ENABLE_BLOCK_CHAINING ?= 1
$(call set-feature, BLOCK_CHAINING)

# Enable logging with color
ENABLE_LOG_COLOR ?= 1
$(call set-feature, LOG_COLOR)

# Enable system emulation
ENABLE_SYSTEM ?= 0
$(call set-feature, SYSTEM)

ifeq ($(call has, SYSTEM), 1)
    OBJS_EXT += system.o
endif

# Definition that bridges:
#   Device Tree(initrd, memory range)
#   src/io.c(memory init)
#   src/riscv.c(system emulation layout init)
# Note: These memory settings are for SYSTEM mode only (when ELF_LOADER=0)
ifeq ($(call has, SYSTEM), 1)
ifeq ($(call has, ELF_LOADER), 0)
MiB = 1024*1024
MEM_START ?= 0
MEM_SIZE ?= 512 # unit in MiB
DTB_SIZE ?= 1 # unit in MiB
INITRD_SIZE ?= 8 # unit in MiB

compute_size = $(shell echo "obase=16; ibase=10; $(1)*$(MiB)" | bc)
REAL_MEM_SIZE = $(call compute_size, $(MEM_SIZE))
REAL_DTB_SIZE = $(call compute_size, $(DTB_SIZE))
REAL_INITRD_SIZE = $(call compute_size, $(INITRD_SIZE))

CFLAGS_dt += -DMEM_START=0x$(MEM_START) \
             -DMEM_END=0x$(shell echo "obase=16; ibase=16; $(MEM_START)+$(REAL_MEM_SIZE)" | bc) \
             -DINITRD_START=0x$(shell echo "obase=16; ibase=16; \
                              $(REAL_MEM_SIZE) - $(call compute_size, ($(INITRD_SIZE)+$(DTB_SIZE)))" | bc) \
             -DINITRD_END=0x$(shell echo "obase=16; ibase=16; \
                            $(REAL_MEM_SIZE) - $(call compute_size, $(DTB_SIZE)) - 1" | bc)

# Memory size for SYSTEM mode (may be overridden by FULL4G if ENABLE_ELF_LOADER=1)
CFLAGS += -DMEM_SIZE=0x$(REAL_MEM_SIZE) -DDTB_SIZE=0x$(REAL_DTB_SIZE) -DINITRD_SIZE=0x$(REAL_INITRD_SIZE)
endif
endif

ENABLE_ARCH_TEST ?= 0
$(call set-feature, ARCH_TEST)

# ThreadSanitizer support
# TSAN on x86-64 memory layout:
#   Shadow: 0x02a000000000 - 0x7cefffffffff (reserved by TSAN)
#   App:    0x7cf000000000 - 0x7ffffffff000 (usable by application)
#
# We use MAP_FIXED to allocate FULL4G's 4GB memory at a fixed address
# (0x7d0000000000) within TSAN's app range, ensuring compatibility.
#
# IMPORTANT: TSAN requires ASLR (Address Space Layout Randomization) to be
# disabled to prevent system allocations from landing in TSAN's shadow memory.
# Tests are run with 'setarch $(uname -m) -R' to disable ASLR.
ENABLE_TSAN ?= 0
ifeq ("$(ENABLE_TSAN)", "1")
override ENABLE_SDL := 0       # SDL (uninstrumented system lib) creates threads TSAN cannot track
override ENABLE_LTO := 0       # LTO interferes with TSAN instrumentation
CFLAGS += -DTSAN_ENABLED       # Signal code to use TSAN-compatible allocations
# Disable ASLR for TSAN tests to prevent allocations in TSAN shadow memory
BIN_WRAPPER = setarch $(shell uname -m) -R
else
BIN_WRAPPER =
endif

# Enable link-time optimization (LTO)
ENABLE_LTO ?= 1
ifeq ($(call has, LTO), 1)
ifeq ("$(CC_IS_CLANG)$(CC_IS_GCC)$(CC_IS_EMCC)", "")
$(warning LTO is only supported in clang, gcc and emcc.)
override ENABLE_LTO := 0
endif
endif
$(call set-feature, LTO)
ifeq ($(call has, LTO), 1)
ifeq ("$(CC_IS_EMCC)", "1")
ifeq ($(call has, SDL), 1)
$(warning LTO is not supported to build emscripten-port SDL using emcc.)
else
CFLAGS += -flto
endif
endif
ifeq ("$(CC_IS_GCC)", "1")
CFLAGS += -flto=auto
endif
ifeq ("$(CC_IS_CLANG)", "1")
CFLAGS += -flto=thin -fsplit-lto-unit
LDFLAGS += -flto=thin
endif
endif

# Disable Intel's Control-flow Enforcement Technology (CET)
CFLAGS += $(CFLAGS_NO_CET)

# Integer Multiplication and Division instructions
ENABLE_EXT_M ?= 1
$(call set-feature, EXT_M)

# Atomic Instructions
ENABLE_EXT_A ?= 1
$(call set-feature, EXT_A)

# Single-precision floating point instructions
ENABLE_EXT_F ?= 1
$(call set-feature, EXT_F)
ifeq ($(call has, EXT_F), 1)
AR := ar
ifeq ("$(CC_IS_CLANG)", "1")
    # On macOS, system ar works with Apple Clang's LTO
    ifeq ($(UNAME_S),Darwin)
        # macOS: system ar is sufficient
    else
        # Non-macOS with Clang: check if LTO is enabled
        ifeq ($(call has, LTO), 1)
            # LTO requires llvm-ar to handle LLVM bitcode in object files
            LLVM_AR := $(shell which llvm-ar 2>/dev/null)
            ifeq ($(LLVM_AR),)
                $(error llvm-ar not found. Install LLVM or disable LTO with ENABLE_LTO=0)
            endif
            AR = llvm-ar
        else
            # LTO disabled: prefer llvm-ar if available, otherwise use system ar
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

ifeq ($(call has, SYSTEM), 1)
DEV_OUT := $(OUT)/devices
endif
OBJS_NEED_SOFTFLOAT := $(OUT)/decode.o \
                       $(OUT)/riscv.o \
                       $(DEV_OUT)/uart.o \
                       $(DEV_OUT)/plic.o
$(OBJS_NEED_SOFTFLOAT): $(SOFTFLOAT_LIB)
LDFLAGS += $(SOFTFLOAT_LIB)
LDFLAGS += -lm
endif

# Compressed extension instructions
ENABLE_EXT_C ?= 1
$(call set-feature, EXT_C)

# RV32E Base Integer Instruction Set
ENABLE_RV32E ?= 0
$(call set-feature, RV32E)

# Control and Status Register (CSR)
ENABLE_Zicsr ?= 1
$(call set-feature, Zicsr)

# Instruction-Fetch Fence
ENABLE_Zifencei ?= 1
$(call set-feature, Zifencei)

# Zba Address generation instructions
ENABLE_Zba ?= 1
$(call set-feature, Zba)

# Zbb Basic bit-manipulation
ENABLE_Zbb ?= 1
$(call set-feature, Zbb)

# Zbc Carry-less multiplication
ENABLE_Zbc ?= 1
$(call set-feature, Zbc)

# Zbs Single-bit instructions
ENABLE_Zbs ?= 1
$(call set-feature, Zbs)

ENABLE_FULL4G ?= 0

# Experimental SDL oriented system calls
ENABLE_SDL ?= 1
ENABLE_SDL_MIXER ?= 1
ifneq ("$(CC_IS_EMCC)", "1") # note that emcc generates port SDL headers/library, so it does not requires system SDL headers/library
ifeq ($(call has, SDL), 1)
ifeq (, $(shell which sdl2-config))
$(warning No sdl2-config in $$PATH. Check SDL2 installation in advance)
override ENABLE_SDL := 0
endif
ifeq (1, $(shell pkg-config --exists SDL2_mixer; echo $$?))
$(warning No SDL2_mixer lib installed. SDL2_mixer support will be disabled)
override ENABLE_SDL_MIXER := 0
endif
endif
endif
$(call set-feature, SDL)
$(call set-feature, SDL_MIXER)
ifeq ($(call has, SDL), 1)
OBJS_EXT += syscall_sdl.o
ifneq ("$(CC_IS_EMCC)", "1")
$(OUT)/syscall_sdl.o: CFLAGS += $(shell sdl2-config --cflags)
endif
# 4 GiB of memory is required to run video games.
ENABLE_FULL4G := 1
ifneq ("$(CC_IS_EMCC)", "1")
LDFLAGS += $(shell sdl2-config --libs) -pthread
ifeq ($(call has, SDL_MIXER), 1)
LDFLAGS += $(shell pkg-config --libs SDL2_mixer)
endif
endif
endif

# If SYSTEM is enabled and ELF_LOADER is not, then skip FULL4G bacause guestOS
# has dedicated memory mapping range.
ifeq ($(call has, SYSTEM), 1)
ifeq ($(call has, ELF_LOADER), 0)
override ENABLE_FULL4G := 0
endif
endif

# Full access to a 4 GiB address space, necessitating more memory mapping
# during emulator initialization.
$(call set-feature, FULL4G)

# Configuration validation and conflict detection
ifeq ($(call has, SDL), 1)
ifeq ($(call has, SYSTEM), 1)
ifeq ($(call has, ELF_LOADER), 0)
ifeq ($(call has, FULL4G), 0)
    $(warning SDL requires FULL4G=1 but SYSTEM forces FULL4G=0. Set ENABLE_ELF_LOADER=1 or disable SDL/SYSTEM)
endif
endif
endif
endif

ifeq ($(call has, FULL4G), 1)
# Note: If both SYSTEM and FULL4G are enabled with ELF_LOADER=1,
# this MEM_SIZE definition will override the SYSTEM mode definition.
# This is intentional for ELF loader use cases.
CFLAGS += -DMEM_SIZE=0xFFFFFFFFULL # 2^{32} - 1
endif

ENABLE_GDBSTUB ?= 0
$(call set-feature, GDBSTUB)
ifeq ($(call has, GDBSTUB), 1)
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

ENABLE_JIT ?= 0
$(call set-feature, JIT)
ifeq ($(call has, JIT), 1)
    OBJS_EXT += jit.o
    ENABLE_T2C ?= 1
    $(call set-feature, T2C)
    ifeq ($(call has, T2C), 1)
        # tier-2 JIT compiler is powered by LLVM
        LLVM_CONFIG = llvm-config-18
        LLVM_CONFIG := $(shell which $(LLVM_CONFIG))
        ifndef LLVM_CONFIG
            # Try Homebrew on macOS
            LLVM_CONFIG = /opt/homebrew/opt/llvm@18/bin/llvm-config
            LLVM_CONFIG := $(shell which $(LLVM_CONFIG))
            ifdef LLVM_CONFIG
                LDFLAGS += -L/opt/homebrew/opt/llvm@18/lib
            endif
        endif
        ifeq ("$(LLVM_CONFIG)", "")
            $(error No llvm-config-18 installed. Check llvm-config-18 installation in advance, or use "ENABLE_T2C=0" to disable tier-2 LLVM compiler)
        endif
        ifeq ("$(findstring -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS, "$(shell $(LLVM_CONFIG) --cflags)")", "")
            $(error No llvm-config-18 installed. Check llvm-config-18 installation in advance, or use "ENABLE_T2C=0" to disable tier-2 LLVM compiler)
        endif
        CHECK_LLVM_LIBS := $(shell $(LLVM_CONFIG) --libs 2>/dev/null 1>&2; echo $$?)
        ifeq ("$(CHECK_LLVM_LIBS)", "0")
            OBJS_EXT += t2c.o
            CFLAGS += -g $(shell $(LLVM_CONFIG) --cflags)
            LDFLAGS += $(shell $(LLVM_CONFIG) --libfiles)
        else
            $(error No llvm-config-18 installed. Check llvm-config-18 installation in advance, or use "ENABLE_T2C=0" to disable tier-2 LLVM compiler)
        endif
    else
        $(warning T2C (tier-2 compiler) is disabled. Using tier-1 JIT only.)
    endif
    ifneq ($(processor),$(filter $(processor),x86_64 aarch64 arm64))
        $(error JIT mode only supports for x64 and arm64 target currently.)
    endif

$(OUT)/jit.o: src/jit.c src/rv32_jit.c
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) -c -MMD -MF $@.d $<

$(OUT)/t2c.o: src/t2c.c src/t2c_template.c
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) -c -MMD -MF $@.d $<
endif
# For tail-call elimination, we need a specific set of build flags applied.
# FIXME: On macOS + Apple Silicon, -fno-stack-protector might have a negative impact.

ENABLE_UBSAN ?= 0
ifeq ("$(ENABLE_UBSAN)", "1")
CFLAGS += -fsanitize=undefined -fno-sanitize=alignment -fno-sanitize-recover=all
LDFLAGS += -fsanitize=undefined -fno-sanitize=alignment -fno-sanitize-recover=all
endif

# ThreadSanitizer flags (ENABLE_TSAN is set earlier to override SDL/FULL4G)
ifeq ("$(ENABLE_TSAN)", "1")
CFLAGS += -fsanitize=thread -g
LDFLAGS += -fsanitize=thread
endif

$(OUT)/emulate.o: CFLAGS += -foptimize-sibling-calls -fomit-frame-pointer -fno-stack-check -fno-stack-protector

# .DEFAULT_GOAL should be set to all since the very first target is not all
# after including "mk/external.mk"
.DEFAULT_GOAL := all

include mk/external.mk
include mk/artifact.mk
include mk/system.mk
include mk/wasm.mk

DTB_DEPS :=
ifeq ($(call has, SYSTEM), 1)
ifeq ($(call has, ELF_LOADER), 0)
DTB_DEPS := $(BUILD_DTB) $(BUILD_DTB2C)
endif
endif

all: config $(DTB_DEPS) $(BUILD_DTB) $(BUILD_DTB2C) $(BIN)

OBJS := \
    map.o \
    utils.o \
    decode.o \
    io.o \
    syscall.o

# em_runtime.o should prior to emulate.o, otherwise wasm-ld fails to link
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
deps += $(OBJS:%.o=%.o.d) # mk/system.mk includes prior this line, so declare deps at there

ifeq ($(call has, EXT_F), 1)
$(OBJS): $(SOFTFLOAT_LIB)
endif

ifeq ($(call has, GDBSTUB), 1)
$(OBJS): $(GDBSTUB_LIB)
endif

$(OUT)/%.o: src/%.c $(CONFIG_FILE) $(deps_emcc) | $(OUT)
	$(Q)mkdir -p $(dir $@)
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) $(CFLAGS_emcc) -c -MMD -MF $@.d $<

$(OUT):
	$(Q)mkdir -p $@

$(BIN): $(OBJS) $(DEV_OBJS) | $(OUT)
	$(VECHO) "  LD\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS_emcc) $^ $(LDFLAGS)

$(CONFIG_FILE): FORCE
	$(Q)mkdir -p $(OUT)
	$(Q)echo "$(CFLAGS)" | xargs -n1 | sort | sed -n 's/^RV32_FEATURE/ENABLE/p' > $@.tmp
	$(Q)if ! cmp -s $@ $@.tmp 2>/dev/null; then \
		mv $@.tmp $@; \
		$(PRINTF) "Configuration updated. Check $(OUT)/.config for configured items.\n"; \
	else \
		$(RM) $@.tmp; \
	fi

.PHONY: FORCE config
FORCE:
config: $(CONFIG_FILE)

# Tools
include mk/tools.mk
tool: $(TOOLS_BIN)

# RISC-V Architecture Tests
include mk/riscv-arch-test.mk
include mk/tests.mk

# the prebuilt executables are built for "rv32im"
CHECK_ELF_FILES :=

ifeq ($(call has, EXT_M), 1)
CHECK_ELF_FILES += \
	puzzle \
	fcalc \
	pi
endif

EXPECTED_hello = Hello World!
EXPECTED_puzzle = success in 2005 trials
EXPECTED_fcalc = Performed 12 tests, 0 failures, 100% success rate.
EXPECTED_pi = 3.141592653589793238462643383279502884197169399375105820974944592307816406286208998628034825342117067982148086

LOG_FILTER=sed -E '/^[0-9]{2}:[0-9]{2}:[0-9]{2} /d'

# $(1): rv32emu's extra CLI parameter
# $(2): ELF executable
# $(3): ELF executable name
# $(4): extra command in the pipeline
# $(5): expected output
define check-test
$(Q)true; \
$(PRINTF) "Running $(3) ... "; \
OUTPUT_FILE="$$(mktemp)"; \
if (LC_ALL=C $(BIN_WRAPPER) $(BIN) $(1) $(2) > "$$OUTPUT_FILE") && \
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

EXPECTED_mmu = STORE PAGE FAULT TEST PASSED!
mmu-test: $(BIN)
	$(call check-test, , tests/system/mmu/vm.elf, vm.elf, tail -n 1,$(EXPECTED_mmu))

# Non-trivial demonstration programs
ifeq ($(call has, SDL), 1)
doom_action := (cd $(OUT); LC_ALL=C ../$(BIN) riscv32/doom)
doom_deps += $(DOOM_DATA) $(BIN)
doom: artifact $(doom_deps)
	$(doom_action)

ifeq ($(call has, EXT_F), 1)
quake_action := (cd $(OUT); LC_ALL=C ../$(BIN) riscv32/quake)
quake_deps += $(QUAKE_DATA) $(BIN)
quake: artifact $(quake_deps)
	$(quake_action)
endif
endif

CLANG_FORMAT := $(shell which clang-format-18 2>/dev/null)

SHFMT := $(shell which shfmt 2>/dev/null)

BLACK := $(shell which black 2>/dev/null)
BLACK_VERSION := $(if $(strip $(BLACK)),$(shell $(BLACK) --version | head -n 1 | awk '{print $$2}'),)
BLACK_MAJOR := $(shell echo $(BLACK_VERSION) | cut -f1 -d.)
BLACK_MINOR := $(shell echo $(BLACK_VERSION) | cut -f2 -d.)
BLACK_PATCH := $(shell echo $(BLACK_VERSION) | cut -f3 -d.)
BLACK_ATLEAST_MAJOR := 25
BLACK_ATLEAST_MINOR := 1
BLACK_ATLEAST_PATCH := 0
BLACK_FORMAT_WARNING := Python format check might fail at CI as you use version $(BLACK_VERSION). \
                        You may switch black to version $(BLACK_ATLEAST_MAJOR).$(BLACK_ATLEAST_MINOR).$(BLACK_ATLEAST_PATCH)

SUBMODULES := $(shell git config --file .gitmodules --get-regexp path | awk '{ print $$2 }')
SUBMODULES_PRUNE_PATHS := $(shell for subm in $(SUBMODULES); do echo -n "-path \"./$$subm\" -o "; done | sed 's/ -o $$//')

format:
ifeq ($(CLANG_FORMAT),)
	$(error clang-format-18 not found. Install clang-format version 18 and try again)
else
        # Skip formatting submodules and everything in $(OUT), apply the same rule for shfmt and black
	$(Q)$(CLANG_FORMAT) -i $(shell find . \( $(SUBMODULES_PRUNE_PATHS) -o -path \"./$(OUT)\" \) \
		                               -prune -o -name "*.[ch]" -print)
endif
ifeq ($(SHFMT),)
	$(error shfmt not found. Install shfmt and try again)
else
	$(Q)$(SHFMT) -w $(shell find . \( $(SUBMODULES_PRUNE_PATHS) -o -path \"./$(OUT)\" \) \
		                        -prune -o -name "*.sh" -print)
endif
ifeq ($(BLACK),)
	$(error black not found. Install black version 25.1.0 or above and try again)
else
        ifeq ($(call version_lt,\
                $(BLACK_MAJOR),$(BLACK_MINOR),$(BLACK_PATCH),\
                $(BLACK_ATLEAST_MAJOR),$(BLACK_ATLEAST_MINOR),$(BLACK_ATLEAST_PATCH)), 1)
		$(warning $(BLACK_FORMAT_WARNING))
        else
		$(Q)$(BLACK) --quiet $(shell find . \( $(SUBMODULES_PRUNE_PATHS) -o -path \"./$(OUT)\" \) \
		                             -prune -o \( -name "*.py" -o -name "*.pyi" \) -print)
        endif
endif
	$(Q)$(call notice,All files are properly formatted.)

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
	$(Q)-$(RM) $(OUT)/.config
	$(Q)-$(RM) -r $(SOFTFLOAT_DUMMY_PLAT) $(OUT)/softfloat
	$(Q)$(call notice, [OK])

.PHONY: all config tool check check-hello misalign misalign-in-blk-emu mmu-test
.PHONY: gdbstub-test doom quake format clean distclean artifact

-include $(deps)
