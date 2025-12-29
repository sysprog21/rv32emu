riscof-check:
	$(Q)if [ "$(shell pip show riscof 2>&1 | head -n 1 | cut -d' ' -f1)" = "WARNING:" ]; then \
		$(PRINTF) "Run 'pip3 install -r requirements.txt' to install dependencies.\n"; \
		exit 1; \
	fi

ARCH_TEST_DIR ?= tests/riscv-arch-test
ARCH_TEST_SUITE ?= $(ARCH_TEST_DIR)/riscv-test-suite
export RISCV_TARGET := tests/arch-test-target
export TARGETDIR := $(shell pwd)
export RISCV_DEVICE ?= IMACFZicsrZifencei
# Use device-specific work directory to enable parallel arch-test execution
export WORK := $(TARGETDIR)/build/arch-test-$(RISCV_DEVICE)

# SKIP_PREREQ=1 skips artifact/git/copy steps (used when pre-fetched for parallel execution)
SKIP_PREREQ ?= 0

ifeq ($(RISCV_DEVICE),FCZicsr)
ARCH_TEST_SUITE := tests/rv32fc-test-suite
endif

# Dependencies for arch-test (skipped when SKIP_PREREQ=1 for parallel execution)
# When SKIP_PREREQ=1, we don't depend on $(BIN) to avoid race conditions in parallel make.
# The binary must already be built - we verify it exists in the recipe.
ifeq ($(SKIP_PREREQ),1)
ARCH_TEST_DEPS := riscof-check
else
ARCH_TEST_DEPS := riscof-check $(BIN) artifact
endif

arch-test: $(ARCH_TEST_DEPS)
ifeq ($(CROSS_COMPILE),)
	$(error GNU Toolchain for RISC-V is required to build architecture tests. Please check package installation)
endif
ifeq ($(SKIP_PREREQ),1)
	$(Q)test -x $(BIN) || \
		{ echo "Error: SKIP_PREREQ=1 requires pre-built binary. Run 'make' first."; exit 1; }
	$(Q)test -x tests/arch-test-target/sail_cSim/riscv_sim_RV32 || \
		{ echo "Error: SKIP_PREREQ=1 requires pre-fetched sail binary. Run 'make artifact' first."; exit 1; }
	$(Q)test -d $(ARCH_TEST_DIR) || \
		{ echo "Error: SKIP_PREREQ=1 requires submodule. Run 'git submodule update --init tests/riscv-arch-test/' first."; exit 1; }
else
	git submodule update --init $(dir $(ARCH_TEST_DIR))
	$(Q)cp $(OUT)/rv32emu-prebuilt-sail-$(HOST_PLATFORM) tests/arch-test-target/sail_cSim/riscv_sim_RV32
	$(Q)chmod +x tests/arch-test-target/sail_cSim/riscv_sim_RV32
endif
	$(Q)python3 -B $(RISCV_TARGET)/setup.py --riscv_device=$(RISCV_DEVICE) --hw_data_misaligned_support=$(hw_data_misaligned_support) --work_dir=$(WORK)
	$(Q)grep -q '^\[RISCOF\]' $(WORK)/config.ini || \
		{ echo "Error: config.ini missing RISCOF section. Contents:"; cat $(WORK)/config.ini; exit 1; }
	$(Q)riscof run --no-clean --work-dir=$(WORK) \
			--config=$(WORK)/config.ini \
			--suite=$(ARCH_TEST_SUITE) \
			--env=$(ARCH_TEST_DIR)/riscv-test-suite/env

.PHONY: riscof-check arch-test
