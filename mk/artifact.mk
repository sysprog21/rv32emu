ENABLE_PREBUILT ?= 1

CC ?= gcc
CROSS_COMPILE ?= riscv-none-elf-

BINDIR := $(abspath $(OUT))

TEST_SUITES += \
	ansibench \
	rv8-bench

# "ieee754" needs F extension
# "smolnes", "ticks" have inline assembly and only work in riscv
TESTBENCHES += \
	captcha \
	donut \
	fcalc \
	hamilton \
	jit \
	lena \
	line \
	maj2random \
	mandelbrot \
	nqueens \
	nyancat \
	pi \
	puzzle \
	qrcode \
	richards \
	rvsim \
	spirograph \
	uaes

SHELL_HACK := $(shell mkdir -p $(BINDIR)/linux-x86-softfp $(BINDIR)/riscv32)

ifeq ($(call has, PREBUILT), 1)
  LATEST_RELEASE := $(shell wget -q https://api.github.com/repos/sysprog21/rv32emu-prebuilt/releases/latest -O- | grep -Po '(?<="tag_name": ").+(?=",)')
else
  CFLAGS := -m32 -mno-sse -mno-sse2 -msoft-float -O2 -L$(BINDIR)
  LDFLAGS := -lsoft-fp -lm

  CFLAGS_CROSS := -march=rv32im -mabi=ilp32 -O2
  LDFLAGS_CROSS := -lm -lsemihost
endif

.PHONY: artifact

artifact:
ifeq ($(call has, PREBUILT), 1)
	$(Q)$(PRINTF) "Fetching prebuilt executables from \"rv32emu-prebuilt\" ...\n"
	$(Q)wget -q --show-progress https://github.com/sysprog21/rv32emu-prebuilt/releases/download/$(LATEST_RELEASE)/rv32emu-prebuilt.tar.gz -O- | tar -C build --strip-components=1 -xz
else
	git submodule update --init ./src/ieeelib $(addprefix ./tests/,$(foreach tb,$(TEST_SUITES),$(tb)))
	$(Q)$(MAKE) -C ./src/ieeelib CC=$(CC) CFLAGS="$(CFLAGS)" BINDIR=$(BINDIR)
	$(Q)for tb in $(TEST_SUITES); do \
	    CC=$(CC) CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" BINDIR=$(BINDIR)/linux-x86-softfp $(MAKE) -C ./tests/$$tb; \
	done
	$(Q)for tb in $(TEST_SUITES); do \
	    CC=$(CROSS_COMPILE)gcc CFLAGS="$(CFLAGS_CROSS)" LDFLAGS="$(LDFLAGS_CROSS)" BINDIR=$(BINDIR)/riscv32 $(MAKE) -C ./tests/$$tb; \
	done
	$(Q)$(PRINTF) "Building standalone testbenches ...\n"
	$(Q)for tb in $(TESTBENCHES); do \
	    $(CC) $(CFLAGS) -Wno-unused-result -o $(BINDIR)/linux-x86-softfp/$$tb ./tests/$$tb.c $(LDFLAGS); \
	done
	$(Q)for tb in $(TESTBENCHES); do \
	    $(CROSS_COMPILE)gcc $(CFLAGS_CROSS) -o $(BINDIR)/riscv32/$$tb ./tests/$$tb.c $(LDFLAGS_CROSS); \
	done
endif
