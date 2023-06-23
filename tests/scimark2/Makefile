.PHONY: clean

include ../../mk/toolchain.mk

CFLAGS = -march=rv32imf -mabi=ilp32
CFLAGS += -Wall -Ofast -flto
LDFLAGS = -lm -flto

.SUFFIXES: .c .o

%.o: %.c
	$(CROSS_COMPILE)gcc $(CFLAGS) -c -o $@ $<

BIN = scimark2.elf

all: $(BIN)

OBJS = \
	FFT.o kernel.o Stopwatch.o Random.o SOR.o SparseCompRow.o \
	array.o MonteCarlo.o LU.o \
	scimark2.o

$(BIN): $(OBJS)
	$(CROSS_COMPILE)gcc $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJS) $(BIN)
