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
CROSS_COMPILE ?= riscv32-unknown-elf-
RV32_CC = $(CROSS_COMPILE)gcc
RV32_CC := $(shell which $(RV32_CC))
ifndef RV32_CC
    # xPack GNU RISC-V Embedded GCC
    CROSS_COMPILE = riscv-none-elf-
    RV32_CC = $(CROSS_COMPILE)gcc
    RV32_CC := $(shell which $(RV32_CC))
    ifndef RV32_CC
        # DEPRECATED: Replaced by xpack-dev-tools/riscv-none-elf-gcc-xpack
        CROSS_COMPILE = riscv-none-embed-
        RV32_CC = $(CROSS_COMPILE)gcc
        RV32_CC := $(shell which $(RV32_CC))
        ifndef RV32_CC
        $(warning No GNU Toolchain for RISC-V found.)
        CROSS_COMPILE :=
        endif
    endif
endif

export CROSS_COMPILE
