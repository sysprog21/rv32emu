.PHONY: clean

include ../../mk/toolchain.mk

CFLAGS = -march=rv32i -mabi=ilp32 -O2

OBJS = chacha20.o chacha20_asm.o
BIN = chacha20.elf

%.o: %.S
	$(CROSS_COMPILE)gcc $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CROSS_COMPILE)gcc $(CFLAGS) -c -o $@ $<

all: $(BIN)

$(BIN): $(OBJS)
	 $(CROSS_COMPILE)gcc -o $@ $^

clean:
	$(RM) $(BIN) $(OBJS)
