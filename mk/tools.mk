HIST_BIN := $(OUT)/rv_histogram

# FIXME: riscv.o and map.o are dependency of elf.o not the tool
HIST_OBJS := \
	riscv.o \
	map.o \
	elf.o \
	decode.o
HIST_OBJS := $(addprefix $(OUT)/, $(HIST_OBJS))

HIST_MAIN_OBJ := rv_histogram.o
HIST_MAIN_OBJ := $(addprefix $(OUT)/, $(HIST_MAIN_OBJ))

$(OUT)/%.o: tools/%.c
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) -Isrc -c -MMD -MF $@.d $<

# GDBSTUB is diabled for excluding the mini-gdb during compilation
$(HIST_BIN): $(HIST_OBJS) $(HIST_MAIN_OBJ)
	$(VECHO) "  LD\t$@\n"
	$(Q)$(CC) -o $@ $^ $(LDFLAGS) -D RV32_FEATURE_GDBSTUB=0

tool: $(HIST_BIN)
