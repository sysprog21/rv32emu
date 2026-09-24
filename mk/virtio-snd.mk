# VirtIO sound device support
#
# This file contains build rules and host dependency constraints specific to
# virtio-snd.

ifndef _MK_VIRTIO_SND_INCLUDED
_MK_VIRTIO_SND_INCLUDED := 1

# Reject an explicitly forced virtio-snd configuration when the selected
# build mode cannot support it. Kconfig-derived virtio-snd settings may become
# stale when another legacy ENABLE_* override changes a dependency; those are
# excluded from unsupported build modes below instead of treated as an error.
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
	   [ "$(call has,VIRTIO_SND)" = "1" ] && \
	   [ "$(call pkg-exists,portaudio-2.0)" != "y" ]; then \
		echo "Error: VirtIO sound requires PortAudio (portaudio-2.0)." >&2; \
		exit 1; \
	fi

ifeq ($(SYSTEM_MMIO),1)
ifneq ($(CC_IS_EMCC),1)
ifeq ($(call has,VIRTIO_SND),1)
ifneq ($(NEEDS_CONFIG),)
PORTAUDIO_CFLAGS := $(call dep,cflags,portaudio-2.0)
PORTAUDIO_LIBS := $(call dep,libs,portaudio-2.0)
$(OUT)/devices/virtio-snd.o: CFLAGS += $(PORTAUDIO_CFLAGS)
LDFLAGS += $(PORTAUDIO_LIBS) -pthread -lm
endif
endif
endif
endif

$(OUT)/devices/virtio-snd.o: | check-vsnd-config
$(BIN): | check-vsnd-config

.PHONY: check-vsnd-config

endif # _MK_VIRTIO_SND_INCLUDED
