.PHONY: clean

include ../../mk/toolchain.mk

ASFLAGS = -march=rv32i -mabi=ilp32

all: perfcounter.elf

perfcounter.elf: perfcounter.S
	$(CROSSCOMPILE)gcc $(ASFLAGS) -o $@ $<

clean:
	$(RM) perfcounter.elf
