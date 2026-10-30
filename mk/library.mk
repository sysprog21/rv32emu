# Native user-mode embedding interface. Clients compile using include/ only.
LIBRV32EMU := $(OUT)/librv32emu.a
LIBRV32EMU_OBJS := $(filter-out $(OUT)/main.o,$(OBJS)) $(OUT)/api.o
# api.o is outside OBJS, so give it the same prerequisites and header tracking.
deps += $(OUT)/api.o.d
ifeq ($(CONFIG_EXT_F),y)
$(OUT)/api.o: $(SOFTFLOAT_LIB)
endif
ifeq ($(CONFIG_GDBSTUB),y)
$(OUT)/api.o: $(GDBSTUB_LIB)
endif
# The main Makefile selects a matching llvm-ar only with floating point; an
# archive of Clang LTO objects needs it regardless.
LIBRARY_RESDIR = $(shell $(CC) -print-resource-dir 2>/dev/null)
LIBRARY_AR_CLANG = $(if $(LIBRARY_RESDIR),$(realpath $(LIBRARY_RESDIR)/../../../bin/llvm-ar))
LIBRARY_AR = $(or $(and $(filter-out y,$(CONFIG_EXT_F)),$(filter 1,$(CC_IS_CLANG)),$(LIBRARY_AR_CLANG)),$(AR))
.PHONY: library example-embed check-api
# Clients linked by the generic rule below; test-api has its own object name.
LIBRARY_CLIENTS := embed test-api-cxx api-run-guest
LIBRARY_CLEAN_FILES := $(LIBRV32EMU) $(OUT)/rv32emu.pc $(OUT)/test-api \
    $(LIBRARY_CLIENTS:%=$(OUT)/%) $(LIBRARY_CLIENTS:%=$(OUT)/%.o) \
    $(OUT)/api-symbols $(OUT)/api-test-api.o

ifeq ($(CONFIG_SYSTEM)$(CONFIG_BUILD_WASM),)
check tests: check-api

library: $(LIBRV32EMU)

$(LIBRV32EMU): $(LIBRV32EMU_OBJS) $(EFFECTIVE_CONFIG_STAMP)
	$(Q)rm -f $@
	$(Q)$(LIBRARY_AR) rcs $@ $(LIBRV32EMU_OBJS)

# Keep host client compilation separate from the core's LTO link flags.
# In particular, Clang ThinLTO clients need not share core split-LTO settings.
$(OUT)/embed.o: examples/embed.c include/rv32emu.h $(EFFECTIVE_CONFIG_STAMP) | $(OUT)
	$(Q)$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude -c $< -o $@

$(OUT)/api-%.o: tests/api/%.c include/rv32emu.h $(EFFECTIVE_CONFIG_STAMP) | $(OUT)
	$(Q)$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude $(API_TEST_CFLAGS) -c $< -o $@

ifeq ($(CONFIG_SDL),y)
$(OUT)/api-test-api.o: API_TEST_CFLAGS += -DAPI_TEST_SDL $(SDL2_CFLAGS)
endif
ifeq ($(CONFIG_RV32E),y)
$(OUT)/api-test-api.o: API_TEST_CFLAGS += -DAPI_TEST_RV32E
endif

# Link every client with the core compiler so LTO archives resolve.
$(LIBRARY_CLIENTS:%=$(OUT)/%): $(OUT)/%: $(OUT)/%.o $(LIBRV32EMU)
	$(Q)$(CC) $< $(LIBRV32EMU) $(LDFLAGS) -o $@
$(OUT)/test-api: $(OUT)/api-test-api.o $(LIBRV32EMU)
	$(Q)$(CC) $< $(LIBRV32EMU) $(LDFLAGS) -o $@
$(OUT)/test-api-cxx.o: tests/api/test-cxx.cc include/rv32emu.h | $(OUT)
	$(Q)$(CXX) -std=c++11 -Wall -Wextra -Werror -Iinclude -c $< -o $@

example-embed: $(OUT)/embed

check-api: $(OUT)/test-api $(OUT)/test-api-cxx
	$(Q)$(OUT)/test-api $(OUT)/api-guest.elf
	$(Q)$(OUT)/test-api-cxx

# Build-tree pkg-config metadata includes configuration-specific dependencies.
# Paths refer to this checkout; regenerate after moving it.
library: $(OUT)/rv32emu.pc
$(OUT)/rv32emu.pc: $(EFFECTIVE_CONFIG_STAMP) Makefile mk/library.mk
	$(Q){ \
	    echo 'Name: rv32emu'; \
	    echo 'Description: Embedded RV32 user-mode emulator'; \
	    echo 'Version: 0.1'; \
	    echo 'Cflags: -I$(abspath include)'; \
	    echo 'Libs: -L$(abspath $(OUT)) -lrv32emu'; \
	    echo 'Libs.private: $(foreach flag,$(LDFLAGS),$(if $(filter %.a,$(flag)),$(abspath $(flag)),$(flag)))'; \
	} > $@

# Run established guest regressions through the public interface as well as
# the CLI, keeping their existing ISA/toolchain gates and output oracles.
API_GUEST_CHECKS := $(filter check-load-x0 check-register-loop \
    check-const-arith check-const-shift check-const-operand check-lrsc \
    check-jit-muldiv check-jit-compare check-jit-shift check-jit-cycles \
    check-jit-fencei,$(GUEST_ASM_CHECK_TARGETS))
define api-guest-check
.PHONY: api-$(1)
api-$(1): $(1) $(OUT)/api-run-guest
	$$(call check-test,,$$(OUT)/$(patsubst check-%,%,$(1)),public API $(1),uniq,$$(EXPECTED_$(patsubst check-%,%,$(1))),$$(OUT)/api-run-guest)
endef
$(foreach target,$(API_GUEST_CHECKS),$(eval $(call api-guest-check,$(target))))
check: $(addprefix api-,$(API_GUEST_CHECKS))

# ELF visibility test: even a host exporting all its symbols must not publish
# our core or bundled SoftFloat internals. The archive still has cross-TU
# symbols for static linking; visibility is not archive symbol localization.
ifeq ($(UNAME_S),Linux)
.PHONY: check-api-symbols
# Match nm to the compiler so it can read the archive's LTO format.
API_NM ?= $(shell $(CC) -print-prog-name=$(if $(filter 1,$(CC_IS_CLANG)),llvm-nm,gcc-nm))
$(OUT)/api-symbols: $(OUT)/api-run-guest.o $(LIBRV32EMU)
	$(Q)$(CC) $< $(LIBRV32EMU) $(LDFLAGS) \
	    -Wl,--export-dynamic -o $@

check-api-symbols: $(OUT)/api-symbols tests/api/check-symbols.py $(T2C_COLLECTOR)
	$(Q)python3 tests/api/check-symbols.py $(API_NM) include/rv32emu.h $< \
	    $(if $(filter 1,$(T2C_ENABLED)),1,0) $(if $(T2C_COLLECTOR),$(T2C_COLLECTOR),-) \
	    $(LIBRV32EMU) $(SOFTFLOAT_LIB)

check-api: check-api-symbols
endif
else
library example-embed check-api:
	$(error The public library requires a native user-mode configuration)
endif
