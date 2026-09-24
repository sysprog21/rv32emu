# VirtIO sound device support
#
# This file contains build rules and host dependency constraints specific to
# virtio-snd.

ifndef _MK_VIRTIO_SND_INCLUDED
_MK_VIRTIO_SND_INCLUDED := 1

# VirtIO sound is only available for kernel system emulation.
VIRTIO_SND_SYSTEM_MMIO := 0
ifeq ($(call has,SYSTEM),1)
ifneq ($(call has,ELF_LOADER),1)
VIRTIO_SND_SYSTEM_MMIO := 1
endif
endif

# CONFIG_* values may be overridden by legacy ENABLE_* flags after loading
# .config, so keep the effective host dependency check in the build system as
# well instead of relying only on the Kconfig dependency graph.
PORTAUDIO_AVAILABLE := n
ifeq ($(SKIP_DEPS_CHECK),)
ifeq ($(call pkg-exists,portaudio-2.0),y)
PORTAUDIO_AVAILABLE := y
endif
endif

VIRTIO_SND_BUILD_ENABLED := n
ifeq ($(VIRTIO_SND_SYSTEM_MMIO),1)
ifeq ($(call has,VIRTIO_SND),1)
ifneq ($(CC_IS_EMCC),1)
ifeq ($(PORTAUDIO_AVAILABLE),y)
VIRTIO_SND_BUILD_ENABLED := y
endif
endif
endif
endif

# The source code uses RV32_HAS(VIRTIO_SND), so expose the effective build
# state rather than the raw CONFIG_VIRTIO_SND value.
VIRTIO_SND_FEATURE := $(if $(filter y,$(VIRTIO_SND_BUILD_ENABLED)),1,0)
CFLAGS += -DRV32_FEATURE_VIRTIO_SND=$(VIRTIO_SND_FEATURE)

PORTAUDIO_CFLAGS :=
PORTAUDIO_LIBS :=

ifeq ($(VIRTIO_SND_BUILD_ENABLED),y)
PORTAUDIO_CFLAGS := $(call dep,cflags,portaudio-2.0)
PORTAUDIO_LIBS := $(call dep,libs,portaudio-2.0)

$(OUT)/devices/virtio-snd.o: CFLAGS += $(PORTAUDIO_CFLAGS)

LDFLAGS += $(PORTAUDIO_LIBS) -pthread -lm
endif

# Track values which can change the effective sound build without changing the
# saved Kconfig file.
EFFECTIVE_VSND_FEATURES := VIRTIO_SND
EFFECTIVE_VSND_VARS := \
    PORTAUDIO_AVAILABLE \
    VIRTIO_SND_BUILD_ENABLED

# Reject legacy ENABLE_VIRTIO_SND=1 when the host cannot actually build the
# device. Keep this as a build prerequisite so clean/config targets remain
# usable on hosts without PortAudio.
check-vsnd-config:
	$(Q)if [ "$(VIRTIO_SND_SYSTEM_MMIO)" = "1" ] && \
	    [ "$(call has,VIRTIO_SND)" = "1" ] && \
	    [ "$(VIRTIO_SND_BUILD_ENABLED)" != "y" ]; then \
		echo "Error: VirtIO sound device requires PortAudio (portaudio-2.0)." >&2; \
		exit 1; \
	fi

$(BIN): | check-vsnd-config

.PHONY: check-vsnd-config

endif # _MK_VIRTIO_SND_INCLUDED
