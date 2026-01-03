# Prebuilt artifacts and benchmark building
#
# Handles downloading prebuilt binaries or building from source.

ifndef _MK_ARTIFACT_INCLUDED
_MK_ARTIFACT_INCLUDED := 1

ENABLE_PREBUILT ?= 1

# Note: CC and CROSS_COMPILE are already set in mk/toolchain.mk

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

# Create output directories (using order-only prerequisite pattern)
$(BIN_DIR)/linux-x86-softfp $(BIN_DIR)/riscv32 $(BIN_DIR)/linux-image:
	$(Q)mkdir -p $@

# $(1): tag of GitHub releases
# $(2): name of GitHub releases
# $(3): name showing in terminal
define fetch-releases-tag
    $(eval LATEST_RELEASE := $(shell wget -q https://api.github.com/repos/sysprog21/rv32emu-prebuilt/releases -O- \
                                     | grep '"tag_name"' \
                                     | grep "$(1)" \
                                     | head -n 1 \
                                     | sed -E 's/.*"tag_name": "([^"]+)".*/\1/')) \
    $(if $(LATEST_RELEASE),, \
        $(error Fetching tag of latest releases failed) \
    ) \
    $(if $(wildcard $(BIN_DIR)/$(2)), \
        $(info $(call warnx, $(3) is found. Skipping downloading.)))
endef

# Verify prebuilt files against a SHA checksum file.
# Sets result to "1" in temp file if any verification fails.
# $(1): SHA checksum file path
# $(2): prefix path for files (can be empty)
# $(3): temp file to store result (writes "1" on failure, preserves existing failures)
# Note: All checks use shell commands to defer until recipe execution.
# $(error) and $(shell) in $(eval) run at Makefile parse time,
# before prerequisites (like fetch-checksum) execute.
define verify-prebuilt-files
	$(Q)if [ ! -s "$(1)" ]; then \
		echo "Error: Checksum file $(1) is missing or empty" >&2; \
		exit 1; \
	fi; \
	verify_failed=$$(cat "$(3)" 2>/dev/null || echo 0); \
	if [ "$$verify_failed" != "1" ]; then \
		while read expected_sha filename; do \
			filepath="$(2)$$filename"; \
			if [ ! -e "$$filepath" ]; then \
				verify_failed=1; break; \
			fi; \
			if ! echo "$$expected_sha  $$filepath" | $(SHA1SUM) -c - >/dev/null 2>&1; then \
				verify_failed=1; break; \
			fi; \
		done < "$(1)"; \
	fi; \
	echo $$verify_failed > "$(3)"
endef

# Handle SHA-1 verification result: re-fetch if failed, report status.
# $(1): whether to extract tarball (yes/no)
# $(2): temp file containing verification result
define handle-sha1-result
	$(Q)if [ "$$(cat "$(2)" 2>/dev/null || echo 0)" = "1" ]; then \
	    $(call warn, SHA-1 verification failed!); \
	    $(PRINTF) "Re-fetching prebuilt binaries from \"rv32emu-prebuilt\" ...\n"; \
	    wget -q --show-progress "$(PREBUILT_BLOB_URL)/$(RV32EMU_PREBUILT_TARBALL)" -O "build/$(RV32EMU_PREBUILT_TARBALL)" || exit 1; \
	    $(if $(filter yes,$(1)),tar --strip-components=1 -zxf "build/$(RV32EMU_PREBUILT_TARBALL)" -C build || exit 1;) \
	else \
	    $(call notice, [OK]); \
	fi; \
	rm -f "$(2)"
endef

# Fetch checksum file if tarball doesn't exist.
# $(1): tarball path to check
# $(2): checksum file(s) to download (space-separated base names)
define fetch-checksum-files
	$(Q)if [ ! -f "$(1)" ]; then \
	    $(foreach f,$(2),wget -q -O "$(BIN_DIR)/$(f)" "$(PREBUILT_BLOB_URL)/$(f)" || exit 1;) \
	    $(call notice, [OK]); \
	else \
	    $(call warn, skipped); \
	fi
endef

LATEST_RELEASE ?=

# Only fetch releases when artifact-related targets are requested.
# This prevents network calls during unrelated targets like 'make defconfig'.
# IMPORTANT: When adding new targets that depend on 'artifact' in the main
# Makefile, they must also be added here to trigger release fetching.
ARTIFACT_TARGETS := artifact fetch-checksum scimark2 ieeelib \
                    check misalign doom quake arch-test system
ifneq ($(filter $(ARTIFACT_TARGETS),$(MAKECMDGOALS)),)
ifeq ($(call has, PREBUILT), 1)
    # On macOS/arm64 Github runner, let's leverage the ${{ secrets.GITHUB_TOKEN }} to prevent 403 rate limit error.
    # Thus, the LATEST_RELEASE tag is defined at Github job steps, no need to fetch them here.
    # Also skip fetching if prebuilt artifacts already exist (cached from previous artifact fetch).
    # Use checksum files as sentinels - they indicate a complete artifact fetch and are required
    # for SHA verification. If missing, we need LATEST_RELEASE to construct download URLs.
    ifeq ($(LATEST_RELEASE),)
         ifeq ($(call has, SYSTEM), 1)
             ifeq ($(wildcard $(BIN_DIR)/sha1sum-linux-image),)
                 $(call fetch-releases-tag,Linux-Image,rv32emu-linux-image-prebuilt.tar.gz,Linux image)
             endif
         else ifeq ($(call has, ARCH_TEST), 1)
             ifeq ($(wildcard $(BIN_DIR)/rv32emu-prebuilt-sail-$(HOST_PLATFORM).sha),)
                 $(call fetch-releases-tag,sail,rv32emu-prebuilt-sail-$(HOST_PLATFORM),Sail model)
             endif
         else
             ifeq ($(wildcard $(BIN_DIR)/sha1sum-riscv32),)
                 $(call fetch-releases-tag,ELF,rv32emu-prebuilt.tar.gz,Prebuilt benchmark)
             endif
         endif
    endif
endif
endif

ifeq ($(call has, PREBUILT), 1)
    PREBUILT_BLOB_URL = https://github.com/sysprog21/rv32emu-prebuilt/releases/download/$(LATEST_RELEASE)
else
    # Since rv32emu only supports the dynamic binary translation of integer
    # instruction in tiered compilation currently, we disable the hardware
    # floating-point and the related SIMD operation of x86.
    CFLAGS := -m32 -mno-sse -mno-sse2 -msoft-float -O2 -Wno-unused-result -L$(BIN_DIR)
    LDFLAGS := -lsoft-fp -lm

    CFLAGS_CROSS := -march=rv32im -mabi=ilp32 -O2 -Wno-implicit-function-declaration
    LDFLAGS_CROSS := -lm -lsemihost
endif

.PHONY: artifact fetch-checksum scimark2 ieeelib

# Temp file for verification result.
# Safe for normal use: Make deduplicates targets and runs recipe lines sequentially.
# Initialized with "0" (success) and set to "1" on verification failure.
VERIFY_RESULT_FILE := $(BIN_DIR)/.verify_result

artifact: fetch-checksum ieeelib scimark2
ifeq ($(call has, PREBUILT), 1)
	$(Q)$(PRINTF) "Checking SHA-1 of prebuilt binaries ... "
	$(Q)rm -f $(VERIFY_RESULT_FILE) && echo 0 > $(VERIFY_RESULT_FILE)

ifeq ($(call has, SYSTEM), 1)
	$(call verify-prebuilt-files,$(BIN_DIR)/sha1sum-linux-image,$(BIN_DIR)/,$(VERIFY_RESULT_FILE))
	$(Q)$(eval RV32EMU_PREBUILT_TARBALL := rv32emu-linux-image-prebuilt.tar.gz)
else ifeq ($(call has, ARCH_TEST), 1)
	$(call verify-prebuilt-files,$(BIN_DIR)/rv32emu-prebuilt-sail-$(HOST_PLATFORM).sha,$(BIN_DIR)/,$(VERIFY_RESULT_FILE))
	$(Q)$(eval RV32EMU_PREBUILT_TARBALL := rv32emu-prebuilt-sail-$(HOST_PLATFORM))
else
	$(call verify-prebuilt-files,$(BIN_DIR)/sha1sum-linux-x86-softfp,$(BIN_DIR)/linux-x86-softfp/,$(VERIFY_RESULT_FILE))
	$(call verify-prebuilt-files,$(BIN_DIR)/sha1sum-riscv32,$(BIN_DIR)/riscv32/,$(VERIFY_RESULT_FILE))
	$(Q)$(eval RV32EMU_PREBUILT_TARBALL := rv32emu-prebuilt.tar.gz)
endif

ifeq ($(call has, ARCH_TEST), 1)
	$(call handle-sha1-result,no,$(VERIFY_RESULT_FILE))
else
	$(call handle-sha1-result,yes,$(VERIFY_RESULT_FILE))
endif
else
ifeq ($(call has, SYSTEM), 1)
	$(Q)(mkdir -p /tmp/rv32emu-linux-image-prebuilt/linux-image)
	$(Q)(cd $(BIN_DIR) && $(SHA1SUM) linux-image/Image >> sha1sum-linux-image)
	$(Q)(cd $(BIN_DIR) && $(SHA1SUM) linux-image/rootfs.cpio >> sha1sum-linux-image)
	$(Q)(cd $(BIN_DIR) && $(SHA1SUM) linux-image/simplefs.ko >> sha1sum-linux-image)
	$(Q)(mv $(BIN_DIR)/sha1sum-linux-image /tmp)
	$(Q)(mv $(BIN_DIR)/linux-image/Image /tmp/rv32emu-linux-image-prebuilt/linux-image)
	$(Q)(mv $(BIN_DIR)/linux-image/rootfs.cpio /tmp/rv32emu-linux-image-prebuilt/linux-image)
	$(Q)(mv $(BIN_DIR)/linux-image/simplefs.ko /tmp/rv32emu-linux-image-prebuilt/linux-image)
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
	$(call fetch-checksum-files,$(BIN_DIR)/rv32emu-linux-image-prebuilt.tar.gz,sha1sum-linux-image)
else ifeq ($(call has, ARCH_TEST), 1)
	$(call fetch-checksum-files,$(BIN_DIR)/rv32emu-prebuilt-sail-$(HOST_PLATFORM),rv32emu-prebuilt-sail-$(HOST_PLATFORM).sha)
else
	$(call fetch-checksum-files,$(BIN_DIR)/rv32emu-prebuilt.tar.gz,sha1sum-linux-x86-softfp sha1sum-riscv32)
endif
endif

scimark2: | $(BIN_DIR)/linux-x86-softfp $(BIN_DIR)/riscv32
ifeq ($(call has, PREBUILT), 0)
ifeq ($(call has, SYSTEM), 0)
	$(call prologue,scimark2)
	$(Q)$(call download,$(SCIMARK2_URL))
	$(call verify-sha,$(SHA1SUM),$(SCIMARK2_SHA1),$(notdir $(SCIMARK2_URL)))
	$(Q)$(call extract,./tests/scimark2,$(notdir $(SCIMARK2_URL)),0)
	$(call epilogue,$(notdir $(SCIMARK2_URL)))
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

endif # _MK_ARTIFACT_INCLUDED
