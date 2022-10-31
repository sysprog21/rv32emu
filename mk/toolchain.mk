CC_IS_CLANG :=
CC_IS_GCC :=
ifneq ($(shell $(CC) --version | head -n 1 | grep clang),)
    CC_IS_CLANG := 1
else
    ifneq ($(shell $(CC) --version | grep "Free Software Foundation"),)
        CC_IS_GCC := 1
    endif
endif

# Supported GNU Toolchain for RISC-V
TOOLCHAIN_LIST := riscv-none-elf-      \
		  riscv32-unknown-elf- \
		  riscv64-unknown-elf- \
		  riscv-none-embed-

define check-cross-tools
$(shell which $(1)gcc >/dev/null &&
        which $(1)cpp >/dev/null &&
        echo | $(1)cpp -dM - | grep __riscv >/dev/null &&
        echo "$(1) ")
endef

# TODO: support clang/llvm based cross compilers
# TODO: support native RISC-V compilers
CROSS_COMPILE ?= $(word 1,$(foreach prefix,$(TOOLCHAIN_LIST),$(call check-cross-tools, $(prefix))))
ifeq ($(CROSS_COMPILE),)
$(warning GNU Toolchain for RISC-V is required to build architecture tests. Please check package installation)
endif

export CROSS_COMPILE
