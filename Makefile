UNAME_S := $(shell uname -s)
ifeq ("$(origin CC)", "default")
    ifeq ($(UNAME_S),Darwin)
	CC := clang
    else
	CC := gcc
    endif
endif

CFLAGS = -std=gnu99 -Wall -Wextra
CFLAGS += -include common.h

# Base configurations for RISC-V extensions
CFLAGS += -D ENABLE_RV32M
CFLAGS += -D ENABLE_Zicsr
CFLAGS += -D ENABLE_Zifencei
CFLAGS += -D ENABLE_RV32A
CFLAGS += -D ENABLE_RV32C

# Set the default stack pointer
CFLAGS += -D DEFAULT_STACK_ADDR=0xFFFFF000

# Experimental SDL oriented system calls
CFLAGS += -D ENABLE_SDL
CFLAGS += `sdl2-config --cflags`
LDFLAGS += `sdl2-config --libs`

# Whether to enable computed goto in riscv.c
ENABLE_COMPUTED_GOTO ?= 1
ifeq ("$(ENABLE_COMPUTED_GOTO)", "1")
ifneq ($(filter $(CC), gcc clang),)
riscv.o: CFLAGS += -D ENABLE_COMPUTED_GOTO
	ifeq ("$(CC)", "gcc")
riscv.o: CFLAGS += -fno-gcse -fno-crossjumping 
	endif
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
	syscall_sdl.o

deps := $(OBJS:%.o=%.o.d)

%.o: %.c
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) -c -MMD -MF $@.d $<

$(BIN): $(OBJS)
	$(VECHO) "  LD\t$@\n"
	$(Q)$(CC) -o $@ $^ $(LDFLAGS)

SHA1SUM = sha1sum
SHA1SUM := $(shell which $(SHA1SUM))
ifndef SHA1SUM
    SHA1SUM = shasum
    SHA1SUM := $(shell which $(SHA1SUM))
    ifndef SHA1SUM
        SHA1SUM := @echo
    endif
endif

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

check: $(BIN)
	(cd $(OUT); ../$(BIN) hello.elf)
	(cd $(OUT); ../$(BIN) puzzle.elf)

# Validate GNU Toolchain for RISC-V
CROSS_COMPILE ?= riscv32-unknown-elf-
RV32_CC = $(CROSS_COMPILE)gcc
RV32_CC := $(shell which $(RV32_CC))
ifndef RV32_CC
  # Try Debian/Ubuntu package
  CROSS_COMPILE = riscv-none-embed-
  RV32_CC = $(CROSS_COMPILE)gcc
  RV32_CC := $(shell which $(RV32_CC))
  ifndef RV32_CC
  $(warning "no $(CROSS_COMPILE)gcc found. Check GNU Toolchain for RISC-V installation.")
  CROSS_COMPILE :=
  endif
endif

ARCH_TEST_DIR ?= tests/riscv-arch-test
ARCH_TEST_BUILD := $(ARCH_TEST_DIR)/Makefile
export RISCV_TARGET := tests/arch-test-target
export RISCV_PREFIX ?= $(CROSS_COMPILE)
export TARGETDIR := $(shell pwd)
export XLEN := 32
export JOBS ?= -j
export WORK := $(TARGETDIR)/build/arch-test

$(ARCH_TEST_BUILD):
	git submodule update --init

arch-test: $(BIN) $(ARCH_TEST_BUILD)
	$(Q)$(MAKE) --quiet -C $(ARCH_TEST_DIR) clean
	$(Q)$(MAKE) --quiet -C $(ARCH_TEST_DIR)

demo: $(BIN) $(OUT)/DOOM1.WAD
	(cd $(OUT); ../$(BIN) doom.elf)

quake: $(BIN) $(OUT)/id1/pak0.pak
	(cd $(OUT); ../$(BIN) quake.elf)

clean:
	$(RM) $(BIN) $(OBJS) $(deps)
distclean: clean
	$(RM) $(OUT)/DOOM1.WAD $(OUT)/DOOM1.WAD.sha1
	$(RM) -r $(OUT)/id1
	$(RM) *.zip

-include $(deps)
