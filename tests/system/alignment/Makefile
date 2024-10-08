PREFIX ?= riscv-none-elf-
ARCH = -march=rv32izicsr
LINKER_SCRIPT = linker.ld

DEBUG_CFLAGS = -g
CFLAGS = -c -march=rv32i_zicsr
LDFLAGS = -T
EXEC = misalign.elf

CC = $(PREFIX)gcc
AS = $(PREFIX)as
LD = $(PREFIX)ld
OBJDUMP = $(PREFIX)objdump

deps = main.o misalign.o

all:
	$(CC) $(DEBUG_CLAGS) $(CFLAGS) main.c
	$(AS) $(DEBUG_CLAGS) $(ARCH) misalign.S -o misalign.o
	$(LD) $(LDFLAGS) $(LINKER_SCRIPT) -o $(EXEC) $(deps)

dump:
	$(OBJDUMP) -Ds $(EXEC) | less

clean:
	rm $(EXEC) $(deps)
