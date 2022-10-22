CC_IS_CLANG :=
CC_IS_GCC :=
ifneq ($(shell $(CC) --version | head -n 1 | grep clang),)
    CC_IS_CLANG := 1
else
    ifneq ($(shell $(CC) --version | grep "Free Software Foundation"),)
        CC_IS_GCC := 1
    endif
endif

# Validate GNU Toolchain for RISC-V
TOOLCHAIN_LIST := riscv-none-elf-      \
		  riscv32-unknown-elf- \
		  riscv64-unknown-elf- \
		  riscv-none-embed-

# TODO: add support to clang/llvm based cross compilers
VALID_TOOLCHAIN := $(foreach toolchain,$(TOOLCHAIN_LIST),     \
		   $(shell which $(toolchain)gcc > /dev/null) \
		   $(if $(filter 0,$(.SHELLSTATUS)),$(toolchain)))

# Get the first element in valid toolchain list
CROSS_COMPILE ?= $(word 1,$(VALID_TOOLCHAIN))
ifeq ($(CROSS_COMPILE),)
$(warning GNU Toolchain for RISC-V is required. Please check package installation)
endif

export CROSS_COMPILE
