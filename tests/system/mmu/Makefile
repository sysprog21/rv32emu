PREFIX ?= riscv-none-elf-
ARCH = -march=rv32izicsr
LINKER_SCRIPT = linker.ld

DEBUG_CFLAGS = -g
CFLAGS = -c -march=rv32i_zicsr
LDFLAGS = -T
EXEC = vm.elf

CC = $(PREFIX)gcc
AS = $(PREFIX)as
LD = $(PREFIX)ld
OBJDUMP = $(PREFIX)objdump

# Locate libgcc.a dynamically, as the itoa function uses division and modulo operators,
# which rely on __udivsi3 and __umodsi3, both provided by libgcc
LIBGCC = $(shell $(CC) -print-libgcc-file-name)
LIBGCC_PATH = $(shell dirname $(LIBGCC))

deps = main.o setup.o vm_setup.o

all:
	$(CC) $(DEBUG_CLAGS) $(CFLAGS) main.c
	$(CC) $(DEBUG_CLAGS) $(CFLAGS) vm_setup.c
	$(AS) $(DEBUG_CLAGS) $(ARCH) setup.S -o setup.o
	$(LD) $(LDFLAGS) $(LINKER_SCRIPT) -o $(EXEC) $(deps) -L$(LIBGCC_PATH) -lgcc

dump:
	$(OBJDUMP) -DS $(EXEC) | less

# __udivsi3 and __umodsi3 can be found
nm:
	$(NM) $(LIBGCC) | less

clean:
	rm $(EXEC) $(deps)
