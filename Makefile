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

# Whether to enable computed goto in riscv.c
ENABLE_COMPUTED_GOTO ?= 1
ifeq ("$(ENABLE_COMPUTED_GOTO)", "1")
ifeq ("$(CC_IS_CLANG)$(CC_IS_GCC)",)
$(error "Computed goto is only supported in clang and gcc.")
endif
riscv.o: CFLAGS += -D ENABLE_COMPUTED_GOTO
ifeq ("$(CC_IS_GCC)", "1")
riscv.o: CFLAGS += -fno-gcse -fno-crossjumping 
endif
endif

# Control the build verbosity
ifeq ("$(VERBOSE)","1")
    Q :=
    VECHO = @true
else
    Q := @
    VECHO = @printf
endif

OUT ?= build
BIN := $(OUT)/rv32emu

all: $(BIN)

OBJS := \
	map.o \
	riscv.o \
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

# https://tipsmake.com/how-to-run-doom-on-raspberry-pi-without-emulator
DOOM_WAD_DL = http://www.doomworld.com/3ddownloads/ports/shareware_doom_iwad.zip
$(OUT)/DOOM1.WAD:
	$(VECHO) "  Downloading $@ ...\n"
	wget $(DOOM_WAD_DL)
	unzip -d $(OUT) shareware_doom_iwad.zip
	echo "5b2e249b9c5133ec987b3ea77596381dc0d6bc1d  $@" > $@.sha1
	$(SHA1SUM) -c $@.sha1
	$(RM) shareware_doom_iwad.zip

Quake_shareware = https://www.libsdl.org/projects/quake/data/quakesw-1.0.6.zip
$(OUT)/id1/pak0.pak:
	$(VECHO) " Downloading $@ ...\n"
	wget $(Quake_shareware)
	unzip -d $(OUT) quakesw-1.0.6.zip
	echo "36b42dc7b6313fd9cabc0be8b9e9864840929735  $@" > $@.sha1
	$(SHA1SUM) -c $@.sha1
	$(RM) quakesw-1.0.6.zip

# Non-trivial demonstration programs
ifeq ("$(ENABLE_SDL)", "1")
demo: $(BIN) $(OUT)/DOOM1.WAD
	(cd $(OUT); ../$(BIN) doom.elf)
quake: $(BIN) $(OUT)/id1/pak0.pak
	(cd $(OUT); ../$(BIN) quake.elf)
endif

clean:
	$(RM) $(BIN) $(OBJS) $(deps)
distclean: clean
	$(RM) $(OUT)/DOOM1.WAD $(OUT)/DOOM1.WAD.sha1
	$(RM) -r $(OUT)/id1
	$(RM) *.zip

-include $(deps)
