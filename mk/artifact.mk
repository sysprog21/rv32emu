USE_PREBUILT ?= 1

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
  LATEST_RELEASE := $(shell wget -q https://api.github.com/repos/sysprog21/rv32emu-prebuilt/releases/latest -O- | grep -Po '(?<="tag_name": ").+(?=",)')
endif

.PHONY: build-artifact

build-artifact:
ifeq ($(USE_PREBUILT),1)
	@echo "Fetching prebuilt executables in \"rv32emu-prebuilt\"..."
	@wget -q https://github.com/sysprog21/rv32emu-prebuilt/releases/download/$(LATEST_RELEASE)/rv32emu-prebuilt.tar.gz -O- | tar -C build -xz
else
	@$(foreach tb,$(TEST_SUITES), \
	    git submodule update --init ./tests/$(tb) &&) true
	@$(foreach tb,$(TEST_SUITES), \
		$(MAKE) -C ./tests/$(tb) all BINDIR=$(BINDIR) &&) true
	@$(foreach tb,$(TESTBENCHES), \
	    $(CC) -m32 -O2 -Wno-unused-result -o $(BINDIR)/linux-x64/$(tb) tests/$(tb).c -lm &&) true
	@$(foreach tb,$(TESTBENCHES), \
	    $(CROSS_COMPILE)gcc -march=rv32im -mabi=ilp32 -O2 -Wno-unused-result -Wno-implicit-function-declaration \
		    -o $(BINDIR)/riscv32/$(tb) tests/$(tb).c -lm -lsemihost &&) true
endif
