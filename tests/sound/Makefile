OUTPUT := sound.elf

CROSS_COMPILE = riscv32-unknown-elf-

CC := $(CROSS_COMPILE)gcc

CFLAGS += -Wall -Wno-format -Wno-unused
CFLAGS += -std=gnu99
CFLAGS += -march=rv32i -mabi=ilp32

all: sound.c
	$(CC) $(CFLAGS) -o $(OUTPUT) $^

clean:
	rm -f $(OUTPUT) $(OBJS)

.PHONY: all
