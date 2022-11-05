.PHONY: clean

include ../../mk/toolchain.mk

CFLAGS = -march=rv32i -mabi=ilp32 -O2 -Wall

OBJS = \
    context.o \
    switch_context.o \
    coro.o
BIN = coro.elf

%.o: %.S
	$(CROSS_COMPILE)gcc $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CROSS_COMPILE)gcc $(CFLAGS) -c -o $@ $<

all: $(BIN)

$(BIN): $(OBJS)
	 $(CROSS_COMPILE)gcc -o $@ $^

clean:
	$(RM) $(BIN) $(OBJS)
