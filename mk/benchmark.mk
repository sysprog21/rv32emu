USE_PREBUILT ?= 1

CC ?= gcc
CROSS_CC ?= riscv-none-elf-gcc

BINDIR := $(abspath $(OUT))/bin

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
    $(ieee754) \
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
    $(smolnes) \
    spirograph \
    $(ticks)

ifeq ($(USE_PREBUILT),1)
  LATEST_RELEASE := $(shell wget -qO - https://api.github.com/repos/sysprog21/rv32emu-prebuilt/releases/latest | grep -Po '(?<="tag_name": ").+(?=",)')
endif

.PHONY: build-testbenches benchmark

# TODO: generate results automatically
benchmark: build-testbenches

build-testbenches:
ifeq ($(USE_PREBUILT),1)
	@echo "Fetching prebuilt executables in \"rv32emu-prebuilt\"..."
	@wget -O - https://github.com/sysprog21/rv32emu-prebuilt/releases/download/$(LATEST_RELEASE)/rv32emu-prebuilt.tar.gz | tar zx -C build
else
	@$(foreach tb,$(TEST_SUITES), \
	    git submodule update --init ./tests/$(tb) &&) true
	@$(foreach tb,$(TEST_SUITES), \
	    CC=$(CC) CROSS_CC=$(CROSS_CC) BINDIR=$(BINDIR) \
		$(MAKE) -C ./tests/$(tb) all &&) true
	@$(foreach tb,$(TESTBENCHES), \
	    $(CC) -m32 -O2 -Wno-unused-result -o $(BINDIR)/x86_64/$(tb) tests/$(tb).c -lm &&) true
	@$(foreach tb,$(TESTBENCHES), \
	    $(CROSS_CC) -march=rv32im -mabi=ilp32 -O2 -Wno-unused-result -Wno-implicit-function-declaration \
		    -o $(BINDIR)/riscv32/$(tb) tests/$(tb).c -lm -lsemihost &&) true
endif
