COMPRESSED_SUFFIX :=
COMPRESSED_IS_ZIP :=
COMPRESSED_IS_DIR :=
EXTRACTOR :=
VERIFIER :=

# temporarily files to store correct SHA1 value and computed SHA1 value
# respectively for verification of directory source
$(eval SHA1_FILE1 := $(shell mktemp))
$(eval SHA1_FILE2 := $(shell mktemp))

# $(1): compressed source
define prologue
    $(eval _ := $(shell echo "  GET\t$(1)\n"))
    $(info $(_))
endef

# $(1), $(2), $(3): files to be deleted
define epilogue
    $(eval _ := $(shell $(RM) $(1) $(2) $(3)))
endef

# $(1): compressed source URL
# if URL contains git command, then use git
define download
    $(eval _ :=  \
	$(if $(findstring git,$(1)), \
            ($(eval _ := $(shell $(1)))), \
            ($(eval _ := $(shell wget -q --show-progress --continue "$(strip $(1))"))) \
    ))
endef

# $(1): destination directory
# $(2): compressed source(.zip or.gz)
# For Buildroot and Linux kernel, no need to extract
define extract
    $(eval COMPRESSED_SUFFIX := $(suffix $(2)))
    $(eval COMPRESSED_IS_ZIP := $(filter $(COMPRESSED_SUFFIX),.zip))
    $(eval _ := \
        $(if $(findstring git,$(2)), \
	    (# git is used, do nothing),
            ($(eval _ :=  \
                $(if $(COMPRESSED_IS_ZIP), \
                    ($(eval EXTRACTOR := unzip -d $(1) $(2))), \
                    ($(eval EXTRACTOR := tar -xf $(2) -C $(1))) \
            ))
            $(eval _ := $(shell $(EXTRACTOR))))
        ) \
    )
endef

# $(1): correct SHA1 value
# $(2): filename or directory path
# $(3): (optional) returned result
#
# Note:
# 1. for regular file, $(SHA1SUM) command's -c option generates keyword "FAILED" for indicating an unmatch
# 2. for directory, cmp command outputs keyword "differ" for indicating an unmatch
define verify
    $(eval COMPRESSED_IS_DIR := $(if $(wildcard $(2)/*),1,0))
    $(eval _ := \
        $(if $(filter 1,$(COMPRESSED_IS_DIR)), \
            ($(eval VERIFIER :=  \
                echo $(1) > $(SHA1_FILE1) \
                | find $(2) -type f -not -path '*/.git/*' -print0 \
                | sort -z \
                | xargs -0 $(SHA1SUM) \
                | sort \
                | $(SHA1SUM) \
                | cut -f 1 -d ' ' > $(SHA1_FILE2) && cmp $(SHA1_FILE1) $(SHA1_FILE2))), \
            ($(eval VERIFIER := echo "$(strip $(1))  $(strip $(2))" | $(SHA1SUM) -c)) \
    ))
    $(eval _ := $(shell $(VERIFIER) 2>&1))
    $(eval _ := \
        $(if $(filter FAILED differ:,$(_)), \
            ($(if $(3), \
                $(eval $(3) := 1), \
                $(error $(_)) \
            )), \
            (# SHA1 value match, do nothing) \
    ))
endef

# For each external target, the following must be defined in advance:
#   _DATA_URL : the hyperlink which points to archive.
#   _DATA : the file to be read by specific executable.
#   _DATA_SHA1 : the checksum of the content in _DATA

# Doom
# https://tipsmake.com/how-to-run-doom-on-raspberry-pi-without-emulator
DOOM_DATA_URL = http://www.doomworld.com/3ddownloads/ports/shareware_doom_iwad.zip
DOOM_DATA = $(OUT)/DOOM1.WAD
DOOM_DATA_SHA1 = 5b2e249b9c5133ec987b3ea77596381dc0d6bc1d

# Quake
QUAKE_DATA_URL = https://www.libsdl.org/projects/quake/data/quakesw-1.0.6.zip
QUAKE_DATA = $(OUT)/id1/pak0.pak
QUAKE_DATA_SHA1 = 36b42dc7b6313fd9cabc0be8b9e9864840929735

# Timidity software synthesizer configuration for SDL2_mixer
TIMIDITY_DATA_URL = http://www.libsdl.org/projects/mixer/timidity/timidity.tar.gz
TIMIDITY_DATA = $(OUT)/timidity
TIMIDITY_DATA_SHA1 = cf6217a5d824b717ec4a07e15e6c129a4657ca25

# Buildroot
BUILDROOT_VERSION = 2024.05.2
BUILDROOT_DATA = /tmp/buildroot
BUILDROOT_DATA_URL = git clone https://github.com/buildroot/buildroot $(BUILDROOT_DATA) -b $(BUILDROOT_VERSION) --depth=1
BUILDROOT_DATA_SHA1 = e6677825313cdb47f6e62e4dc09a22ae30699b69

# Linux kernel
LINUX_VERSION = linux-6.1.y
LINUX_DATA = /tmp/linux
LINUX_DATA_URL = git clone https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git $(LINUX_DATA) -b $(LINUX_VERSION) --depth=1
LINUX_DATA_SHA1 = 30e95c62e8264aba948bb52d75af57d435bb6b8e

define download-extract-verify
$($(T)_DATA):
	$(Q)$$(call prologue,$$@)
	$(Q)$$(call download,$(strip $($(T)_DATA_URL)))
	$(Q)$$(call extract,$(OUT),$(notdir $($(T)_DATA_URL)))
	$(Q)$$(call verify,$($(T)_DATA_SHA1),$($(T)_DATA))
	$(Q)$$(call epilogue,$(notdir $($(T)_DATA_URL)),$(SHA1_FILE1),$(SHA1_FILE2))
endef

EXTERNAL_DATA = DOOM QUAKE TIMIDITY BUILDROOT LINUX
$(foreach T,$(EXTERNAL_DATA),$(eval $(download-extract-verify)))
