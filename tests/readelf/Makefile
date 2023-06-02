OUTPUT := readelf.elf

SRCS := elf.c  file.c  file_header.c  readelf.c  section.c  string_table.c
OBJS := $(SRCS:%.c=%.o)

CROSS_COMPILE = riscv32-unknown-elf-

CC := $(CROSS_COMPILE)gcc

CFLAGS += -Wall -Wno-format -Wno-unused
CFLAGS += -std=gnu99
CFLAGS += -Os
CFLAGS += -I.
CFLAGS += -march=rv32i -mabi=ilp32

%.o: %.c
	$(CC) $(CFLAGS) -c  $< -o $@

all: $(OBJS)
	$(CC) $(CFLAGS) -o $(OUTPUT) $^

clean:
	rm -f $(OUTPUT) $(OBJS)

.PHONY: all
