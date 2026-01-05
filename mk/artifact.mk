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

# URL and API Configuration (single source of truth)
PREBUILT_REPO := sysprog21/rv32emu-prebuilt
GITHUB_API_URL := https://api.github.com/repos/$(PREBUILT_REPO)/releases
GITHUB_BLOB_URL := https://github.com/$(PREBUILT_REPO)/releases/download

# Build blob URL from tag
# $(1): release tag
prebuilt-url = $(GITHUB_BLOB_URL)/$(1)

# HTTP utilities are provided by mk/http.mk (included before this file)
# Provides: HTTP_TOOL, HTTP_GET, HTTP_DOWNLOAD, HTTP_DOWNLOAD_QUIET

# Mode Configuration (consolidates all mode-specific settings)
# Each mode defines: TAG, STAMP, CHECKSUMS, TARBALL, SENTINEL, EXTRACT, VERIFY_SPECS

# SYSTEM mode (Linux kernel boot)
MODE_SYSTEM_TAG        := Linux-Image
MODE_SYSTEM_STAMP      := $(BIN_DIR)/.stamp-linux-image
MODE_SYSTEM_CHECKSUMS  := sha1sum-linux-image
MODE_SYSTEM_TARBALL    := rv32emu-linux-image-prebuilt.tar.gz
MODE_SYSTEM_SENTINEL   := $(BIN_DIR)/linux-image/Image
MODE_SYSTEM_EXTRACT    := yes
MODE_SYSTEM_VERIFY     := $(BIN_DIR)/sha1sum-linux-image:$(BIN_DIR)/

# ARCH_TEST mode (RISC-V compliance tests)
MODE_ARCH_TAG          := sail
MODE_ARCH_STAMP        := $(BIN_DIR)/.stamp-sail
MODE_ARCH_CHECKSUMS    := rv32emu-prebuilt-sail-$(HOST_PLATFORM).sha
MODE_ARCH_TARBALL      := rv32emu-prebuilt-sail-$(HOST_PLATFORM)
MODE_ARCH_SENTINEL     := $(BIN_DIR)/riscv_sim_RV32
MODE_ARCH_EXTRACT      := no
MODE_ARCH_VERIFY       := $(BIN_DIR)/rv32emu-prebuilt-sail-$(HOST_PLATFORM).sha:$(BIN_DIR)/

# ELF mode (default prebuilt binaries)
MODE_ELF_TAG           := ELF
MODE_ELF_STAMP         := $(BIN_DIR)/.stamp-prebuilt
MODE_ELF_CHECKSUMS     := sha1sum-linux-x86-softfp sha1sum-riscv32
MODE_ELF_TARBALL       := rv32emu-prebuilt.tar.gz
MODE_ELF_SENTINEL      := $(BIN_DIR)/riscv32/coremark
MODE_ELF_EXTRACT       := yes
MODE_ELF_VERIFY        := $(BIN_DIR)/sha1sum-linux-x86-softfp:$(BIN_DIR)/linux-x86-softfp/ $(BIN_DIR)/sha1sum-riscv32:$(BIN_DIR)/riscv32/

# Select active mode configuration
ifeq ($(call has, SYSTEM), 1)
    ACTIVE_TAG       := $(MODE_SYSTEM_TAG)
    ACTIVE_STAMP     := $(MODE_SYSTEM_STAMP)
    ACTIVE_CHECKSUMS := $(MODE_SYSTEM_CHECKSUMS)
    ACTIVE_TARBALL   := $(MODE_SYSTEM_TARBALL)
    ACTIVE_SENTINEL  := $(MODE_SYSTEM_SENTINEL)
    ACTIVE_EXTRACT   := $(MODE_SYSTEM_EXTRACT)
    ACTIVE_VERIFY    := $(MODE_SYSTEM_VERIFY)
else ifeq ($(call has, ARCH_TEST), 1)
    ACTIVE_TAG       := $(MODE_ARCH_TAG)
    ACTIVE_STAMP     := $(MODE_ARCH_STAMP)
    ACTIVE_CHECKSUMS := $(MODE_ARCH_CHECKSUMS)
    ACTIVE_TARBALL   := $(MODE_ARCH_TARBALL)
    ACTIVE_SENTINEL  := $(MODE_ARCH_SENTINEL)
    ACTIVE_EXTRACT   := $(MODE_ARCH_EXTRACT)
    ACTIVE_VERIFY    := $(MODE_ARCH_VERIFY)
else
    ACTIVE_TAG       := $(MODE_ELF_TAG)
    ACTIVE_STAMP     := $(MODE_ELF_STAMP)
    ACTIVE_CHECKSUMS := $(MODE_ELF_CHECKSUMS)
    ACTIVE_TARBALL   := $(MODE_ELF_TARBALL)
    ACTIVE_SENTINEL  := $(MODE_ELF_SENTINEL)
    ACTIVE_EXTRACT   := $(MODE_ELF_EXTRACT)
    ACTIVE_VERIFY    := $(MODE_ELF_VERIFY)
endif

# Core Macros

# Shell command to fetch tag from GitHub API
# $(1): tag pattern
FETCH_TAG_CMD = $(call HTTP_GET,$(GITHUB_API_URL)) | grep '"tag_name"' | grep "$(1)" | head -n 1 | sed -E 's/.*"tag_name": "([^"]+)".*/\1/'

# Fetch the latest release tag from GitHub API (parse-time)
# $(1): tag pattern to match (e.g., "ELF", "Linux-Image", "sail")
define fetch-releases-tag
    $(eval LATEST_RELEASE := $(shell $(call FETCH_TAG_CMD,$(1)))) \
    $(if $(LATEST_RELEASE),, \
        $(error Fetching tag of latest releases failed) \
    )
endef

# Check if artifacts are fully present (stamp + checksums + binary)
# Note: Uses wildcard which only checks existence, not content.
# Empty checksum files are handled at recipe time by fetch-checksum-files.
# $(1): Stamp file
# $(2): Checksum file(s) - space-separated base names
# $(3): Representative binary
# Note: The foreach/if combo emits "x" for each MISSING file. If any "x" exists,
# the outer $(if ...) returns empty; otherwise returns "yes" (all files present).
check-sentinels = $(and $(wildcard $(1)),$(if $(foreach f,$(2),$(if $(wildcard $(BIN_DIR)/$(f)),,x)),,yes),$(wildcard $(3)))

# Handle SHA-1 verification result: re-fetch if failed, re-verify after fetch
# $(1): whether to extract tarball (yes/no)
# $(2): temp file containing verification result
# $(3): stamp file to create on success
# $(4): tag pattern for recovery fetch
# $(5): tarball filename
# $(6): verification specs as "checksum_file:verify_dir" pairs
define handle-sha1-result
	$(Q)if [ "$$(cat "$(2)" 2>/dev/null || echo 0)" = "1" ]; then \
	    $(call warn, SHA-1 verification failed!); \
	    blob_url="$(PREBUILT_BLOB_URL)"; \
	    if [ -z "$(LATEST_RELEASE)" ]; then \
	        echo "Attempting to recover by fetching latest tag for $(4)..."; \
	        tag=$$($(call FETCH_TAG_CMD,$(4))); \
	        if [ -z "$$tag" ]; then \
	             echo "Error: Recovery failed. Cannot fetch tag." >&2; \
	             rm -f "$(2)" "$(3)"; exit 1; \
	        fi; \
	        blob_url="$(call prebuilt-url,$$tag)"; \
	    fi; \
	    $(PRINTF) "Re-fetching prebuilt binaries from $$blob_url ...\n"; \
	    rm -f "$(3)"; \
	    $(call HTTP_DOWNLOAD,"$$blob_url/$(5)","$(BIN_DIR)/$(5)") || exit 1; \
	    $(if $(filter yes,$(1)),tar --strip-components=1 -zxf "$(BIN_DIR)/$(5)" -C "$(BIN_DIR)" || exit 1;) \
	    $(PRINTF) "Re-verifying after re-fetch ... "; \
	    reverify_ok=1; \
	    for spec in $(6); do \
	        checksum=$$(echo "$$spec" | cut -d: -f1); \
	        dir=$$(echo "$$spec" | cut -d: -f2); \
	        if ! (cd "$$dir" && $(SHA1SUM) -c "$$checksum" >/dev/null 2>&1); then \
	            reverify_ok=0; break; \
	        fi; \
	    done; \
	    if [ "$$reverify_ok" = "1" ]; then \
	        $(call notice, [OK]); \
	        touch "$(3)"; \
	    else \
	        echo "FAILED" >&2; \
	        echo "Error: Re-fetch succeeded but verification still fails." >&2; \
	        echo "The downloaded archive may be corrupted." >&2; \
	        rm -f "$(2)"; exit 1; \
	    fi; \
	else \
	    $(call notice, [OK]); \
	    touch "$(3)"; \
	fi; \
	rm -f "$(2)"
endef

# Fetch checksum files if any are missing or empty
# Handles edge case where checksums exist but are empty (fetch tag if needed)
# $(1): checksum file(s) to download (space-separated base names)
# $(2): tag pattern for recovery fetch
define fetch-checksum-files
	$(Q)missing=0; \
	for f in $(1); do \
	    if [ ! -s "$(BIN_DIR)/$$f" ]; then missing=1; break; fi; \
	done; \
	if [ "$$missing" = "1" ]; then \
	    blob_url="$(PREBUILT_BLOB_URL)"; \
	    if [ -z "$(LATEST_RELEASE)" ]; then \
	        tag=$$($(call FETCH_TAG_CMD,$(2))); \
	        if [ -z "$$tag" ]; then \
	            echo "Error: Cannot fetch release tag." >&2; exit 1; \
	        fi; \
	        blob_url="$(call prebuilt-url,$$tag)"; \
	    fi; \
	    $(foreach f,$(1),$(call HTTP_DOWNLOAD_QUIET,"$$blob_url/$(f)","$(BIN_DIR)/$(f)") || exit 1;) \
	    $(call notice, [OK]); \
	else \
	    $(call notice, [cached]); \
	fi
endef

# Release Tag Fetching (conditional on artifact targets)
LATEST_RELEASE ?=

# Only fetch releases when artifact-related targets are requested.
# This prevents network calls during unrelated targets like 'make defconfig'.
ARTIFACT_TARGETS := artifact fetch-checksum scimark2 ieeelib \
                    check misalign doom quake arch-test system gdbstub-test

ifneq ($(filter $(ARTIFACT_TARGETS),$(MAKECMDGOALS)),)
ifeq ($(call has, PREBUILT), 1)
    # Verify HTTP download tool is available
    ifeq ($(HTTP_TOOL),)
        $(error No HTTP download tool found. Please install curl or wget)
    endif
    # Skip LATEST_RELEASE fetch only when artifacts are fully present
    ifeq ($(LATEST_RELEASE),)
        ifeq ($(call check-sentinels,$(ACTIVE_STAMP),$(ACTIVE_CHECKSUMS),$(ACTIVE_SENTINEL)),)
            $(call fetch-releases-tag,$(ACTIVE_TAG))
        endif
    endif
endif
endif

ifeq ($(call has, PREBUILT), 1)
    PREBUILT_BLOB_URL = $(call prebuilt-url,$(LATEST_RELEASE))
else
    # Build from source: disable hardware floating-point for x86 compatibility
    CFLAGS := -m32 -mno-sse -mno-sse2 -msoft-float -O2 -Wno-unused-result -L$(BIN_DIR)
    LDFLAGS := -lsoft-fp -lm

    CFLAGS_CROSS := -march=rv32im -mabi=ilp32 -O2 -Wno-implicit-function-declaration
    LDFLAGS_CROSS := -lm -lsemihost
endif

# Build Targets
.PHONY: artifact fetch-checksum scimark2 ieeelib

# Temp file for verification result
VERIFY_RESULT_FILE := $(BIN_DIR)/.verify_result

artifact: fetch-checksum ieeelib scimark2
ifeq ($(call has, PREBUILT), 1)
	$(Q)$(PRINTF) "Verifying prebuilt binaries ... "
	$(Q)rm -f "$(VERIFY_RESULT_FILE)" && echo 0 > "$(VERIFY_RESULT_FILE)"
	$(Q)for spec in $(ACTIVE_VERIFY); do \
	    checksum=$$(echo "$$spec" | cut -d: -f1); \
	    dir=$$(echo "$$spec" | cut -d: -f2); \
	    if [ ! -s "$$checksum" ]; then \
	        echo 1 > "$(VERIFY_RESULT_FILE)"; break; \
	    elif ! (cd "$$dir" && $(SHA1SUM) -c "$$checksum" >/dev/null 2>&1); then \
	        echo 1 > "$(VERIFY_RESULT_FILE)"; break; \
	    fi; \
	done
	$(call handle-sha1-result,$(ACTIVE_EXTRACT),$(VERIFY_RESULT_FILE),$(ACTIVE_STAMP),$(ACTIVE_TAG),$(ACTIVE_TARBALL),$(ACTIVE_VERIFY))
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
	$(Q)$(PRINTF) "Fetching checksum files ... "
	$(call fetch-checksum-files,$(ACTIVE_CHECKSUMS),$(ACTIVE_TAG))
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
