# Peripherals for system emulation
ifeq ($(call has, SYSTEM), 1)

DEV_SRC := src/devices

DTC ?= dtc
$(OUT)/minimal.dtb: $(DEV_SRC)/minimal.dts
	$(VECHO) " DTC\t$@\n"
	$(Q)$(DTC) $^ -o $@
BUILD_DTB := $(OUT)/minimal.dtb

$(DEV_OUT)/%.o: $(DEV_SRC)/%.c $(deps_emcc)
	$(Q)mkdir -p $(DEV_OUT)
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) $(CFLAGS_emcc) -c -MMD -MF $@.d $<
DEV_OBJS := $(patsubst $(DEV_SRC)/%.c, $(DEV_OUT)/%.o, $(wildcard $(DEV_SRC)/*.c))
deps := $(DEV_OBJS:%.o=%.o.d)

OBJS_EXT += system.o

# system target execution by using default dependencies
LINUX_IMAGE_DIR := linux-image
system_action := ($(BIN) -k $(OUT)/$(LINUX_IMAGE_DIR)/Image -i $(OUT)/$(LINUX_IMAGE_DIR)/rootfs.cpio -b $(OUT)/minimal.dtb)
system_deps += artifact $(BUILD_DTB) $(BIN)
system: $(system_deps)
	$(system_action)

endif
