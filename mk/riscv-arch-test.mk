ARCH_TEST_DIR ?= tests/riscv-arch-test
ARCH_TEST_BUILD := $(ARCH_TEST_DIR)/Makefile
export RISCV_TARGET := tests/arch-test-target
export RISCV_PREFIX ?= $(CROSS_COMPILE)
export TARGETDIR := $(shell pwd)
export XLEN := 32
export JOBS ?= -j
export WORK := $(TARGETDIR)/build/arch-test

$(ARCH_TEST_BUILD):
	git submodule update --init $(dir $@)

arch-test: $(BIN) $(ARCH_TEST_BUILD)
ifndef CROSS_COMPILE
	$(error GNU Toolchain for RISC-V is required. Please check package installation)
endif
	$(Q)$(MAKE) --quiet -C $(ARCH_TEST_DIR) clean
	$(Q)$(MAKE) --quiet -C $(ARCH_TEST_DIR)
