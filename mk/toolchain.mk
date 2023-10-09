CC_IS_CLANG :=
CC_IS_GCC :=
ifneq ($(shell $(CC) --version | head -n 1 | grep clang),)
    CC_IS_CLANG := 1
else
    ifneq ($(shell $(CC) --version | grep "Free Software Foundation"),)
        CC_IS_GCC := 1
    endif
endif

CFLAGS_NO_CET :=
processor := $(shell uname -m)
ifeq ($(processor),$(filter $(processor),i386 x86_64))
    # GCC and Clang can generate support code for Intel's Control-flow
    # Enforcement Technology (CET) through this compiler flag:
    # -fcf-protection=[full]
    CFLAGS_NO_CET := -fcf-protection=none
endif

# As of Xcode 15, linker warnings are emitted if duplicate '-l' options are
# present. Until such linkopts can be deduped by the build system, we disable
# these warnings.
ifeq ($(UNAME_S),Darwin)
    ifneq ($(shell ld -version_details | cut -f2 -d: | grep 15.0.0),)
        LDFLAGS += -Wl,-no_warn_duplicate_libraries
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

export CROSS_COMPILE
