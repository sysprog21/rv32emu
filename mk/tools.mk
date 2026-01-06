# Development tools (histogram generator, Linux image builder)

ifndef _MK_TOOLS_INCLUDED
_MK_TOOLS_INCLUDED := 1

HIST_BIN := $(OUT)/rv_histogram

# On macOS, gcc-14 requires linking symbols from emulate.o, syscall.o, syscall_sdl.o, io.o, and log.o.
# However, these symbols are not actually used in rv_histogram, they are only needed to pass the build.
#
# riscv.o and map.o are dependencies of 'elf.o', not 'rv_histogram'. But, they are also needed to pass
# the build.
HIST_OBJS := \
	riscv.o \
	utils.o \
	map.o \
	elf.o \
	decode.o \
	mpool.o \
	utils.o \
	emulate.o \
	syscall.o \
	syscall_sdl.o \
	io.o \
	log.o \
	rv_histogram.o

HIST_OBJS := $(addprefix $(OUT)/, $(HIST_OBJS))
deps += $(HIST_OBJS:%.o=%.o.d)

$(OUT)/%.o: tools/%.c | $(OUT)
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) -Wno-missing-field-initializers -Isrc -c -MMD -MF $@.d $<

# GDBSTUB is disabled to exclude the mini-gdb during compilation.
$(HIST_BIN): $(HIST_OBJS)
	$(VECHO) "  LD\t$@\n"
	$(Q)$(CC) -o $@ -D RV32_FEATURE_GDBSTUB=0 $^ $(LDFLAGS)

TOOLS_BIN += $(HIST_BIN)

# Build Linux image
LINUX_IMAGE_SRC = $(BUILDROOT_DATA) $(LINUX_DATA) $(SIMPLEFS_DATA)
build-linux-image: $(LINUX_IMAGE_SRC)
	$(Q)./tools/build-linux-image.sh
	$(Q)$(PRINTF) "Build done.\n"

# Code Formatting (tool detection deferred to recipe)
# Uses find -print0 | xargs -0 for safe handling of paths with special characters
format:
	$(Q)CLANG_FORMAT=$$(which clang-format-20 2>/dev/null); \
	SHFMT=$$(which shfmt 2>/dev/null); \
	DTSFMT=$$(which dtsfmt 2>/dev/null); \
	BLACK=$$(which black 2>/dev/null); \
	if [ -z "$$CLANG_FORMAT" ]; then echo "clang-format-20 not found."; exit 1; fi && \
	if [ -z "$$SHFMT" ]; then echo "shfmt not found."; exit 1; fi && \
	if [ -z "$$DTSFMT" ]; then echo "dtsfmt not found."; exit 1; fi && \
	if [ -z "$$BLACK" ]; then echo "black not found."; exit 1; fi && \
	SUBMODULES=$$(git config --file .gitmodules --get-regexp path 2>/dev/null | awk '{ print $$2 }') && \
	PRUNE_PATHS="./$(OUT)" && \
	for subm in $$SUBMODULES; do PRUNE_PATHS="$$PRUNE_PATHS ./$$subm"; done && \
	PRUNE_ARGS=$$(echo "$$PRUNE_PATHS" | tr ' ' '\n' | sed 's/^/-path /;s/$$/ -o/' | tr '\n' ' ' | sed 's/ -o $$//') && \
	find . \( $$PRUNE_ARGS \) -prune -o -name '*.[ch]' -print0 | xargs -0 $$CLANG_FORMAT -i && \
	find . \( $$PRUNE_ARGS \) -prune -o -name '*.sh' -print0 | xargs -0 $$SHFMT -w && \
	find . \( $$PRUNE_ARGS \) -prune -o \( -name '*.dts' -o -name '*.dtsi' \) -print0 | xargs -0 -I{} $$DTSFMT {} && \
	find . \( $$PRUNE_ARGS \) -prune -o \( -name '*.py' -o -name '*.pyi' \) -print0 | xargs -0 $$BLACK --quiet
	$(Q)$(call notice, All files formatted.)

.PHONY: build-linux-image format

endif # _MK_TOOLS_INCLUDED
