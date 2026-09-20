# VirtIO network device support
#
# This file contains build rules and host/backend constraints specific to
# virtio-net.

ifndef _MK_VIRTIO_NET_INCLUDED
_MK_VIRTIO_NET_INCLUDED := 1

SYSTEM_MMIO := 0
ifeq ($(call has,SYSTEM),1)
ifneq ($(call has,ELF_LOADER),1)
SYSTEM_MMIO := 1
endif
endif

# VirtIO networking is available only for kernel system emulation and requires
# at least one backend supported by the selected host/compiler. CONFIG_* values
# may be overridden by legacy ENABLE_* flags after loading .config, so mirror
# the remaining Kconfig host/compiler constraints here as part of the effective
# build state instead of relying only on the Kconfig dependency graph.
VIRTIO_NET_COMMON_BUILD_ENABLED := n
ifeq ($(SYSTEM_MMIO),1)
ifeq ($(call has,VIRTIO_NET),1)
ifneq ($(CC_IS_EMCC),1)
ifneq ($(filter Linux Darwin,$(UNAME_S)),)
VIRTIO_NET_COMMON_BUILD_ENABLED := y
endif
endif
endif
endif

# Linux TAP backend.
VIRTIO_NET_TAP_BUILD_ENABLED := n
ifeq ($(VIRTIO_NET_COMMON_BUILD_ENABLED),y)
ifeq ($(UNAME_S),Linux)
ifeq ($(call has,VIRTIO_NET_TAP),1)
VIRTIO_NET_TAP_BUILD_ENABLED := y
endif
endif
endif

# User-mode SLIRP backend.
VIRTIO_NET_USER_BUILD_ENABLED := n
ifeq ($(VIRTIO_NET_COMMON_BUILD_ENABLED),y)
ifeq ($(call has,VIRTIO_NET_USER),1)
VIRTIO_NET_USER_BUILD_ENABLED := y
endif
endif

# At least one usable backend is required for the virtio-net device.
VIRTIO_NET_BUILD_ENABLED := n
ifneq ($(filter y, \
    $(VIRTIO_NET_TAP_BUILD_ENABLED) \
    $(VIRTIO_NET_USER_BUILD_ENABLED)),)
VIRTIO_NET_BUILD_ENABLED := y
endif

# Track effective VirtIO-net feature settings in the configuration stamp.
EFFECTIVE_VNET_FEATURES := \
    VIRTIO_NET \
    VIRTIO_NET_TAP \
    VIRTIO_NET_USER

# User-mode networking is provided by minislirp.
MINISLIRP_DIR := src/minislirp
MINISLIRP_SRC_DIR := $(MINISLIRP_DIR)/src
MINISLIRP_MAKEFILE := $(MINISLIRP_SRC_DIR)/Makefile
MINISLIRP_LIB := $(MINISLIRP_SRC_DIR)/libslirp.a
MINISLIRP_CONFIG_STAMP := $(OUT)/.minislirp-config
MINISLIRP_CFLAGS :=

# Generated minislirp files are removed by the top-level clean target even if
# the currently selected configuration does not enable the user backend.
VIRTIO_NET_CLEAN_FILES := \
    $(MINISLIRP_LIB) \
    $(MINISLIRP_SRC_DIR)/*.o \
    $(MINISLIRP_CONFIG_STAMP)

ifeq ($(VIRTIO_NET_USER_BUILD_ENABLED),y)

CFLAGS += -I$(MINISLIRP_SRC_DIR)
LDFLAGS += $(MINISLIRP_LIB)

ifeq ($(UNAME_S),Darwin)
MINISLIRP_CFLAGS := MYCFLAGS="-D_DARWIN_C_SOURCE"
LDFLAGS += -lresolv
endif

$(MINISLIRP_CONFIG_STAMP): FORCE | $(OUT)
	$(Q){ \
		printf 'CC=%s\n' '$(CC)'; \
		printf 'UNAME_S=%s\n' '$(UNAME_S)'; \
		printf 'MINISLIRP_CFLAGS=%s\n' '$(MINISLIRP_CFLAGS)'; \
	} > $@.tmp
	$(Q)if ! cmp -s $@.tmp $@ 2>/dev/null; then \
		mv $@.tmp $@; \
	else \
		rm -f $@.tmp; \
	fi

$(MINISLIRP_MAKEFILE):
	$(Q)git submodule update --init $(MINISLIRP_DIR)

# Always recurse into minislirp when its archive is needed. Its own Makefile
# handles source changes, while the configuration stamp forces a clean rebuild
# when compiler or host-specific build settings change.
$(MINISLIRP_LIB): $(MINISLIRP_MAKEFILE) $(MINISLIRP_CONFIG_STAMP) FORCE
	$(Q)if [ ! -f "$@" ] || \
	    [ "$(MINISLIRP_CONFIG_STAMP)" -nt "$@" ]; then \
		$(MAKE) -C $(MINISLIRP_SRC_DIR) clean; \
	fi
	$(Q)$(MAKE) -C $(MINISLIRP_SRC_DIR) CC="$(CC)" $(MINISLIRP_CFLAGS)

$(OUT)/devices/slirp.o: $(MINISLIRP_LIB)
$(BIN): $(MINISLIRP_LIB)

endif

# Reject a build that enables virtio-net without a host backend. Keep this as a
# build prerequisite rather than a parse-time error so maintenance targets such
# as clean and config remain usable with an incomplete configuration.
check-vnet-config:
	$(Q)if [ "$(SYSTEM_MMIO)" = "1" ] && \
	    [ "$(call has,VIRTIO_NET)" = "1" ] && \
	    [ "$(VIRTIO_NET_BUILD_ENABLED)" != "y" ]; then \
		echo "Error: VirtIO network device requires a backend supported by this host and compiler." >&2; \
		exit 1; \
	fi

$(BIN): | check-vnet-config

.PHONY: check-vnet-config

endif # _MK_VIRTIO_NET_INCLUDED
