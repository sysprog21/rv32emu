HIST_BIN := $(OUT)/rv_histogram

# FIXME: riscv.o and map.o are dependencies of 'elf.o', not 'rv_histogram'.
HIST_OBJS := \
	riscv.o \
	utils.o \
	map.o \
	elf.o \
	decode.o \
	mpool.o \
	utils.o \
	rv_histogram.o

HIST_OBJS := $(addprefix $(OUT)/, $(HIST_OBJS))
deps += $(HIST_OBJS:%.o=%.o.d)

$(OUT)/%.o: tools/%.c
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) -Wno-missing-field-initializers -Isrc -c -MMD -MF $@.d $<

# GDBSTUB is disabled to exclude the mini-gdb during compilation.
$(HIST_BIN): $(HIST_OBJS)
	$(VECHO) "  LD\t$@\n"
	$(Q)$(CC) -o $@ -D RV32_FEATURE_GDBSTUB=0 $^ $(LDFLAGS)

TOOLS_BIN += $(HIST_BIN)

# Build Linux image
LINUX_IMAGE_SRC = $(BUILDROOT_DATA) $(LINUX_DATA)
build-linux-image: $(LINUX_IMAGE_SRC)
	$(Q)./tools/build-linux-image.sh
	$(Q)$(PRINTF) "Build done.\n"
