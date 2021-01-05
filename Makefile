CFLAGS = -std=gnu99 -O2 -Wall -Wextra
CFLAGS += -include common.h

# Base configurations for RISC-V extensions
CFLAGS += -D ENABLE_RV32M
CFLAGS += -D ENABLE_Zicsr
CFLAGS += -D ENABLE_Zifencei
CFLAGS += -D ENABLE_RV32A
CFLAGS += -D DEFAULT_STACK_ADDR=0xFFFFF000

# Experimental SDL oriented system calls
CFLAGS += -D ENABLE_SDL
CFLAGS += `sdl2-config --cflags`
LDFLAGS += `sdl2-config --libs`

# Control the build verbosity
ifeq ("$(VERBOSE)","1")
    Q :=
    VECHO = @true
else
    Q := @
    VECHO = @printf
endif

OUT ?= build
BIN = $(OUT)/rv32emu

all: $(BIN)

OBJS = \
	c_map.o \
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

# https://tipsmake.com/how-to-run-doom-on-raspberry-pi-without-emulator
DOOM_WAD_DL = http://www.doomworld.com/3ddownloads/ports/shareware_doom_iwad.zip
$(OUT)/DOOM1.WAD:
	$(VECHO) "  Downloading $@ ...\n"
	wget $(DOOM_WAD_DL)
	unzip -d $(OUT) shareware_doom_iwad.zip
	echo "5b2e249b9c5133ec987b3ea77596381dc0d6bc1d  $@" > $@.sha1
	shasum -a 1 -c $@.sha1
	$(RM) shareware_doom_iwad.zip

check: $(BIN)
	(cd $(OUT); ../$(BIN) hello.elf)
	(cd $(OUT); ../$(BIN) puzzle.elf)

# variables for compliance
COMPLIANCE_DIR ?= ./riscv-compliance
export RISCV_PREFIX ?= riscv32-unknown-elf-
export RISCV_TARGET = test-rv32emu
export TARGETDIR = $(shell pwd)
export XLEN = 32
export JOBS ?= -j
export WORK = $(TARGETDIR)/build/compliance

compliance: $(BIN)
	$(Q)$(MAKE) --quiet -C $(COMPLIANCE_DIR) clean;
	$(Q)$(MAKE) --quiet -C $(COMPLIANCE_DIR);
		
demo: $(BIN) $(OUT)/DOOM1.WAD
	(cd $(OUT); ../$(BIN) doom.elf)

clean:
	$(RM) $(BIN) $(OBJS) $(deps)
distclean: clean
	$(RM) $(OUT)/DOOM1.WAD $(OUT)/DOOM1.WAD.sha1

-include $(deps)
