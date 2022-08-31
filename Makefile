include mk/common.mk
include mk/toolchain.mk

OUT ?= build
BIN := $(OUT)/rv32emu

CFLAGS = -std=gnu99 -O2 -Wall -Wextra
CFLAGS += -include common.h

# Base configurations for RISC-V extensions
CFLAGS += -D ENABLE_RV32M
CFLAGS += -D ENABLE_Zicsr
CFLAGS += -D ENABLE_Zifencei
CFLAGS += -D ENABLE_RV32A

# Set the default stack pointer
CFLAGS += -D DEFAULT_STACK_ADDR=0xFFFFF000

OBJS_EXT :=

# Compressed extension instructions
ENABLE_RV32C ?= 1
ifeq ("$(ENABLE_RV32C)", "1")
CFLAGS += -D ENABLE_RV32C
endif

# Single-precision floating point
ENABLE_RV32F ?= 1
ifeq ("$(ENABLE_RV32F)", "1")
CFLAGS += -D ENABLE_RV32F
LDFLAGS += -lm
endif

# Experimental SDL oriented system calls
ENABLE_SDL ?= 1
ifeq ("$(ENABLE_SDL)", "1")
ifeq (, $(shell which sdl2-config))
$(error "No sdl2-config in $(PATH). Check SDL2 installation in advance")
endif
CFLAGS += -D ENABLE_SDL
OBJS_EXT += syscall_sdl.o
$(OUT)/syscall_sdl.o: CFLAGS += $(shell sdl2-config --cflags)
LDFLAGS += $(shell sdl2-config --libs)
endif

# Whether to enable computed goto
ENABLE_COMPUTED_GOTO ?= 1
ifeq ("$(ENABLE_COMPUTED_GOTO)", "1")
ifeq ("$(CC_IS_CLANG)$(CC_IS_GCC)",)
$(error "Computed goto is only supported in clang and gcc.")
endif
$(OUT)/emulate.o: CFLAGS += -D ENABLE_COMPUTED_GOTO
ifeq ("$(CC_IS_GCC)", "1")
$(OUT)/emulate.o: CFLAGS += -fno-gcse -fno-crossjumping
endif
endif

ENABLE_GDBSTUB ?= 1
ifeq ("$(ENABLE_GDBSTUB)", "1")
MINI_GDBSTUB_OUT = $(abspath $(OUT)/mini-gdbstub)
GDBSTUB_COMM = 127.0.0.1:1234
LIB_GDBSTUB += $(MINI_GDBSTUB_OUT)/libgdbstub.a
gdbstub-test: $(BIN)
	./tests/gdbstub-test/main.sh

$(LIB_GDBSTUB):
	git submodule update --init mini-gdbstub
	$(MAKE) -C mini-gdbstub O=$(MINI_GDBSTUB_OUT)
$(OUT)/emulate.o: $(LIB_GDBSTUB)
OBJS_EXT += gdbstub.o
CFLAGS += -D ENABLE_GDBSTUB -D'GDBSTUB_COMM="$(GDBSTUB_COMM)"'
LDFLAGS += $(LIB_GDBSTUB)
endif

# Clear the .DEFAULT_GOAL special variable, so that the following turns
# to the first target after .DEFAULT_GOAL is not set.
.DEFAULT_GOAL :=

all: $(BIN)

OBJS := \
	map.o \
	emulate.o \
	io.o \
	elf.o \
	main.o \
	syscall.o \
	$(OBJS_EXT)

OBJS := $(addprefix $(OUT)/, $(OBJS))
deps := $(OBJS:%.o=%.o.d)

$(OUT)/%.o: %.c
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) -c -MMD -MF $@.d $<

$(BIN): $(OBJS)
	$(VECHO) "  LD\t$@\n"
	$(Q)$(CC) -o $@ $^ $(LDFLAGS)

# RISC-V Architecture Tests
include mk/riscv-arch-test.mk

CHECK_ELF_FILES := \
	hello \
	puzzle \
	pi

EXPECTED_hello = Hello World!
EXPECTED_puzzle = success in 2005 trials
EXPECTED_pi = 3.141592653589793238462643383279502884197169399375105820974944592307816406286208998628034825342117067982148086

check: $(BIN)
	$(Q)$(foreach e,$(CHECK_ELF_FILES),\
	    $(PRINTF) "Testing $(e) ... "; \
	    if [ "$(shell $(BIN) $(OUT)/$(e).elf)" = "$(strip $(EXPECTED_$(e))) inferior exit code 0" ]; then \
	    $(call notice, [OK]); \
	    else \
	    $(PRINTF) "Fail. Re-run '$(e)' later.\n"; \
	    fi; \
	)

include mk/external.mk

# Non-trivial demonstration programs
ifeq ("$(ENABLE_SDL)", "1")
doom: $(BIN) $(DOOM_DATA)
	(cd $(OUT); ../$(BIN) doom.elf)
ifeq ("$(ENABLE_RV32F)", "1")
quake: $(BIN) $(QUAKE_DATA)
	(cd $(OUT); ../$(BIN) quake.elf)
endif
endif

clean:
	$(RM) $(BIN) $(OBJS) $(deps)
distclean: clean
	-$(RM) $(DOOM_DATA) $(QUAKE_DATA)
	$(RM) -r $(OUT)/id1
	$(RM) *.zip

-include $(deps)
