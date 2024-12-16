ENABLE_PREBUILT ?= 1

CC ?= gcc
CROSS_COMPILE ?= riscv-none-elf-

BIN_DIR := $(abspath $(OUT))

TEST_SUITES += \
	ansibench \
	rv8-bench

# "ieee754" needs F extension
# "smolnes", "ticks" have inline assembly and only work in riscv
TEST_BENCHES += \
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

SCIMARK2_URL := https://math.nist.gov/scimark2/scimark2_1c.zip
SCIMARK2_SHA1 := de278c5b8cef84ab6dda41855052c7bfef919e36

SHELL_HACK := $(shell mkdir -p $(BIN_DIR)/linux-x86-softfp $(BIN_DIR)/riscv32 $(BIN_DIR)/linux-image)

ifeq ($(call has, PREBUILT), 1)
ifeq ($(call has, SYSTEM), 1)
  LATEST_RELEASE := $(shell wget -q https://api.github.com/repos/sysprog21/rv32emu-prebuilt/releases -O- | grep '"tag_name"' | grep "Linux-Image" | head -n 1 | sed -E 's/.*"tag_name": "([^"]+)".*/\1/')
else
  LATEST_RELEASE := $(shell wget -q https://api.github.com/repos/sysprog21/rv32emu-prebuilt/releases -O- | grep '"tag_name"' | grep "ELF" | head -n 1 | sed -E 's/.*"tag_name": "([^"]+)".*/\1/')
endif
else
  # Since rv32emu only supports the dynamic binary translation of integer instruction in tiered compilation currently,
  # we disable the hardware floating-point and the related SIMD operation of x86.
  CFLAGS := -m32 -mno-sse -mno-sse2 -msoft-float -O2 -Wno-unused-result -L$(BIN_DIR)
  LDFLAGS := -lsoft-fp -lm

  CFLAGS_CROSS := -march=rv32im -mabi=ilp32 -O2 -Wno-implicit-function-declaration
  LDFLAGS_CROSS := -lm -lsemihost
endif

.PHONY: artifact fetch-checksum scimark2 ieeelib

artifact: fetch-checksum ieeelib scimark2
ifeq ($(call has, PREBUILT), 1)
	$(Q)$(PRINTF) "Checking SHA-1 of prebuilt binaries ... "
	$(Q)$(eval RES := 0)

ifeq ($(call has, SYSTEM), 1)
	$(Q)$(eval PREBUILT_LINUX_IMAGE_FILENAME := $(shell cat $(BIN_DIR)/sha1sum-linux-image | awk '{  print $$2 };'))

	$(Q)$(eval $(foreach FILE,$(PREBUILT_LINUX_IMAGE_FILENAME), \
	    $(call verify,$(shell grep -w $(FILE) $(BIN_DIR)/sha1sum-linux-image | awk '{ print $$1 };'),$(BIN_DIR)/linux-image/$(FILE),RES) \
	))

	$(Q)$(eval RV32EMU_PREBUILT_TARBALL := rv32emu-linux-image-prebuilt.tar.gz)
else
	$(Q)$(eval PREBUILT_X86_FILENAME := $(shell cat $(BIN_DIR)/sha1sum-linux-x86-softfp | awk '{  print $$2 };'))
	$(Q)$(eval PREBUILT_RV32_FILENAME := $(shell cat $(BIN_DIR)/sha1sum-riscv32 | awk '{ print $$2 };'))

	$(Q)$(eval $(foreach FILE,$(PREBUILT_X86_FILENAME), \
	    $(call verify,$(shell grep -w $(FILE) $(BIN_DIR)/sha1sum-linux-x86-softfp | awk '{ print $$1 };'),$(BIN_DIR)/linux-x86-softfp/$(FILE),RES) \
	))
	$(Q)$(eval $(foreach FILE,$(PREBUILT_RV32_FILENAME), \
	    $(call verify,$(shell grep -w $(FILE) $(BIN_DIR)/sha1sum-riscv32 | awk '{ print $$1 };'),$(BIN_DIR)/riscv32/$(FILE),RES) \
	))

	$(Q)$(eval RV32EMU_PREBUILT_TARBALL := rv32emu-prebuilt.tar.gz)
endif

	$(Q)if [ "$(RES)" = "1" ]; then \
	    $(PRINTF) "\n$(YELLOW)SHA-1 verification fails! Re-fetching prebuilt binaries from \"rv32emu-prebuilt\" ...\n$(NO_COLOR)"; \
	    wget -q --show-progress https://github.com/sysprog21/rv32emu-prebuilt/releases/download/$(LATEST_RELEASE)/$(RV32EMU_PREBUILT_TARBALL) -O- | tar -C build --strip-components=1 -xz; \
	else \
	    $(call notice, [OK]); \
	fi
else
ifeq ($(call has, SYSTEM), 1)
	$(Q)(cd $(BIN_DIR) && $(SHA1SUM) Image >> sha1sum-linux-image)
	$(Q)(cd $(BIN_DIR) && $(SHA1SUM) rootfs.cpio >> sha1sum-linux-image)
else
	git submodule update --init $(addprefix ./tests/,$(foreach tb,$(TEST_SUITES),$(tb)))
	$(Q)for tb in $(TEST_SUITES); do \
	    CC=$(CC) CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" BINDIR=$(BIN_DIR)/linux-x86-softfp $(MAKE) -C ./tests/$$tb; \
	done
	$(Q)for tb in $(TEST_SUITES); do \
	    CC=$(CROSS_COMPILE)gcc CFLAGS="$(CFLAGS_CROSS)" LDFLAGS="$(LDFLAGS_CROSS)" BINDIR=$(BIN_DIR)/riscv32 $(MAKE) -C ./tests/$$tb; \
	done

	$(Q)$(PRINTF) "Building standalone testbenches ...\n"
	$(Q)for tb in $(TEST_BENCHES); do \
	    $(CC) $(CFLAGS) -o $(BIN_DIR)/linux-x86-softfp/$$tb ./tests/$$tb.c $(LDFLAGS); \
	done
	$(Q)for tb in $(TEST_BENCHES); do \
	    $(CROSS_COMPILE)gcc $(CFLAGS_CROSS) -o $(BIN_DIR)/riscv32/$$tb ./tests/$$tb.c $(LDFLAGS_CROSS); \
	done

	git submodule update --init ./tests/doom ./tests/quake
	$(Q)$(PRINTF) "Building doom ...\n"
	$(Q)$(MAKE) -C ./tests/doom/src/riscv CROSS=$(CROSS_COMPILE)
	$(Q)cp ./tests/doom/src/riscv/doom-riscv.elf $(BIN_DIR)/riscv32/doom
	$(Q)$(PRINTF) "Building quake ...\n"
	$(Q)cd ./tests/quake && mkdir -p build && cd build && \
	    cmake -DCMAKE_TOOLCHAIN_FILE=../port/boards/rv32emu/toolchain.cmake \
	          -DCROSS_COMPILE=$(CROSS_COMPILE) \
	          -DCMAKE_BUILD_TYPE=RELEASE -DBOARD_NAME=rv32emu .. && \
	    make
	$(Q)cp ./tests/quake/build/port/boards/rv32emu/quake $(BIN_DIR)/riscv32/quake

	$(Q)(cd $(BIN_DIR)/linux-x86-softfp; for fd in *; do $(SHA1SUM) "$$fd"; done) >> $(BIN_DIR)/sha1sum-linux-x86-softfp
	$(Q)(cd $(BIN_DIR)/riscv32; for fd in *; do $(SHA1SUM) "$$fd"; done) >> $(BIN_DIR)/sha1sum-riscv32
endif
endif

fetch-checksum:
ifeq ($(call has, PREBUILT), 1)
	$(Q)$(PRINTF) "Fetching SHA-1 of prebuilt binaries ... "
ifeq ($(call has, SYSTEM), 1)
	$(Q)wget -q -O $(BIN_DIR)/sha1sum-linux-image https://github.com/sysprog21/rv32emu-prebuilt/releases/download/$(LATEST_RELEASE)/sha1sum-linux-image
	$(Q)$(call notice, [OK])
else
	$(Q)wget -q -O $(BIN_DIR)/sha1sum-linux-x86-softfp https://github.com/sysprog21/rv32emu-prebuilt/releases/download/$(LATEST_RELEASE)/sha1sum-linux-x86-softfp
	$(Q)wget -q -O $(BIN_DIR)/sha1sum-riscv32 https://github.com/sysprog21/rv32emu-prebuilt/releases/download/$(LATEST_RELEASE)/sha1sum-riscv32
	$(Q)$(call notice, [OK])
endif
endif

scimark2:
ifeq ($(call has, PREBUILT), 0)
ifeq ($(call has, SYSTEM), 0)
	$(Q)$(call prologue,"scimark2")
	$(Q)$(call download,$(SCIMARK2_URL))
	$(Q)$(call verify,$(SCIMARK2_SHA1),$(notdir $(SCIMARK2_URL)))
	$(Q)$(call extract,"./tests/scimark2",$(notdir $(SCIMARK2_URL)))
	$(Q)$(call epilogue,$(notdir $(SCIMARK2_URL)),$(SHA1_FILE1),$(SHA1_FILE2))
	$(Q)$(PRINTF) "Building scimark2 ...\n"
	$(Q)$(MAKE) -C ./tests/scimark2 CC=$(CC) CFLAGS="-m32 -O2"
	$(Q)cp ./tests/scimark2/scimark2 $(BIN_DIR)/linux-x86-softfp/scimark2
	$(Q)$(MAKE) -C ./tests/scimark2 clean && $(RM) ./tests/scimark2/scimark2.o
	$(Q)$(MAKE) -C ./tests/scimark2 CC=$(CROSS_COMPILE)gcc CFLAGS="-march=rv32imf -mabi=ilp32 -O2"
	$(Q)cp ./tests/scimark2/scimark2 $(BIN_DIR)/riscv32/scimark2
endif
endif

ieeelib:
ifeq ($(call has, PREBUILT), 0)
	git submodule update --init ./src/ieeelib
	$(Q)$(MAKE) -C ./src/ieeelib CC=$(CC) CFLAGS="$(CFLAGS)" BINDIR=$(BIN_DIR)
endif
