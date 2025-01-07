# Peripherals for system emulation
ifeq ($(call has, SYSTEM), 1)

CFLAGS += -Isrc/dtc/libfdt
LIBFDT_HACK := $(shell git submodule update --init src/dtc)

DEV_SRC := src/devices

DTC ?= dtc
BUILD_DTB := $(OUT)/minimal.dtb
$(BUILD_DTB): $(DEV_SRC)/minimal.dts
	$(VECHO) " DTC\t$@\n"
	$(Q)$(DTC) $^ -o $@

BIN_TO_C := $(OUT)/bin2c
$(BIN_TO_C): tools/bin2c.c
	$(Q)$(CC) -Wall -o $@ $^

BUILD_DTB2C := src/minimal_dtb.h
$(BUILD_DTB2C): $(BIN_TO_C) $(BUILD_DTB)
	$(BIN_TO_C) $(BUILD_DTB) > $(BUILD_DTB2C)

$(DEV_OUT)/%.o: $(DEV_SRC)/%.c $(deps_emcc)
	$(Q)mkdir -p $(DEV_OUT)
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) $(CFLAGS_emcc) -c -MMD -MF $@.d $<
DEV_OBJS := $(patsubst $(DEV_SRC)/%.c, $(DEV_OUT)/%.o, $(wildcard $(DEV_SRC)/*.c))
deps := $(DEV_OBJS:%.o=%.o.d)

OBJS_EXT += system.o
OBJS_EXT += dtc/libfdt/fdt.o dtc/libfdt/fdt_ro.o dtc/libfdt/fdt_rw.o

# system target execution by using default dependencies
LINUX_IMAGE_DIR := linux-image
system_action := ($(BIN) -k $(OUT)/$(LINUX_IMAGE_DIR)/Image -i $(OUT)/$(LINUX_IMAGE_DIR)/rootfs.cpio)
system_deps += artifact $(BUILD_DTB) $(BUILD_DTB2C) $(BIN)
system: $(system_deps)
	$(system_action)

endif
