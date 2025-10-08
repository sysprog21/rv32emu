riscof-check:
	$(Q)if [ "$(shell pip show riscof 2>&1 | head -n 1 | cut -d' ' -f1)" = "WARNING:" ]; then \
		$(PRINTF) "Run 'pip3 install -r requirements.txt' to install dependencies.\n"; \
		exit 1; \
	fi

ARCH_TEST_DIR ?= tests/riscv-arch-test
ARCH_TEST_SUITE ?= $(ARCH_TEST_DIR)/riscv-test-suite
export RISCV_TARGET := tests/arch-test-target
export TARGETDIR := $(shell pwd)
export WORK := $(TARGETDIR)/build/arch-test
export RISCV_DEVICE ?= IMACFZicsrZifencei

ifeq ($(RISCV_DEVICE),FCZicsr)
ARCH_TEST_SUITE := tests/rv32fc-test-suite
endif

arch-test: riscof-check $(BIN) artifact
ifeq ($(CROSS_COMPILE),)
	$(error GNU Toolchain for RISC-V is required to build architecture tests. Please check package installation)
endif
	git submodule update --init $(dir $(ARCH_TEST_DIR))
	$(Q)cp $(OUT)/rv32emu-prebuilt-sail-$(HOST_PLATFORM) tests/arch-test-target/sail_cSim/riscv_sim_RV32
	$(Q)chmod +x tests/arch-test-target/sail_cSim/riscv_sim_RV32
	$(Q)python3 -B $(RISCV_TARGET)/setup.py --riscv_device=$(RISCV_DEVICE) --hw_data_misaligned_support=$(hw_data_misaligned_support)
	$(Q)riscof run --work-dir=$(WORK) \
			--config=$(RISCV_TARGET)/config.ini \
			--suite=$(ARCH_TEST_SUITE) \
			--env=$(ARCH_TEST_DIR)/riscv-test-suite/env

.PHONY: riscof-check arch-test
