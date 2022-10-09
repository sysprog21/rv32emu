.PHONY: clean

include ../../mk/toolchain.mk

ASFLAGS = -march=rv32i -mabi=ilp32
LDFLAGS = --oformat=elf32-littleriscv

%.o: %.S
	$(CROSS_COMPILE)as -R $(ASFLAGS) -o $@ $<

all: hello.elf

hello.elf: hello.o
	 $(CROSS_COMPILE)ld -o $@ -T hello.ld $(LDFLAGS) $<

clean:
	$(RM) hello.elf hello.o
