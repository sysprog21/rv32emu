include mk/common.mk
include mk/toolchain.mk

OUT ?= build
BIN := $(OUT)/rv32emu

CONFIG_FILE := $(OUT)/.config
-include $(CONFIG_FILE)

CFLAGS = -std=gnu99 -O2 -Wall -Wextra
CFLAGS += -Wno-unused-label
CFLAGS += -include src/common.h

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
CFLAGS += -flto
endif
ifeq ("$(CC_IS_CLANG)", "1")
CFLAGS += -flto=thin -fsplit-lto-unit
LDFLAGS += -flto=thin
endif
endif

# Disable Intel's Control-flow Enforcement Technology (CET)
CFLAGS += $(CFLAGS_NO_CET)

OBJS_EXT :=

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
ifeq ("$(CC_IS_EMCC)", "1")
AR = emar
endif
SOFTFLOAT_OUT = $(abspath $(OUT)/softfloat)
src/softfloat/build/Linux-RISCV-GCC/Makefile:
	git submodule update --init src/softfloat/
SOFTFLOAT_LIB := $(SOFTFLOAT_OUT)/softfloat.a
$(SOFTFLOAT_LIB): src/softfloat/build/Linux-RISCV-GCC/Makefile
	$(MAKE) -C $(dir $<) BUILD_DIR=$(SOFTFLOAT_OUT) CC=$(CC) AR=$(AR)
$(OUT)/decode.o $(OUT)/riscv.o: $(SOFTFLOAT_LIB)
LDFLAGS += $(SOFTFLOAT_LIB)
LDFLAGS += -lm
endif

# Compressed extension instructions
ENABLE_EXT_C ?= 1
$(call set-feature, EXT_C)

# Control and Status Register (CSR)
ENABLE_Zicsr ?= 1
$(call set-feature, Zicsr)

# Instruction-Fetch Fence
ENABLE_Zifencei ?= 1
$(call set-feature, Zifencei)

# Experimental SDL oriented system calls
ENABLE_SDL ?= 1
ifneq ("$(CC_IS_EMCC)", "1") # note that emcc generates port SDL headers/library, so it does not requires system SDL headers/library
ifeq ($(call has, SDL), 1)
ifeq (, $(shell which sdl2-config))
$(warning No sdl2-config in $$PATH. Check SDL2 installation in advance)
override ENABLE_SDL := 0
endif
ifeq (1, $(shell pkg-config --exists SDL2_mixer; echo $$?))
$(warning No SDL2_mixer lib installed. Check SDL2_mixer installation in advance)
override ENABLE_SDL := 0
endif
endif
$(call set-feature, SDL)
ifeq ($(call has, SDL), 1)
OBJS_EXT += syscall_sdl.o
$(OUT)/syscall_sdl.o: CFLAGS += $(shell sdl2-config --cflags)
# 4 GiB of memory is required to run video games.
$(OUT)/main.o: CFLAGS += -DMEM_SIZE=0xFFFFFFFFULL # 2^{32} - 1
LDFLAGS += $(shell sdl2-config --libs) -pthread
LDFLAGS += $(shell pkg-config --libs SDL2_mixer)
endif
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
# FIXME: track gdbstub dependency properly
$(OUT)/decode.o: $(GDBSTUB_LIB)
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
# tier-2 JIT compiler powered LLVM
LLVM_CONFIG = llvm-config-17
LLVM_CONFIG := $(shell which $(LLVM_CONFIG))
ifndef LLVM_CONFIG
# Try Homebrew on macOS
LLVM_CONFIG = /opt/homebrew/opt/llvm@17/bin/llvm-config
LLVM_CONFIG := $(shell which $(LLVM_CONFIG))
ifdef LLVM_CONFIG
LDFLAGS += -L/opt/homebrew/opt/llvm@17/lib
endif
endif
ifneq ("$(LLVM_CONFIG)", "")
ifneq ("$(findstring -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS, "$(shell $(LLVM_CONFIG) --cflags)")", "")
ENABLE_T2C := 1
$(call set-feature, T2C)
OBJS_EXT += t2c.o
CFLAGS += -g $(shell $(LLVM_CONFIG) --cflags)
LDFLAGS += $(shell $(LLVM_CONFIG) --libs)
else
ENABLE_T2C := 0
$(call set-feature, T2C)
$(warning No llvm-config-17 installed. Check llvm-config-17 installation in advance)
endif
endif

ifneq ($(processor),$(filter $(processor),x86_64 aarch64 arm64))
$(error JIT mode only supports for x64 and arm64 target currently.)
endif

src/rv32_jit.c:
	$(Q)tools/gen-jit-template.py $(CFLAGS) > $@

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

$(OUT)/emulate.o: CFLAGS += -foptimize-sibling-calls -fomit-frame-pointer -fno-stack-check -fno-stack-protector

# .DEFAULT_GOAL should be set to all since the very first target is not all
# after including "mk/external.mk"
.DEFAULT_GOAL := all

include mk/external.mk
include mk/artifact.mk
include mk/wasm.mk

all: config $(BIN)

OBJS := \
	map.o \
	utils.o \
	decode.o \
	io.o \
	syscall.o \
	emulate.o \
	riscv.o \
	elf.o \
	cache.o \
	mpool.o \
	$(OBJS_EXT) \
	main.o

OBJS := $(addprefix $(OUT)/, $(OBJS))
deps := $(OBJS:%.o=%.o.d)

ifeq ($(call has, EXT_F), 1)
$(OBJS): $(SOFTFLOAT_LIB)
endif

$(OUT)/%.o: src/%.c $(deps_emcc)
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) $(CFLAGS_emcc) -c -MMD -MF $@.d $<

$(BIN): $(OBJS)
	$(VECHO) "  LD\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS_emcc) $^ $(LDFLAGS)

config: $(CONFIG_FILE)
$(CONFIG_FILE):
	$(Q)echo "$(CFLAGS)" | xargs -n1 | sort | sed -n 's/^RV32_FEATURE/ENABLE/p' > $@
	$(VECHO) "Check the file $(OUT)/.config for configured items.\n"

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

check-hello: $(BIN)
	$(Q)$(PRINTF) "Running hello.elf ..."; \
	    if [ "$(shell $(BIN) $(OUT)/hello.elf | uniq)" = "$(strip $(EXPECTED_hello)) inferior exit code 0" ]; then \
	    $(call notice, [OK]); \
	    else \
	    $(PRINTF) "Failed.\n"; \
	    exit 1; \
	    fi;

check: $(BIN) check-hello artifact
	$(Q)$(foreach e,$(CHECK_ELF_FILES),\
	    $(PRINTF) "Running $(e) ... "; \
	    if [ "$(shell $(BIN) $(OUT)/riscv32/$(e) | uniq)" = "$(strip $(EXPECTED_$(e))) inferior exit code 0" ]; then \
	    $(call notice, [OK]); \
	    else \
	    $(PRINTF) "Failed.\n"; \
	    exit 1; \
	    fi; \
	)

EXPECTED_aes_sha1 = 1242a6757c8aef23e50b5264f5941a2f4b4a347e  -
misalign: $(BIN) artifact
	$(Q)$(PRINTF) "Running uaes ... ";
	$(Q)if [ "$(shell $(BIN) -m $(OUT)/riscv32/uaes | $(SHA1SUM))" = "$(EXPECTED_aes_sha1)" ]; then \
	    $(call notice, [OK]); \
	    else \
	    $(PRINTF) "Failed.\n"; \
	    fi

# Non-trivial demonstration programs
ifeq ($(call has, SDL), 1)
doom_action := (cd $(OUT); ../$(BIN) riscv32/doom)
doom_deps += $(DOOM_DATA) $(BIN)
doom: artifact $(doom_deps)
	$(doom_action)

ifeq ($(call has, EXT_F), 1)
quake_action := (cd $(OUT); ../$(BIN) riscv32/quake)
quake_deps += $(QUAKE_DATA) $(BIN)
quake: artifact $(quake_deps)
	$(quake_action)
endif
endif

clean:
	$(RM) $(BIN) $(OBJS) $(HIST_BIN) $(HIST_OBJS) $(deps) $(WEB_FILES) $(CACHE_OUT) src/rv32_jit.c
distclean: clean
	-$(RM) $(DOOM_DATA) $(QUAKE_DATA)
	$(RM) -r $(TIMIDITY_DATA)
	$(RM) -r $(OUT)/id1
	$(RM) -r $(DEMO_DIR)
	$(RM) *.zip
	$(RM) -r $(OUT)/mini-gdbstub
	-$(RM) $(OUT)/.config
	-$(RM) -r $(OUT)/softfloat

-include $(deps)
