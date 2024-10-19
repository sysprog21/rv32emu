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

SHELL_HACK := $(shell mkdir -p $(BIN_DIR)/linux-x86-softfp $(BIN_DIR)/riscv32)

LATEST_RELEASE := $(shell wget -q https://api.github.com/repos/sysprog21/rv32emu-prebuilt/releases/latest -O- | grep '"tag_name"' | sed -E 's/.*"tag_name": "([^"]+)".*/\1/')

.PHONY: artifact fetch-checksum

artifact: fetch-checksum
	$(Q)$(PRINTF) "Checking SHA-1 of prebuilt binaries ... "

	$(Q)$(eval PREBUILT_X86_FILENAME := $(shell cat $(BIN_DIR)/sha1sum-linux-x86-softfp | awk '{  print $$2 };'))
	$(Q)$(eval PREBUILT_RV32_FILENAME := $(shell cat $(BIN_DIR)/sha1sum-riscv32 | awk '{ print $$2 };'))

	$(Q)$(eval RES := 0)
	$(Q)$(eval $(foreach FILE,$(PREBUILT_X86_FILENAME), \
	    $(call verify,$(shell grep -w $(FILE) $(BIN_DIR)/sha1sum-linux-x86-softfp | awk '{ print $$1 };'),$(BIN_DIR)/linux-x86-softfp/$(FILE),RES) \
	))
	$(Q)$(eval $(foreach FILE,$(PREBUILT_RV32_FILENAME), \
	    $(call verify,$(shell grep -w $(FILE) $(BIN_DIR)/sha1sum-riscv32 | awk '{ print $$1 };'),$(BIN_DIR)/riscv32/$(FILE),RES) \
	))

	$(Q)if [ "$(RES)" = "1" ]; then \
	    $(PRINTF) "\n$(YELLOW)SHA-1 verification fails! Re-fetching prebuilt binaries from \"rv32emu-prebuilt\" ...\n$(NO_COLOR)"; \
	    wget -q --show-progress https://github.com/sysprog21/rv32emu-prebuilt/releases/download/$(LATEST_RELEASE)/rv32emu-prebuilt.tar.gz -O- | tar -C build --strip-components=1 -xz; \
	else \
	    $(call notice, [OK]); \
	fi

fetch-checksum:
	$(Q)$(PRINTF) "Fetching SHA-1 of prebuilt binaries ... "
	$(Q)wget -q -O $(BIN_DIR)/sha1sum-linux-x86-softfp https://github.com/sysprog21/rv32emu-prebuilt/releases/download/$(LATEST_RELEASE)/sha1sum-linux-x86-softfp
	$(Q)wget -q -O $(BIN_DIR)/sha1sum-riscv32 https://github.com/sysprog21/rv32emu-prebuilt/releases/download/$(LATEST_RELEASE)/sha1sum-riscv32
	$(Q)$(call notice, [OK])
