ARCH_TEST_DIR ?= tests/riscv-arch-test
export RISCV_TARGET := tests/arch-test-target
export TARGETDIR := $(shell pwd)
export WORK := $(TARGETDIR)/build/arch-test
export RISCV_DEVICE ?= IMCZicsrZifencei

arch-test: $(BIN)
	git submodule update --init $(dir $(ARCH_TEST_DIR))

	$(Q)python3 -B $(RISCV_TARGET)/setup.py --riscv_device=$(RISCV_DEVICE)

	$(Q)riscof run --work-dir=$(WORK) \
			--config=$(RISCV_TARGET)/config.ini \
			--suite=$(ARCH_TEST_DIR)/riscv-test-suite \
	   		--env=$(ARCH_TEST_DIR)/riscv-test-suite/env
