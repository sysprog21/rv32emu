# ACT4-based RISC-V architectural tests.

ifndef _MK_ACT_INCLUDED
_MK_ACT_INCLUDED := 1

ACT_DIR ?= tests/riscv-act
ACT_RV32EMU_CONFIG ?= tests/act-rv32emu/rv32emu-rv32imafc/test_config.yaml
ACT_WORKDIR ?= $(OUT)/act
ACT_EXTENSIONS ?= I,M,F,Zicsr,Zicntr,Zifencei,Zca,Zcf,Misalign,MisalignZca,Zaamo
ACT_SAIL_BIN_DIR ?= $(OUT)/act-tools/sail-riscv-Linux-x86_64/bin

act-user-elfs:
	$(Q)git submodule update --init $(ACT_DIR)
	$(Q)PATH="$(abspath $(ACT_SAIL_BIN_DIR)):$$PATH" $(MAKE) -C $(ACT_DIR) \
		CONFIG_FILES=$(abspath $(ACT_RV32EMU_CONFIG)) \
		WORKDIR=$(abspath $(ACT_WORKDIR)) \
		EXTENSIONS=$(ACT_EXTENSIONS)

act-user-run: $(BIN) act-user-elfs
ifeq ($(CONFIG_SYSTEM),y)
	$(Q)echo "act-user-run requires a user-mode build. Run 'make cleanconfig && make ci_defconfig && make' first." >&2
	$(Q)exit 1
endif
ifneq ($(CONFIG_ARCH_TEST),y)
	$(Q)echo "act-user-run requires CONFIG_ARCH_TEST=y so rv32emu can detect tohost writes. Run 'make cleanconfig && make ci_defconfig && make' first." >&2
	$(Q)exit 1
endif
	$(Q)tools/run-act-elfs.sh $(BIN) $(ACT_WORKDIR)/rv32emu-rv32imafc/elfs $(ACT_EXTENSIONS)

.PHONY: act-user-elfs act-user-run

endif # _MK_ACT_INCLUDED
