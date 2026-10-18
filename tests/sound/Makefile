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
	rm -rf $(HOST_OUT)

.PHONY: all

# Exercise the deferred loader with the same calls as a directly linked mixer.
# This host check uses SDL's dummy device and requires Linux shared libraries.
HOSTCC ?= cc
HOST_OUT ?= ../../build/sound
MIXER_CFLAGS = $(shell pkg-config --cflags SDL2_mixer)
MIXER_LIBS = $(shell pkg-config --libs SDL2_mixer sdl2)

$(HOST_OUT):
	mkdir -p $@

$(HOST_OUT)/test-mixer-direct: test-mixer.c | $(HOST_OUT)
	$(HOSTCC) -std=gnu11 -Wall -Wextra -Werror $(MIXER_CFLAGS) $< \
	    -o $@ $(MIXER_LIBS) -pthread

$(HOST_OUT)/test-mixer-dynamic: test-mixer.c ../../src/sdl_mixer_dynamic.h | $(HOST_OUT)
	$(HOSTCC) -std=gnu11 -Wall -Wextra -Werror $(MIXER_CFLAGS) \
	    -DTEST_DYNAMIC_MIXER $< -o $@ \
	    $(filter-out -lSDL2_mixer,$(MIXER_LIBS)) -ldl -pthread

.PHONY: check-mixer
check-mixer: $(HOST_OUT)/test-mixer-direct $(HOST_OUT)/test-mixer-dynamic
	SDL_AUDIODRIVER=dummy $(HOST_OUT)/test-mixer-direct > $(HOST_OUT)/direct.out
	SDL_AUDIODRIVER=dummy $(HOST_OUT)/test-mixer-dynamic > $(HOST_OUT)/dynamic.out
	cmp $(HOST_OUT)/direct.out $(HOST_OUT)/dynamic.out
