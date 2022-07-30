include mk/common.mk
include mk/toolchain.mk

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
syscall_sdl.o: CFLAGS += $(shell sdl2-config --cflags)
LDFLAGS += $(shell sdl2-config --libs)
endif

# Whether to enable computed goto
ENABLE_COMPUTED_GOTO ?= 1
ifeq ("$(ENABLE_COMPUTED_GOTO)", "1")
ifeq ("$(CC_IS_CLANG)$(CC_IS_GCC)",)
$(error "Computed goto is only supported in clang and gcc.")
endif
emulate.o: CFLAGS += -D ENABLE_COMPUTED_GOTO
ifeq ("$(CC_IS_GCC)", "1")
emulate.o: CFLAGS += -fno-gcse -fno-crossjumping
endif
endif

OUT ?= build
BIN := $(OUT)/rv32emu

all: $(BIN)

OBJS := \
	map.o \
	emulate.o \
	io.o \
	elf.o \
	main.o \
	syscall.o \
	$(OBJS_EXT)

deps := $(OBJS:%.o=%.o.d)

%.o: %.c
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
check: $(BIN)
	$(Q)for e in $(CHECK_ELF_FILES); do \
	    (cd $(OUT); ../$(BIN) $$e.elf) && $(call pass,$$e); \
	done

include mk/external.mk

# Non-trivial demonstration programs
ifeq ("$(ENABLE_SDL)", "1")
demo: $(BIN) $(DOOM_DATA)
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
