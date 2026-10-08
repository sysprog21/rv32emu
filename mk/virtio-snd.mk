# VirtIO sound device support
#
# This file contains build rules and host dependency constraints specific to
# virtio-snd.

ifndef _MK_VIRTIO_SND_INCLUDED
_MK_VIRTIO_SND_INCLUDED := 1

# Legacy ENABLE_VIRTIO_SND may force the feature on after Kconfig dependency
# resolution. Reject an explicit enable when the selected build mode cannot
# support virtio-snd.
check-vsnd-config:
	$(Q)if [ "$(ENABLE_VIRTIO_SND_NORMALIZED)" = "y" ]; then \
		if [ "$(SYSTEM_MMIO)" != "1" ]; then \
			echo "Error: VirtIO sound requires kernel system emulation." >&2; \
			exit 1; \
		fi; \
		if [ "$(CC_IS_EMCC)" = "1" ]; then \
			echo "Error: VirtIO sound is not supported with Emscripten." >&2; \
			exit 1; \
		fi; \
	fi; \
	if [ "$(SYSTEM_MMIO)" = "1" ] && \
	   [ "$(CC_IS_EMCC)" != "1" ] && \
	   [ "$(CONFIG_VIRTIO_SND)" = "y" ] && \
	   [ "$(call pkg-exists,portaudio-2.0)" != "y" ]; then \
		echo "Error: VirtIO sound requires PortAudio (portaudio-2.0)." >&2; \
		exit 1; \
	fi

# Kconfig normally enforces the system/compiler constraints. Mirror them here
# because legacy ENABLE_* overrides can change those settings without rerunning
# Kconfig dependency resolution.
ifeq ($(SYSTEM_MMIO),1)
ifneq ($(CC_IS_EMCC),1)

CFLAGS += -DRV32_FEATURE_VIRTIO_SND=$(call config-to-feature,VIRTIO_SND)

ifeq ($(CONFIG_VIRTIO_SND),y)
ifneq ($(NEEDS_CONFIG),)
PORTAUDIO_CFLAGS := $(call dep,cflags,portaudio-2.0)
PORTAUDIO_LIBS := $(call dep,libs,portaudio-2.0)

$(OUT)/devices/virtio-snd.o: CFLAGS += $(PORTAUDIO_CFLAGS)

LDFLAGS += $(PORTAUDIO_LIBS) -pthread -lm
endif
endif

else
CFLAGS += -DRV32_FEATURE_VIRTIO_SND=0
endif
else
CFLAGS += -DRV32_FEATURE_VIRTIO_SND=0
endif

$(OUT)/devices/virtio-snd.o: | check-vsnd-config
$(BIN): | check-vsnd-config

.PHONY: check-vsnd-config

endif # _MK_VIRTIO_SND_INCLUDED
