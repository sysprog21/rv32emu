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

# $(1): tag of GitHub releases
# $(2): name of GitHub releases
# $(3): name showing in terminal
define fetch-releases-tag
    $(if $(wildcard $(BIN_DIR)/$(2)), \
        $(info $(call warnx, $(3) is found. Skipping downloading.)), \
        $(eval LATEST_RELEASE := $(shell wget -q https://api.github.com/repos/sysprog21/rv32emu-prebuilt/releases -O- \
                                         | grep '"tag_name"' \
                                         | grep "$(1)" \
                                         | head -n 1 \
                                         | sed -E 's/.*"tag_name": "([^"]+)".*/\1/')) \
        $(if $(LATEST_RELEASE),, \
            $(error Fetching tag of latest releases failed) \
        ) \
    )
endef

LATEST_RELEASE ?=
ifeq ($(LATEST_RELEASE),)
    ifeq ($(call has, SYSTEM), 1)
        $(call fetch-releases-tag,Linux-Image,rv32emu-linux-image-prebuilt.tar.gz,Linux image)
    else ifeq ($(call has, ARCH_TEST), 1)
        $(call fetch-releases-tag,sail,rv32emu-prebuilt-sail-$(HOST_PLATFORM),Sail model)
    else
        $(call fetch-releases-tag,ELF,rv32emu-prebuilt.tar.gz,Prebuilt benchmark)
    endif
endif
PREBUILT_BLOB_URL = https://github.com/sysprog21/rv32emu-prebuilt/releases/download/$(LATEST_RELEASE)

.PHONY: artifact fetch-checksum

artifact: fetch-checksum
	$(Q)$(PRINTF) "Checking SHA-1 of prebuilt binaries ... "
	$(Q)$(eval RES := 0)

ifeq ($(call has, SYSTEM), 1)
	$(Q)$(eval PREBUILT_LINUX_IMAGE_FILENAME := $(shell cat $(BIN_DIR)/sha1sum-linux-image | awk '{  print $$2 };'))

	$(Q)$(eval $(foreach FILE,$(PREBUILT_LINUX_IMAGE_FILENAME), \
	    $(call verify,$(SHA1SUM),$(shell grep -w $(FILE) $(BIN_DIR)/sha1sum-linux-image | awk '{ print $$1 };'),$(BIN_DIR)/$(FILE),RES) \
	))

	$(Q)$(eval RV32EMU_PREBUILT_TARBALL := rv32emu-linux-image-prebuilt.tar.gz)
else ifeq ($(call has, ARCH_TEST), 1)
	$(Q)$(eval PREBUILT_SAIL_FILENAME := $(shell cat $(BIN_DIR)/rv32emu-prebuilt-sail-$(HOST_PLATFORM).sha | awk '{  print $$2 };'))

	$(Q)$(eval $(foreach FILE,$(PREBUILT_SAIL_FILENAME), \
	    $(call verify,$(SHA1SUM),$(shell grep -w $(FILE) $(BIN_DIR)/rv32emu-prebuilt-sail-$(HOST_PLATFORM).sha | awk '{ print $$1 };'),$(BIN_DIR)/$(FILE),RES) \
	))

	$(Q)$(eval RV32EMU_PREBUILT_TARBALL := rv32emu-prebuilt-sail-$(HOST_PLATFORM))
else
	$(Q)$(eval PREBUILT_X86_FILENAME := $(shell cat $(BIN_DIR)/sha1sum-linux-x86-softfp | awk '{  print $$2 };'))
	$(Q)$(eval PREBUILT_RV32_FILENAME := $(shell cat $(BIN_DIR)/sha1sum-riscv32 | awk '{ print $$2 };'))

	$(Q)$(eval $(foreach FILE,$(PREBUILT_X86_FILENAME), \
	    $(call verify,$(SHA1SUM),$(shell grep -w $(FILE) $(BIN_DIR)/sha1sum-linux-x86-softfp | awk '{ print $$1 };'),$(BIN_DIR)/linux-x86-softfp/$(FILE),RES) \
	))
	$(Q)$(eval $(foreach FILE,$(PREBUILT_RV32_FILENAME), \
	    $(call verify,$(SHA1SUM),$(shell grep -w $(FILE) $(BIN_DIR)/sha1sum-riscv32 | awk '{ print $$1 };'),$(BIN_DIR)/riscv32/$(FILE),RES) \
	))

	$(Q)$(eval RV32EMU_PREBUILT_TARBALL := rv32emu-prebuilt.tar.gz)
endif

ifeq ($(call has, ARCH_TEST), 1)
	$(Q)if [ "$(RES)" = "1" ]; then \
	    $(call warn, SHA-1 verification failed!); \
	    $(PRINTF) "Re-fetching prebuilt binaries from \"rv32emu-prebuilt\" ...\n"; \
	    wget -q --show-progress $(PREBUILT_BLOB_URL)/$(RV32EMU_PREBUILT_TARBALL) -O build/$(RV32EMU_PREBUILT_TARBALL); \
	else \
	    $(call notice, [OK]); \
	fi
else
	$(Q)if [ "$(RES)" = "1" ]; then \
	    $(call warn, SHA-1 verification failed!); \
	    $(PRINTF) "Re-fetching prebuilt binaries from \"rv32emu-prebuilt\" ...\n"; \
	    wget -q --show-progress $(PREBUILT_BLOB_URL)/$(RV32EMU_PREBUILT_TARBALL) -O build/$(RV32EMU_PREBUILT_TARBALL); \
	    tar --strip-components=1 -zxf build/$(RV32EMU_PREBUILT_TARBALL) -C build; \
	else \
	    $(call notice, [OK]); \
	fi
endif

fetch-checksum:
	$(Q)$(PRINTF) "Fetching SHA-1 of prebuilt binaries ... "
ifeq ($(call has, SYSTEM), 1)
    ifeq ($(wildcard $(BIN_DIR)/rv32emu-linux-image-prebuilt.tar.gz),)
		$(Q)wget -q -O $(BIN_DIR)/sha1sum-linux-image $(PREBUILT_BLOB_URL)/sha1sum-linux-image
		$(Q)$(call notice, [OK])
    else
		$(Q)$(call warn, skipped)
    endif
else ifeq ($(call has, ARCH_TEST), 1)
    ifeq ($(wildcard $(BIN_DIR)/rv32emu-prebuilt-sail-$(HOST_PLATFORM)),)
		$(Q)wget -q -O $(BIN_DIR)/rv32emu-prebuilt-sail-$(HOST_PLATFORM).sha $(PREBUILT_BLOB_URL)/rv32emu-prebuilt-sail-$(HOST_PLATFORM).sha
		$(Q)$(call notice, [OK])
    else
		$(Q)$(call warn, skipped)
    endif
else
    ifeq ($(wildcard $(BIN_DIR)/rv32emu-prebuilt.tar.gz),)
		$(Q)wget -q -O $(BIN_DIR)/sha1sum-linux-x86-softfp $(PREBUILT_BLOB_URL)/sha1sum-linux-x86-softfp
		$(Q)wget -q -O $(BIN_DIR)/sha1sum-riscv32 $(PREBUILT_BLOB_URL)/sha1sum-riscv32
		$(Q)$(call notice, [OK])
    else
		$(Q)$(call warn , skipped)
    endif
endif
