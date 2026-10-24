# ACT4-based RISC-V architectural tests.

ifndef _MK_RISCV_ARCH_TEST_INCLUDED
_MK_RISCV_ARCH_TEST_INCLUDED := 1

ARCH_TEST_DIR ?= tests/riscv-arch-test
ARCH_TEST_DEFAULT_CONFIG := tests/arch-test-target/rv32emu-rv32imafc/test_config.yaml
ARCH_TEST_CONFIG ?= $(ARCH_TEST_DEFAULT_CONFIG)
ARCH_TEST_NAME ?= $(notdir $(patsubst %/,%,$(dir $(ARCH_TEST_CONFIG))))
ARCH_TEST_WORKDIR ?= $(OUT)/arch-test
# An empty filter lets ACT4 select every test applicable to the DUT config.
ARCH_TEST_EXTENSIONS ?=
ARCH_TEST_SAIL_BIN_DIR ?= $(OUT)/act-tools/sail-riscv-Linux-$(UNAME_M)/bin

# The bundled profile must describe the emulator being tested. Custom DUT
# configurations may select a different extension set.
ifeq ($(abspath $(ARCH_TEST_CONFIG)),$(abspath $(ARCH_TEST_DEFAULT_CONFIG)))
ARCH_TEST_REQUIRED_FEATURES := RV32I EXT_M EXT_A EXT_F EXT_C Zicsr Zifencei Zba Zbb Zbc Zbs
ARCH_TEST_MISSING_FEATURES := $(foreach feature,$(ARCH_TEST_REQUIRED_FEATURES),$(if $(filter y,$(CONFIG_$(feature))),,$(feature)))
endif

# Keep cached reference signatures, but replace the executable selection so
# tests removed by a suite upgrade or extension filter cannot run as stale ELFs.
arch-test-elfs:
	$(Q)git submodule update --init $(ARCH_TEST_DIR)
	$(Q)compiler=$$(sed -n "s/^compiler_exe:[[:space:]]*[\"']\{0,1\}\([^\"'#[:space:]]*\).*/\1/p" \
	    $(ARCH_TEST_CONFIG)); \
	[ -z "$$compiler" ] || command -v "$$compiler" > /dev/null || { \
	    echo "ACT4 builds with $$compiler (compiler_exe in $(ARCH_TEST_CONFIG)); put it on PATH." >&2; \
	    exit 1; }
	$(Q)$(RM) -r -- "$(ARCH_TEST_WORKDIR)/$(ARCH_TEST_NAME)/elfs"
	$(Q)PATH="$(abspath $(ARCH_TEST_SAIL_BIN_DIR)):$$PATH" $(MAKE) -C $(ARCH_TEST_DIR) \
		CONFIG_FILES=$(abspath $(ARCH_TEST_CONFIG)) \
		WORKDIR=$(abspath $(ARCH_TEST_WORKDIR)) \
		EXTENSIONS=$(ARCH_TEST_EXTENSIONS)

arch-test-check-config:
ifeq ($(CONFIG_SYSTEM),y)
	$(Q)echo "arch-test requires a user-mode build. Run 'make cleanconfig && make ci_defconfig && make' first." >&2
	$(Q)exit 1
endif
ifneq ($(CONFIG_ARCH_TEST),y)
	$(Q)echo "arch-test requires CONFIG_ARCH_TEST=y so rv32emu can detect tohost writes. Run 'make cleanconfig && make ci_defconfig && make' first." >&2
	$(Q)exit 1
endif
ifneq ($(strip $(ARCH_TEST_MISSING_FEATURES)),)
	$(Q)echo "The bundled ACT4 profile requires enabled features: $(strip $(ARCH_TEST_MISSING_FEATURES)). Use ci_defconfig or select a matching ARCH_TEST_CONFIG." >&2
	$(Q)exit 1
endif

arch-test: arch-test-check-config
	$(Q)$(MAKE) CC="$(CC)" $(BIN) arch-test-elfs
	$(Q)tools/run-arch-test-elfs.sh $(BIN) $(ARCH_TEST_WORKDIR)/$(ARCH_TEST_NAME)/elfs $(ARCH_TEST_EXTENSIONS)

.PHONY: arch-test-elfs arch-test arch-test-check-config

arch-test-check-toolchain:
ifeq ($(CROSS_COMPILE),)
	$(Q)echo "The ACT4 regressions need a RISC-V cross compiler. Install one or set CROSS_COMPILE." >&2
	$(Q)exit 1
endif

.PHONY: arch-test-check-toolchain

check-arch-test-runner:
	$(Q)python3 tests/arch-test-target/test-runner.py

.PHONY: check-arch-test-runner

check-arch-test-tohost: arch-test-check-config arch-test-check-toolchain $(BIN) | $(OUT)
	$(Q)set -eu; for result in 1 3 513; do \
	    $(CROSS_COMPILE)gcc -march=rv32i -mabi=ilp32 -nostdlib -static \
	        -DTEST_RESULT=$$result tests/arch-test-target/regressions/tohost.S -o $(OUT)/act-tohost.elf; \
	    status=0; \
	    timeout --kill-after=5 10 $(BIN) -q -a $(OUT)/act-tohost.signature \
	        $(OUT)/act-tohost.elf || status=$$?; \
	    expected=1; test $$result -ne 1 || expected=0; \
	    test $$status -eq $$expected || exit 1; \
	done

.PHONY: check-arch-test-tohost

check-arch-test-misalign: arch-test-check-config arch-test-check-toolchain $(BIN) | $(OUT)
ifeq ($(CONFIG_EXT_F)$(CONFIG_EXT_C),yy)
	$(Q)$(CROSS_COMPILE)gcc -march=rv32ifc -mabi=ilp32 -nostdlib -static \
	    tests/arch-test-target/regressions/misalign.S -o $(OUT)/act-misalign.elf
	$(Q)timeout --kill-after=5 10 $(BIN) -q -a $(OUT)/act-misalign.signature \
	    $(OUT)/act-misalign.elf
else
	$(error check-arch-test-misalign requires CONFIG_EXT_F=y and CONFIG_EXT_C=y)
endif

.PHONY: check-arch-test-misalign

endif # _MK_RISCV_ARCH_TEST_INCLUDED
