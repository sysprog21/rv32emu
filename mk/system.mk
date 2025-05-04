# Peripherals for system emulation
ifeq ($(call has, SYSTEM), 1)

CFLAGS += -Isrc/dtc/libfdt
LIBFDT_HACK := $(shell [ -d src/dtc/.git ] || \
	git clone --depth=1 https://git.kernel.org/pub/scm/utils/dtc/dtc.git src/dtc)

DEV_SRC := src/devices

DTC ?= dtc
BUILD_DTB := $(OUT)/minimal.dtb
$(BUILD_DTB): $(DEV_SRC)/minimal.dts
	$(VECHO) " DTC\t$@\n"
	$(Q)$(CC) -nostdinc -E -P -x assembler-with-cpp -undef $(CFLAGS_dt) $^ | $(DTC) - > $@

# Assume the system has either GCC or Clang
NATIVE_CC := $(shell which gcc || which clang)

BIN_TO_C := $(OUT)/bin2c
$(BIN_TO_C): tools/bin2c.c
# emcc generates wasm but not executable, so fallback to use GCC or Clang
ifeq ("$(CC_IS_EMCC)", "1")
	$(Q)$(NATIVE_CC) -Wall -o $@ $^
else
	$(Q)$(CC) -Wall -o $@ $^
endif

BUILD_DTB2C := src/minimal_dtb.h
$(BUILD_DTB2C): $(BIN_TO_C) $(BUILD_DTB)
	$(VECHO) "  BIN2C\t$@\n"
	$(Q)$(BIN_TO_C) $(BUILD_DTB) > $@

$(DEV_OUT)/%.o: $(DEV_SRC)/%.c $(deps_emcc)
	$(Q)mkdir -p $(DEV_OUT)
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) $(CFLAGS_emcc) -c -MMD -MF $@.d $<
DEV_OBJS := $(patsubst $(DEV_SRC)/%.c, $(DEV_OUT)/%.o, $(wildcard $(DEV_SRC)/*.c))
deps := $(DEV_OBJS:%.o=%.o.d)

OBJS_EXT += system.o
OBJS_EXT += dtc/libfdt/fdt.o dtc/libfdt/fdt_ro.o dtc/libfdt/fdt_rw.o dtc/libfdt/fdt_wip.o

# system target execution by using default dependencies
LINUX_IMAGE_DIR := linux-image
system_action := ($(BIN) -k $(OUT)/$(LINUX_IMAGE_DIR)/Image -i $(OUT)/$(LINUX_IMAGE_DIR)/rootfs.cpio)
system_deps += artifact $(BUILD_DTB) $(BUILD_DTB2C) $(BIN)
system: $(system_deps)
	$(system_action)

endif
