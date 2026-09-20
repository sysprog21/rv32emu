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

# VirtIO networking is available only for kernel system emulation. The initial
# backend is Linux TAP, so the device is buildable only on Linux hosts.
# CONFIG_* values may be overridden by legacy ENABLE_* flags after loading
# .config, so mirror the Kconfig constraints in the effective build state.
VIRTIO_NET_COMMON_BUILD_ENABLED := n
ifeq ($(SYSTEM_MMIO),1)
ifeq ($(call has,VIRTIO_NET),1)
ifneq ($(CC_IS_EMCC),1)
ifeq ($(UNAME_S),Linux)
VIRTIO_NET_COMMON_BUILD_ENABLED := y
endif
endif
endif
endif

# Linux TAP backend.
VIRTIO_NET_TAP_BUILD_ENABLED := n
ifeq ($(VIRTIO_NET_COMMON_BUILD_ENABLED),y)
ifeq ($(call has,VIRTIO_NET_TAP),1)
VIRTIO_NET_TAP_BUILD_ENABLED := y
endif
endif

# The device requires at least one usable host backend.
VIRTIO_NET_BUILD_ENABLED := $(VIRTIO_NET_TAP_BUILD_ENABLED)

# Track effective VirtIO-net feature settings in the configuration stamp.
EFFECTIVE_VNET_FEATURES := \
    VIRTIO_NET \
    VIRTIO_NET_TAP

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
