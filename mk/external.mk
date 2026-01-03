# External data dependencies (Doom, Quake, Linux kernel, etc.)
#
# Provides templates for downloading, extracting, and verifying external assets.

ifndef _MK_EXTERNAL_INCLUDED
_MK_EXTERNAL_INCLUDED := 1

# SHA Utilities (for verification)
# Use command -v for POSIX compatibility (avoid 'which')

SHA1SUM := $(shell command -v sha1sum 2>/dev/null)
ifndef SHA1SUM
    SHA1SUM := $(shell command -v shasum 2>/dev/null)
endif
ifndef SHA1SUM
    $(warning SHA1SUM tool not found - checksum verification will be skipped)
endif

SHA256SUM := $(shell command -v sha256sum 2>/dev/null)
ifeq ($(SHA256SUM),)
    SHA256SUM_SHASUM := $(shell command -v shasum 2>/dev/null)
    ifneq ($(SHA256SUM_SHASUM),)
        # Verify shasum supports -a 256 before using it
        SHA256SUM_TEST := $(shell echo test | $(SHA256SUM_SHASUM) -a 256 >/dev/null 2>&1 && echo ok)
        ifeq ($(SHA256SUM_TEST),ok)
            SHA256SUM := $(SHA256SUM_SHASUM) -a 256
        endif
    endif
endif
ifeq ($(SHA256SUM),)
    $(warning SHA256SUM tool not found - checksum verification will be skipped)
endif

# Download/Extract/Verify Templates

# Print download message
# $(1): target file
define prologue
	$(VECHO) "  GET\t$(1)\n"
endef

# Check if command is a git clone (first word is 'git')
# $(1): URL or git command
define is-git-clone
$(filter git,$(firstword $(1)))
endef

# Detect wget --show-progress support (GNU wget extension)
WGET_HAS_PROGRESS := $(shell wget --help 2>&1 | grep -q show-progress && echo yes)

# Download from URL (supports git clone or wget)
# $(1): URL or git command
define download
$(if $(call is-git-clone,$(1)),\
	$(1),\
	wget -q $(if $(WGET_HAS_PROGRESS),--show-progress) --continue "$(strip $(1))")
endef

# Extract archive to destination
# $(1): destination directory
# $(2): archive file (.zip or .tar.gz)
# $(3): strip component level (for tar only)
define extract
$(if $(filter .zip,$(suffix $(2))),\
	unzip -q -d $(1) $(2),\
	tar -xf $(2) --strip-components=$(3) -C $(1))
endef

# Verify SHA checksum (auto-detects file vs directory at runtime)
# $(1): SHA command (sha1sum or sha256sum, may be empty if tool unavailable)
# $(2): expected SHA value
# $(3): path to verify (file or directory)
# Returns: exits with error if verification fails; skips if no SHA tool
define verify-sha
	@if [ -z "$(1)" ]; then \
		echo "Skipping SHA verification for $(3) (no SHA tool available)"; \
	elif [ -d "$(3)" ]; then \
		FILE_HASHES=$$(find "$(3)" -type f -not -path '*/.git/*' -print0 | LC_ALL=C sort -z | xargs -0 $(1) 2>/dev/null | LC_ALL=C sort); \
		if [ -z "$$FILE_HASHES" ]; then \
			echo "SHA verification failed for directory $(3): no files found"; \
			exit 1; \
		fi; \
		COMPUTED=$$(echo "$$FILE_HASHES" | $(1) | cut -f1 -d' '); \
		if [ "$$COMPUTED" != "$(2)" ]; then \
			echo "SHA verification failed for directory $(3)"; \
			exit 1; \
		fi; \
	else \
		if ! echo "$(2)  $(3)" | $(1) -c - >/dev/null 2>&1; then \
			echo "SHA verification failed for $(3)"; \
			exit 1; \
		fi; \
	fi
endef

# Clean up downloaded archive
# $(1): archive filename
define epilogue
	$(Q)$(RM) $(1)
endef

# External Data Definitions

# Doom WAD file
DOOM_DATA_URL := https://www.doomworld.com/3ddownloads/ports/shareware_doom_iwad.zip
DOOM_DATA_DEST := $(OUT)
DOOM_DATA := $(DOOM_DATA_DEST)/DOOM1.WAD
DOOM_DATA_SHA := 5b2e249b9c5133ec987b3ea77596381dc0d6bc1d
DOOM_DATA_SHA_CMD := $(SHA1SUM)

# Quake PAK file
QUAKE_DATA_URL := https://www.libsdl.org/projects/quake/data/quakesw-1.0.6.zip
QUAKE_DATA_DEST := $(OUT)
QUAKE_DATA := $(QUAKE_DATA_DEST)/id1/pak0.pak
QUAKE_DATA_SHA := 36b42dc7b6313fd9cabc0be8b9e9864840929735
QUAKE_DATA_SHA_CMD := $(SHA1SUM)

# Timidity software synthesizer (for MIDI in SDL2_mixer)
TIMIDITY_DATA_URL := https://www.libsdl.org/projects/old/SDL_mixer/timidity/timidity.tar.gz
TIMIDITY_DATA_DEST := $(OUT)
TIMIDITY_DATA := $(TIMIDITY_DATA_DEST)/timidity
TIMIDITY_DATA_SKIP_DIR_LEVEL := 0
# find $(TIMIDITY_DATA_DEST)/timidity -type f -not -path '*/.git/*' -print0 | \
#	LC_ALL=C sort -z | \
#	xargs -0 sha1sum | \
#	LC_ALL=C sort | \
#	sha1sum
TIMIDITY_DATA_SHA := cf6217a5d824b717ec4a07e15e6c129a4657ca25
TIMIDITY_DATA_SHA_CMD := $(SHA1SUM)

# Buildroot (for Linux image building)
BUILDROOT_VERSION := 2025.11
BUILDROOT_DATA_DEST := /tmp
BUILDROOT_DATA := $(BUILDROOT_DATA_DEST)/buildroot
BUILDROOT_DATA_URL := git clone https://github.com/buildroot/buildroot $(BUILDROOT_DATA) -b $(BUILDROOT_VERSION) --depth=1
# find /tmp/buildroot -type f -not -path '*/.git/*' -print0 | \
#	LC_ALL=C sort -z | \
#	xargs -0 sha1sum | \
#	LC_ALL=C sort | \
#	sha1sum
BUILDROOT_DATA_SHA := 70999b51eb4034eb96457a0ac210365c9cc7c2bb
BUILDROOT_DATA_SHA_CMD := $(SHA1SUM)

# Linux kernel (uses lazy evaluation to avoid parse-time network calls)
LINUX_VERSION := 6
LINUX_PATCHLEVEL := 1
LINUX_CDN_BASE_URL := https://cdn.kernel.org/pub/linux/kernel
LINUX_CDN_VERSION_URL := $(LINUX_CDN_BASE_URL)/v$(LINUX_VERSION).x
LINUX_DATA_DEST := /tmp/linux
LINUX_DATA_SKIP_DIR_LEVEL := 1
LINUX_DATA_SHA_CMD := $(SHA256SUM)

# simplefs kernel module
SIMPLEFS_VERSION := rel2025.0
SIMPLEFS_DATA_DEST := /tmp
SIMPLEFS_DATA := $(SIMPLEFS_DATA_DEST)/simplefs
SIMPLEFS_DATA_URL := git clone https://github.com/sysprog21/simplefs $(SIMPLEFS_DATA) -b $(SIMPLEFS_VERSION) --depth=1
# find /tmp/simplefs -type f -not -path '*/.git/*' -print0 | \
#	LC_ALL=C sort -z | \
#	xargs -0 sha1sum | \
#	LC_ALL=C sort | \
#	sha1sum
SIMPLEFS_DATA_SHA := 863936f72e0781b240c5ec4574510c57f0394b99
SIMPLEFS_DATA_SHA_CMD := $(SHA1SUM)

# Download Rules Template

# Generate download/extract/verify rules for each external target
# $(1): target name (DOOM, QUAKE, etc.)
define download-extract-verify
$($(1)_DATA):
	$$(call prologue,$$@)
	$(Q)mkdir -p $($(1)_DATA_DEST)
	$(Q)$$(call download,$($(1)_DATA_URL))
	$(Q)$(if $(call is-git-clone,$($(1)_DATA_URL)),,\
		$$(call extract,$($(1)_DATA_DEST),$(notdir $($(1)_DATA_URL)),$(or $($(1)_DATA_SKIP_DIR_LEVEL),0)))
	$$(call verify-sha,$($(1)_DATA_SHA_CMD),$($(1)_DATA_SHA),$($(1)_DATA))
	$(if $(call is-git-clone,$($(1)_DATA_URL)),,\
		$$(call epilogue,$(notdir $($(1)_DATA_URL))))
endef

# Generate rules for static external data (known URLs at parse time)
EXTERNAL_DATA_STATIC := DOOM QUAKE TIMIDITY BUILDROOT SIMPLEFS
$(foreach T,$(EXTERNAL_DATA_STATIC),$(eval $(call download-extract-verify,$(T))))

# Linux kernel: special rule with lazy network detection (avoids parse-time wget)
# Detects latest patch version and SHA only when this target is actually built
# Note: Uses awk for portable version sorting (sort -V is GNU-specific)
$(LINUX_DATA_DEST)/linux-$(LINUX_VERSION).$(LINUX_PATCHLEVEL).%.tar.gz:
	$(Q)mkdir -p $(LINUX_DATA_DEST)
	$(VECHO) "  GET\t$@\n"
	$(Q)LINUX_TARBALL=$$(wget -q -O- $(LINUX_CDN_VERSION_URL) 2>/dev/null | \
		grep -oE 'linux-$(LINUX_VERSION)\.$(LINUX_PATCHLEVEL)\.[0-9]+\.tar\.gz' | \
		awk -F'[.-]' '{print $$4, $$0}' | sort -rn | head -1 | awk '{print $$2}'); \
	if [ -z "$$LINUX_TARBALL" ]; then \
		echo "Error: Failed to detect Linux kernel tarball from $(LINUX_CDN_VERSION_URL)"; \
		exit 1; \
	fi; \
	LINUX_SHA=$$(wget -q -O- $(LINUX_CDN_VERSION_URL)/sha256sums.asc 2>/dev/null | \
		grep "$$LINUX_TARBALL" | awk '{print $$1}'); \
	if [ -z "$$LINUX_SHA" ]; then \
		echo "Error: Failed to fetch SHA256 for $$LINUX_TARBALL"; \
		exit 1; \
	fi; \
	wget -q --show-progress --continue "$(LINUX_CDN_VERSION_URL)/$$LINUX_TARBALL" && \
	tar -xf "$$LINUX_TARBALL" --strip-components=$(LINUX_DATA_SKIP_DIR_LEVEL) -C $(LINUX_DATA_DEST) && \
	if [ -z "$(LINUX_DATA_SHA_CMD)" ]; then \
		echo "Skipping SHA verification for $$LINUX_TARBALL (no SHA tool available)"; \
	elif ! echo "$$LINUX_SHA  $$LINUX_TARBALL" | $(LINUX_DATA_SHA_CMD) -c - >/dev/null 2>&1; then \
		echo "SHA verification failed for $$LINUX_TARBALL"; exit 1; \
	fi && \
	$(RM) "$$LINUX_TARBALL"

# Demo Applications (Doom, Quake)

ifeq ($(CONFIG_SDL),y)
doom: artifact $(DOOM_DATA) $(BIN)
	(cd $(OUT); LC_ALL=C ../$(BIN) riscv32/doom)

ifeq ($(CONFIG_EXT_F),y)
quake: artifact $(QUAKE_DATA) $(BIN)
	(cd $(OUT); LC_ALL=C ../$(BIN) riscv32/quake)
endif
endif

.PHONY: doom quake

endif # _MK_EXTERNAL_INCLUDED
