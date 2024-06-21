PREFIX ?= riscv32-unknown-elf-
ARCH = -march=rv32izicsr
LINKER_SCRIPT = linker.ld

DEBUG_CFLAGS = -g
CFLAGS = -c
LDFLAGS = -T
EXEC = vm.elf

CC = $(PREFIX)gcc
AS = $(PREFIX)as
LD = $(PREFIX)ld
OBJDUMP = $(PREFIX)objdump

deps = main.o setup.o vm_setup.o

all:
	$(CC) $(DEBUG_CLAGS) $(CFLAGS) main.c
	$(CC) $(DEBUG_CLAGS) $(CFLAGS) vm_setup.c
	$(AS) $(DEBUG_CLAGS) $(ARCH) setup.S -o setup.o
	$(LD) $(LDFLAGS) $(LINKER_SCRIPT) -o $(EXEC) $(deps)

dump:
	$(OBJDUMP) -Ds $(EXEC) | less

clean:
	rm $(EXEC) $(deps)
