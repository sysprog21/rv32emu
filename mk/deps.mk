# Unified dependency detection
#
# Provides helper functions for detecting libraries and packages.

ifndef _MK_DEPS_INCLUDED
_MK_DEPS_INCLUDED := 1

# Verbosity control
ifeq ($(V),1)
    DEVNULL :=
else
    DEVNULL := 2>/dev/null
endif

# pkg-config for cross-compilation
PKG_CONFIG ?= pkg-config

# Dependency Detection Functions

# dep(type, packages)
# type: cflags | libs
# packages: space-separated package names
#
# Usage:
#   CFLAGS += $(call dep,cflags,sdl2)
#   LDFLAGS += $(call dep,libs,sdl2)
#
define dep
$(shell \
    for pkg in $(2); do \
        if command -v $${pkg}-config >/dev/null 2>&1; then \
            $${pkg}-config --$(1) $(DEVNULL); \
        elif command -v $(PKG_CONFIG) >/dev/null 2>&1; then \
            $(PKG_CONFIG) --$(1) $$pkg $(DEVNULL); \
        fi; \
    done \
)
endef

# pkg-exists(package)
# Returns "y" if found, empty otherwise
#
define pkg-exists
$(shell \
    if command -v $(1)-config >/dev/null 2>&1; then \
        echo y; \
    elif $(PKG_CONFIG) --exists $(1) $(DEVNULL); then \
        echo y; \
    fi \
)
endef

# Common Dependency Checks

# SDL2
HAVE_SDL2 := $(call pkg-exists,sdl2)
ifeq ($(HAVE_SDL2),y)
    SDL2_CFLAGS := $(call dep,cflags,sdl2)
    SDL2_LIBS := $(call dep,libs,sdl2)
endif

# SDL2_mixer
HAVE_SDL2_MIXER := $(shell $(PKG_CONFIG) --exists SDL2_mixer $(DEVNULL) && echo y)
ifeq ($(HAVE_SDL2_MIXER),y)
    SDL2_MIXER_LIBS := $(call dep,libs,SDL2_mixer)
endif

# Export for Kconfig
export HAVE_SDL2
export HAVE_SDL2_MIXER

# SHA Utilities

SHA1SUM := $(shell which sha1sum 2>/dev/null)
ifndef SHA1SUM
    SHA1SUM := $(shell which shasum 2>/dev/null)
    ifndef SHA1SUM
        SHA1SUM := echo
    endif
endif

SHA256SUM := $(shell which sha256sum 2>/dev/null)
ifndef SHA256SUM
    SHA256SUM_TMP := $(shell which shasum 2>/dev/null)
    ifdef SHA256SUM_TMP
        SHA256SUM := $(SHA256SUM_TMP) -a 256
    else
        SHA256SUM := echo
    endif
endif

endif # _MK_DEPS_INCLUDED
