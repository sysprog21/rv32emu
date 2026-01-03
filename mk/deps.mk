# Unified dependency detection
#
# Provides helper functions for detecting libraries and packages.
# Optimized to skip expensive checks for non-build targets (clean, help, etc.)

ifndef _MK_DEPS_INCLUDED
_MK_DEPS_INCLUDED := 1

# Verbosity control (consistent with mk/common.mk)
# 'make V=1' equals to 'make VERBOSE=1'
ifeq ("$(origin V)","command line")
    VERBOSE = $(V)
endif
ifeq ("$(VERBOSE)","1")
    DEVNULL :=
else
    DEVNULL := 2>/dev/null
endif

# pkg-config for cross-compilation
PKG_CONFIG ?= pkg-config

# Dependency Detection Functions (always available)

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

# Skip expensive dependency detection for clean/help/distclean
# SKIP_DEPS_CHECK is set in mk/common.mk
ifeq ($(SKIP_DEPS_CHECK),)

# SDL2 Detection
# Note: HAVE_SDL2 is exported for Kconfig environment detection (tools/detect-env.py)
# SDL2_CFLAGS/LIBS are only computed when CONFIG_SDL=y (after .config is loaded)
HAVE_SDL2 := $(call pkg-exists,sdl2)

# SDL2_mixer
HAVE_SDL2_MIXER := $(shell $(PKG_CONFIG) --exists SDL2_mixer $(DEVNULL) && echo y)

# Export for Kconfig
export HAVE_SDL2
export HAVE_SDL2_MIXER

# Compute SDL flags only when SDL is enabled (deferred until after .config)
# These are used by the build rules, guarded by CONFIG_SDL
ifeq ($(CONFIG_SDL),y)
ifeq ($(HAVE_SDL2),y)
    SDL2_CFLAGS := $(call dep,cflags,sdl2)
    SDL2_LIBS := $(call dep,libs,sdl2)
endif
ifeq ($(HAVE_SDL2_MIXER),y)
ifeq ($(CONFIG_SDL_MIXER),y)
    SDL2_MIXER_LIBS := $(call dep,libs,SDL2_mixer)
endif
endif
endif

endif # SKIP_DEPS_CHECK

endif # _MK_DEPS_INCLUDED
