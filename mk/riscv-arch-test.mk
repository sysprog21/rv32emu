riscof-check:
	$(Q)if [ "$(shell pip show riscof 2>&1 | head -n 1 | cut -d' ' -f1)" = "WARNING:" ]; then \
	$(PRINTF) "Run 'pip3 install git+https://github.com/riscv/riscof.git@d38859f85fe407bcacddd2efcd355ada4683aee4' to install RISCOF\n"; \
	exit 1; \
	fi;

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
	$(Q)cp $(OUT)/sail_cSim/riscv_sim_RV32 tests/arch-test-target/sail_cSim/riscv_sim_RV32
	$(Q)python3 -B $(RISCV_TARGET)/setup.py --riscv_device=$(RISCV_DEVICE)
	$(Q)riscof run --work-dir=$(WORK) \
			--config=$(RISCV_TARGET)/config.ini \
			--suite=$(ARCH_TEST_SUITE) \
			--env=$(ARCH_TEST_DIR)/riscv-test-suite/env
