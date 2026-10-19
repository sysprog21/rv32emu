# Unit tests for rv32emu components
#
# Uses test-framework templates from mk/common.mk

ifndef _MK_TESTS_INCLUDED
_MK_TESTS_INCLUDED := 1

# Test Definitions using Templates

# Cache test: tests LFU cache implementation
# Extra subdir needed for lfu outputs
$(eval $(call test-framework,cache,test-cache.o,$(OUT)/cache.o $(OUT)/mpool.o,$(OUT)/cache/lfu))

# Block-edge test: verifies predecessor ownership and target-first unlinking
$(eval $(call test-framework,block-edge,test-block-edge.o,,))

# Map test: tests red-black tree map implementation
$(eval $(call test-framework,map,test-map.o mt19937.o,$(OUT)/map.o,))

# Path test: tests path utility functions
$(eval $(call test-framework,path,test-path.o,$(OUT)/utils.o,))

# Hashed set: generations, collisions, capacity limits, and key zero
$(eval $(call test-framework,set,test-set.o,$(OUT)/utils.o,))

# IO test: guest memory accessors reject out-of-range addresses
$(eval $(call test-framework,io,test-io.o,$(OUT)/io.o $(OUT)/log.o,))

# Decode test: reserved encodings and JIT indirect-target history updates
$(eval $(call test-framework,decode,test-decode.o,$(OUT)/decode.o,))

# Constant propagation test: folded and strength-reduced operations keep their
# results
$(eval $(call test-framework,constopt,test-constopt.o,,))

# ELF test: rejects malformed ELF input without reading outside the file
$(eval $(call test-framework,elf,test-elf.o,$(OUT)/elf.o $(OUT)/io.o $(OUT)/map.o $(OUT)/utils.o $(OUT)/log.o,))

ifeq ($(CONFIG_GOLDFISH_RTC),y)
$(eval $(call test-framework,rtc,test-rtc.o,$(OUT)/devices/rtc.o $(OUT)/log.o,))
$(eval $(call run-test-simple,rtc))
tests: run-test-rtc
endif

# Test Runners

# Cache test uses file comparison (input -> output -> compare with expected)
$(eval $(call run-test-compare,cache,cache-new cache-put cache-get cache-replace))
$(eval $(call run-test-simple,block-edge))

# Map and path tests use simple exit code checking
$(eval $(call run-test-simple,map))
$(eval $(call run-test-simple,path))
$(eval $(call run-test-simple,set))
$(eval $(call run-test-simple,elf))
$(eval $(call run-test-simple,io))
$(eval $(call run-test-simple,decode))
$(eval $(call run-test-simple,constopt))

# Main Test Target

tests: run-test-cache run-test-block-edge run-test-map run-test-path run-test-elf run-test-io run-test-decode run-test-set run-test-constopt

# Integration Tests (run emulator with test programs)

LOG_FILTER := sed -E '/^[0-9]{2}:[0-9]{2}:[0-9]{2} /d'

# check-test(flags, binary, name, filter, expected)
define check-test
$(Q)true; \
$(PRINTF) "Running $(3) ... "; \
OUTPUT_FILE="$$(mktemp)"; \
trap '$(RM) "$$OUTPUT_FILE"' 0; \
if (LC_ALL=C $(BIN) $(1) $(2) > "$$OUTPUT_FILE") && \
   [ "$$(cat "$$OUTPUT_FILE" | $(LOG_FILTER) | $(4))" = "$(5)" ]; then \
    $(call notice, [OK]); \
else \
    $(PRINTF) "Failed.\n"; \
    exit 1; \
fi
endef

# Check test definitions
CHECK_ELF_FILES :=
ifeq ($(CONFIG_EXT_M),y)
CHECK_ELF_FILES += puzzle fcalc pi
endif

EXPECTED_hello = Hello World!
EXPECTED_puzzle = success in 2005 trials
EXPECTED_fcalc = Performed 12 tests, 0 failures, 100% success rate.
EXPECTED_pi = 3.141592653589793238462643383279502884197169399375105820974944592307816406286208998628034825342117067982148086
EXPECTED_fused-misalign = fused misalign passed
EXPECTED_block-eviction = block eviction OK
EXPECTED_lrsc = LR/SC reservation OK
EXPECTED_jit-alu-alias = JIT ALU aliases OK
EXPECTED_jit-indirect-targets = JIT indirect targets OK
EXPECTED_jit-identity-normalize = JIT identity normalize OK
EXPECTED_jit-signed-div = JIT signed division OK
EXPECTED_jit-muldiv = JIT multiply/divide OK
EXPECTED_jit-compare = JIT compare OK
EXPECTED_jit-shift = JIT register shifts OK
EXPECTED_jit-spill-loop = JIT spill loop OK
EXPECTED_jit-direct-successor = JIT direct successor OK
EXPECTED_jit-carry = JIT carry OK
EXPECTED_jit-branch-successor = JIT branch successor OK
EXPECTED_jit-region-traps = JIT region traps OK
EXPECTED_jit-bit-loop = JIT bit loop OK
EXPECTED_jit-scaled-add = JIT scaled add OK
EXPECTED_jit-mul = JIT multiply OK
EXPECTED_const-arith = constant arithmetic OK
EXPECTED_const-shift = constant shifts OK
EXPECTED_const-operand = constant operands OK
EXPECTED_const-coperand = compressed constant operands OK
EXPECTED_amo-align = AMO alignment OK
EXPECTED_jit-misalign = JIT misaligned accesses OK
EXPECTED_jit-cycles = JIT cycle counts OK
EXPECTED_jit-memory-address = JIT memory address OK
EXPECTED_register-loop = register loop OK
EXPECTED_load-x0 = load into x0 OK
EXPECTED_loop-shapes = loop shapes OK
EXPECTED_misalign-page-fault = misaligned page fault OK
EXPECTED_fused-page-fault = fused page fault OK
EXPECTED_amo-page-fault = AMO page fault OK
EXPECTED_illegal-prefix = illegal prefix ordering OK
EXPECTED_load-page-fault = canceled loads preserve registers OK
EXPECTED_jit-fencei = FENCE.I made the rewrite visible OK
EXPECTED_jit-interp-diff = JIT matches the interpreter OK
EXPECTED_syscall-zero-write = zero-length write passed
EXPECTED_trace_match = trace matcher corpus passed

check-hello: $(BIN)
	$(call check-test, , $(OUT)/hello.elf, hello.elf, uniq,$(EXPECTED_hello))

# Per-ELF check targets for parallelism (supports make -j)
define make-check-target
check-$(1): $(BIN) artifact
	$$(call check-test, , $$(OUT)/riscv32/$(1), $(1), uniq,$$(EXPECTED_$(1)))
endef
$(foreach e,$(CHECK_ELF_FILES),$(eval $(call make-check-target,$(e))))

# Guest regression programs are compiled from source, so they need a RISC-V
# cross compiler.  Target the base integer ISA: the same programs must run on
# emulators built with any optional extension disabled.
GUEST_CFLAGS := -march=rv32i -mabi=ilp32
GUEST_CHECKS := fused-misalign syscall-zero-write
GUEST_CHECK_TARGETS :=
GUEST_ASM_CHECK_TARGETS :=
SYSTEM_ASM_CHECK_TARGETS :=
# Every check below runs a user-mode guest ELF, which a system-mode emulator
# can only load with the ELF loader selected; otherwise it expects a kernel
# image and the program never runs.
RUN_USER_ELF :=
ifneq ($(CONFIG_SYSTEM),y)
RUN_USER_ELF := y
else ifeq ($(CONFIG_ELF_LOADER),y)
RUN_USER_ELF := y
endif

ifneq ($(CROSS_COMPILE),)
ifneq ($(CONFIG_RV32E),y)
ifneq ($(filter check,$(MAKECMDGOALS)),)
# Freestanding regressions need no rv32 libc multilib, but they do need the
# assembler and linker to accept each ISA string in use. Probe every one,
# because a toolchain that handles rv32i need not handle rv32imc.
guest-asm-arch-works = $(shell printf '' | $(CROSS_COMPILE)gcc -march=$(1) \
	-mabi=ilp32 -nostdlib -static -Wl,-e,0 -x assembler -o /dev/null - \
	>/dev/null 2>&1 && echo y)
GUEST_ASM_WORKS := $(call guest-asm-arch-works,rv32i)
GUEST_ASM_M_WORKS := $(call guest-asm-arch-works,rv32im)
GUEST_ASM_C_WORKS := $(call guest-asm-arch-works,rv32ic)
GUEST_ASM_MC_WORKS := $(call guest-asm-arch-works,rv32imc)
GUEST_ASM_A_WORKS := $(call guest-asm-arch-works,rv32ia)
GUEST_ASM_A_ZICSR_WORKS := $(call guest-asm-arch-works,rv32ia_zicsr)
GUEST_ASM_ZICSR_WORKS := $(call guest-asm-arch-works,rv32i_zicsr)
GUEST_ASM_M_ZICSR_WORKS := $(call guest-asm-arch-works,rv32im_zicsr)
GUEST_ASM_FENCEI_WORKS := $(call guest-asm-arch-works,rv32i_zifencei)
GUEST_ASM_C_ZICSR_WORKS := $(call guest-asm-arch-works,rv32ic_zicsr)
GUEST_ASM_F_C_ZICSR_WORKS := $(call guest-asm-arch-works,rv32ifc_zicsr)

# A flat supervisor kernel exercises MMIO-mode TLBs without a guest libc.
ifeq ($(CONFIG_SYSTEM),y)
ifneq ($(CONFIG_ELF_LOADER),y)
ifeq ($(CONFIG_Zicsr)$(CONFIG_EXT_A)$(GUEST_ASM_A_ZICSR_WORKS),yyy)
ifneq ($(shell command -v $(CROSS_COMPILE)objcopy 2>/dev/null),)
SYSTEM_ASM_CHECK_TARGETS += check-system-mmu-cache check-system-trap-cycles
else
$(info Skipping system MMU regression: $(CROSS_COMPILE)objcopy is unavailable)
endif
endif
endif
endif

ifeq ($(GUEST_ASM_WORKS)$(RUN_USER_ELF),yy)
# Block-cache replacement is exercised by every execution mode.
GUEST_ASM_CHECK_TARGETS := check-block-eviction
GUEST_ASM_CHECK_TARGETS += check-const-shift check-load-x0
ifneq ($(CONFIG_SYSTEM),y)
GUEST_ASM_CHECK_TARGETS += check-register-loop check-loop-shapes
# Short loops reach T1 through loop detection only where T1 uses packed IR,
# which T2C builds do not.
ifeq ($(CONFIG_JIT),y)
ifeq ($(CONFIG_T2C)$(CONFIG_ARCH_TEST),)
GUEST_ASM_CHECK_TARGETS += check-jit-loop-profile
endif
endif
ifeq ($(CONFIG_Zicsr)$(GUEST_ASM_ZICSR_WORKS),yy)
GUEST_ASM_CHECK_TARGETS += check-register-loop-cycles check-loop-shapes-csr
endif
ifeq ($(CONFIG_EXT_C)$(GUEST_ASM_C_WORKS),yy)
GUEST_ASM_CHECK_TARGETS += check-loop-shapes-rvc
endif
endif
# LR/SC runs in the interpreter under every configuration, since the pair is
# marked untranslatable, so this needs only the A extension.
ifeq ($(CONFIG_EXT_A)$(GUEST_ASM_A_WORKS),yy)
GUEST_ASM_CHECK_TARGETS += check-lrsc
endif
ifeq ($(CONFIG_EXT_A)$(CONFIG_Zicsr)$(GUEST_ASM_A_ZICSR_WORKS),yyy)
GUEST_ASM_CHECK_TARGETS += check-amo-align
endif
ifeq ($(CONFIG_EXT_M)$(GUEST_ASM_M_WORKS),yy)
GUEST_ASM_CHECK_TARGETS += check-const-arith check-const-operand
endif
ifeq ($(CONFIG_EXT_C)$(GUEST_ASM_C_WORKS),yy)
GUEST_ASM_CHECK_TARGETS += check-const-coperand
endif
# A directly loaded system program has no handler for its own page faults.
# fused-page-fault also passes unfused; it exercises fused records only with
# CONFIG_MOP_FUSION.
ifeq ($(CONFIG_SYSTEM)$(CONFIG_ELF_LOADER)$(CONFIG_Zicsr)$(GUEST_ASM_ZICSR_WORKS),yyyy)
GUEST_ASM_CHECK_TARGETS += check-insn-page-fault check-misalign-page-fault \
	check-fused-page-fault
ifeq ($(CONFIG_EXT_A)$(GUEST_ASM_A_ZICSR_WORKS),yy)
GUEST_ASM_CHECK_TARGETS += check-amo-page-fault
endif
GUEST_ASM_CHECK_TARGETS += check-fetch-page-boundary
GUEST_ASM_CHECK_TARGETS += check-load-page-fault check-illegal-prefix
ifeq ($(CONFIG_EXT_C)$(GUEST_ASM_C_ZICSR_WORKS),yy)
GUEST_ASM_CHECK_TARGETS += check-load-page-fault-rvc
endif
ifeq ($(CONFIG_EXT_F)$(CONFIG_EXT_C)$(GUEST_ASM_F_C_ZICSR_WORKS),yyy)
GUEST_ASM_CHECK_TARGETS += check-load-page-fault-fp
endif
endif
# Only system JIT builds honor FENCE.I, and only a directly loaded program can
# rewrite its own code without an operating system.
ifeq ($(CONFIG_SYSTEM)$(CONFIG_JIT)$(CONFIG_ELF_LOADER)$(CONFIG_Zifencei)$(GUEST_ASM_FENCEI_WORKS),yyyyy)
GUEST_ASM_CHECK_TARGETS += check-jit-fencei
endif
# The jit-* programs assert tier-1 code generation, so they prove nothing when
# the emulator under test has no JIT. Report them as skipped rather than
# passing them through the interpreter and reporting green.
ifeq ($(CONFIG_JIT),y)
GUEST_ASM_CHECK_TARGETS += check-jit-alu-alias check-jit-indirect-targets
GUEST_ASM_CHECK_TARGETS += check-jit-memory-address check-jit-compare \
	check-jit-shift check-jit-spill-loop check-jit-direct-successor check-jit-carry \
	check-jit-branch-successor check-jit-spill-loop-region \
	check-jit-bit-loop check-jit-bit-loop-xor check-jit-bit-loop-clear \
	check-jit-scaled-add
ifeq ($(CONFIG_EXT_C)$(GUEST_ASM_C_WORKS),yy)
GUEST_ASM_CHECK_TARGETS += check-jit-memory-address-rvc \
	check-jit-direct-successor-rvc check-jit-branch-successor-rvc
endif
ifeq ($(CONFIG_EXT_M)$(GUEST_ASM_M_WORKS),yy)
GUEST_ASM_CHECK_TARGETS += check-jit-identity-normalize check-jit-signed-div \
	check-jit-interp-diff check-jit-mul check-jit-muldiv \
	check-jit-spill-loop-m check-jit-spill-loop-region-m
ifeq ($(CONFIG_EXT_C)$(GUEST_ASM_MC_WORKS),yy)
GUEST_ASM_CHECK_TARGETS += check-jit-interp-diff-rvc
endif
endif
# The jit-misalign programs need a user-mode emulator: system builds run them
# in supervisor mode, and only user mode lets JIT code skip the alignment
# checks, which the -nocsr variant, using no privileged state, exercises.
ifneq ($(CONFIG_SYSTEM),y)
GUEST_ASM_CHECK_TARGETS += check-jit-misalign-nocsr
ifeq ($(CONFIG_EXT_C)$(GUEST_ASM_C_WORKS),yy)
GUEST_ASM_CHECK_TARGETS += check-jit-misalign-nocsr-rvc
endif
# The full program installs a machine-mode trap vector and -nohandler reads the
# trap CSRs, so both need Zicsr, as does jit-cycles to read the counters.
ifeq ($(CONFIG_Zicsr)$(GUEST_ASM_ZICSR_WORKS),yy)
GUEST_ASM_CHECK_TARGETS += check-jit-misalign check-jit-misalign-nohandler \
	check-jit-cycles check-jit-spill-loop-csr check-jit-direct-successor-csr \
	check-jit-direct-successor-cycles check-jit-branch-successor-cycles \
	check-jit-region-traps check-jit-spill-loop-region-csr \
	check-jit-bit-loop-cycles check-jit-bit-loop-clear-cycles
ifeq ($(CONFIG_EXT_M)$(GUEST_ASM_M_ZICSR_WORKS),yy)
GUEST_ASM_CHECK_TARGETS += check-jit-spill-loop-m-csr \
	check-jit-spill-loop-region-m-csr
endif
ifeq ($(CONFIG_EXT_C)$(GUEST_ASM_C_ZICSR_WORKS),yy)
GUEST_ASM_CHECK_TARGETS += check-jit-misalign-rvc \
	check-jit-direct-successor-rvc-csr check-jit-direct-successor-rvc-cycles \
	check-jit-branch-successor-rvc-cycles check-jit-region-traps-rvc
endif
endif
endif
else
$(info Skipping JIT codegen regressions: CONFIG_JIT is not enabled)
endif
else ifneq ($(RUN_USER_ELF),y)
$(info Skipping guest regressions: system mode without CONFIG_ELF_LOADER cannot run user ELFs)
endif
# An auto-detected toolchain may lack an rv32i/ilp32 multilib; probe it so such
# a toolchain skips these programs instead of failing make check.
GUEST_CC_WORKS := $(shell printf 'int main(void) { return 0; }\n' | \
	$(CROSS_COMPILE)gcc $(GUEST_CFLAGS) -x c -o /dev/null - >/dev/null 2>&1 && \
	echo y)
ifneq ($(GUEST_CC_WORKS),y)
$(info Skipping guest regression programs: $(CROSS_COMPILE)gcc cannot build $(GUEST_CFLAGS) programs)
else ifeq ($(RUN_USER_ELF),y)
GUEST_CHECK_TARGETS := $(addprefix check-,$(GUEST_CHECKS))
endif
endif
endif
endif

# Build a freestanding guest program from tests/$(1).S for ISA $(2) and compare
# its output, so these report like every other check instead of staying silent.
# An optional suffix $(3) names a variant built for another ISA, or with the
# extra assembler flags in $(4).
guest-asm-build = $(CROSS_COMPILE)gcc -march=$(2) -mabi=ilp32 -nostdlib \
	-static -fno-pie -no-pie -Wl,-e,_start $(4) -o $(OUT)/$(1)$(3) tests/$(1).S

define guest-asm-check-target
.PHONY: check-$(1)$(3)
check-$(1)$(3): $$(BIN) tests/$(1).S | $$(OUT)
	$$(Q)$$(call guest-asm-build,$(1),$(2),$(3),$(4))
	$$(call check-test, , $$(OUT)/$(1)$(3), $(1)$(3), tail -n 1,$$(EXPECTED_$(1)))
endef

EXPECTED_system-mmu-cache = SYSTEM MMU cache OK
.PHONY: check-system-mmu-cache
check-system-mmu-cache: $(BIN) tests/system-mmu-cache.S | $(OUT)
	$(Q)$(call guest-asm-build,system-mmu-cache,rv32ia_zicsr,, \
	    -Xlinker -Ttext=0 -Xlinker --build-id=none)
	$(Q)$(CROSS_COMPILE)objcopy -O binary $(OUT)/system-mmu-cache \
	    $(OUT)/system-mmu-cache.bin
	$(call check-test,-k,$(OUT)/system-mmu-cache.bin,system-mmu-cache,tail -n 1,$(EXPECTED_system-mmu-cache))

EXPECTED_system-trap-cycles = SYSTEM trap cycles OK
.PHONY: check-system-trap-cycles
check-system-trap-cycles: $(BIN) tests/system-trap-cycles.S | $(OUT)
	$(Q)$(call guest-asm-build,system-trap-cycles,rv32ia_zicsr,, \
	    -Xlinker -Ttext=0 -Xlinker --build-id=none)
	$(Q)$(CROSS_COMPILE)objcopy -O binary $(OUT)/system-trap-cycles \
	    $(OUT)/system-trap-cycles.bin
	$(call check-test,-k,$(OUT)/system-trap-cycles.bin,system-trap-cycles,tail -n 1,$(EXPECTED_system-trap-cycles))

define guest-check-target
check-$(1): $(BIN) tests/$(1).c
	$$(Q)$$(CROSS_COMPILE)gcc $$(GUEST_CFLAGS) -o $$(OUT)/$(1) tests/$(1).c
	$$(call check-test, , $$(OUT)/$(1), $(1), uniq,$$(EXPECTED_$(1)))
endef
$(foreach t,$(GUEST_CHECKS),$(eval $(call guest-check-target,$(t))))

check-trace-match: $(BIN) tests/trace-match.c src/trace_match.c src/trace_match.h | $(OUT)
	$(Q)$(CC) $(CFLAGS) -o $(OUT)/trace-match tests/trace-match.c \
	    src/trace_match.c $(LDFLAGS)
	$(Q)output="$$($(OUT)/trace-match)"; test "$$output" = "$(EXPECTED_trace_match)"

# Freestanding: no rv32 libc multilib is needed. Also runs in interpreter mode.
.PHONY: check-block-eviction
check-block-eviction: $(BIN) tests/block-eviction.c tests/block-eviction-start.S | $(OUT)
	$(Q)$(CROSS_COMPILE)gcc $(GUEST_CFLAGS) -nostdlib -static \
	    -fno-stack-protector -Wl,-e,_start -o $(OUT)/block-eviction \
	    tests/block-eviction-start.S tests/block-eviction.c
	$(call check-test, , $(OUT)/block-eviction, block-eviction, uniq,$(EXPECTED_block-eviction))

# Exercise base/destination aliases and signed offsets with and without RVC.
$(eval $(call guest-asm-check-target,jit-memory-address,rv32i))
$(eval $(call guest-asm-check-target,jit-compare,rv32i))
$(eval $(call guest-asm-check-target,jit-shift,rv32i))
$(eval $(call guest-asm-check-target,jit-scaled-add,rv32i))
$(eval $(call guest-asm-check-target,jit-bit-loop,rv32i))
$(eval $(call guest-asm-check-target,jit-bit-loop,rv32i_zicsr,-cycles,-DUSE_CYCLES))
$(eval $(call guest-asm-check-target,jit-bit-loop,rv32i_zicsr,-clear-cycles,-DUSE_CYCLES -DBIT_OP=2))
$(eval $(call guest-asm-check-target,jit-bit-loop,rv32i,-xor,-DBIT_OP=1))
$(eval $(call guest-asm-check-target,jit-bit-loop,rv32i,-clear,-DBIT_OP=2))
$(eval $(call guest-asm-check-target,jit-region-traps,rv32i_zicsr))
$(eval $(call guest-asm-check-target,jit-region-traps,rv32ic_zicsr,-rvc,-DUSE_RVC))
$(eval $(call guest-asm-check-target,jit-branch-successor,rv32i))
$(eval $(call guest-asm-check-target,jit-branch-successor,rv32i_zicsr,-cycles,-DCHECK_CYCLES))
$(eval $(call guest-asm-check-target,jit-branch-successor,rv32ic_zicsr,-rvc-cycles,-DUSE_RVC -DCHECK_CYCLES))
$(eval $(call guest-asm-check-target,jit-branch-successor,rv32ic,-rvc,-DUSE_RVC))
$(eval $(call guest-asm-check-target,jit-carry,rv32i))
$(eval $(call guest-asm-check-target,jit-direct-successor,rv32i))
$(eval $(call guest-asm-check-target,jit-direct-successor,rv32i_zicsr,-cycles,-DCHECK_CYCLES))
$(eval $(call guest-asm-check-target,jit-direct-successor,rv32ic_zicsr,-rvc-cycles,-DUSE_RVC -DCHECK_CYCLES))
$(eval $(call guest-asm-check-target,jit-direct-successor,rv32ic,-rvc,-DUSE_RVC))
$(eval $(call guest-asm-check-target,jit-direct-successor,rv32i_zicsr,-csr,-DUSE_CSR))
$(eval $(call guest-asm-check-target,jit-direct-successor,rv32ic_zicsr,-rvc-csr,-DUSE_RVC -DUSE_CSR))
$(eval $(call guest-asm-check-target,jit-spill-loop,rv32i,-region,-DREGION_BRANCH))
$(eval $(call guest-asm-check-target,jit-spill-loop,rv32im,-region-m,-DREGION_BRANCH -DUSE_MULDIV))
$(eval $(call guest-asm-check-target,jit-spill-loop,rv32i_zicsr,-region-csr,-DREGION_BRANCH -DUSE_CSR))
$(eval $(call guest-asm-check-target,jit-spill-loop,rv32im_zicsr,-region-m-csr,-DREGION_BRANCH -DUSE_MULDIV -DUSE_CSR))
$(eval $(call guest-asm-check-target,jit-spill-loop,rv32i))
$(eval $(call guest-asm-check-target,jit-spill-loop,rv32im,-m,-DUSE_MULDIV))
$(eval $(call guest-asm-check-target,jit-spill-loop,rv32i_zicsr,-csr,-DUSE_CSR))
$(eval $(call guest-asm-check-target,jit-spill-loop,rv32im_zicsr,-m-csr,-DUSE_MULDIV -DUSE_CSR))
$(eval $(call guest-asm-check-target,register-loop,rv32i))
$(eval $(call guest-asm-check-target,load-x0,rv32i))
$(eval $(call guest-asm-check-target,register-loop,rv32i_zicsr,-cycles,-DCHECK_CYCLES))
$(eval $(call guest-asm-check-target,loop-shapes,rv32i))
$(eval $(call guest-asm-check-target,loop-shapes,rv32ic,-rvc))
$(eval $(call guest-asm-check-target,loop-shapes,rv32i_zicsr,-csr,-DUSE_CSR))

.PHONY: check-jit-loop-profile
check-jit-loop-profile: $(BIN) tests/jit-loop-profile.S tests/test-jit-loop-profile.py | $(OUT)
	$(Q)$(call guest-asm-build,jit-loop-profile,rv32i)
	$(Q)python3 tests/test-jit-loop-profile.py $(BIN) $(OUT)/jit-loop-profile $(CROSS_COMPILE)nm
$(eval $(call guest-asm-check-target,jit-memory-address,rv32ic,-rvc))

$(eval $(call guest-asm-check-target,lrsc,rv32ia))
.PHONY: check-amo-align
check-amo-align: $(BIN) tests/amo-align.S | $(OUT)
	$(Q)$(call guest-asm-build,amo-align,rv32ia_zicsr,,$(if $(filter y,$(CONFIG_SYSTEM)),-DSYSTEM_MODE))
	$(call check-test, , $(OUT)/amo-align, amo-align, tail -n 1,$(EXPECTED_amo-align))
	$(call check-test, -m, $(OUT)/amo-align, amo-align-relaxed, tail -n 1,$(EXPECTED_amo-align))
$(eval $(call guest-asm-check-target,misalign-page-fault,rv32i_zicsr))
$(eval $(call guest-asm-check-target,fused-page-fault,rv32i_zicsr))
$(eval $(call guest-asm-check-target,amo-page-fault,rv32ia_zicsr))
$(eval $(call guest-asm-check-target,illegal-prefix,rv32i_zicsr))
$(eval $(call guest-asm-check-target,load-page-fault,rv32i_zicsr))
$(eval $(call guest-asm-check-target,load-page-fault,rv32ic_zicsr,-rvc))
$(eval $(call guest-asm-check-target,load-page-fault,rv32ifc_zicsr,-fp))
$(eval $(call guest-asm-check-target,jit-fencei,rv32i_zifencei))

EXPECTED_fetch-page-boundary = fetch fault ordering OK
FETCH_PAGE_LDFLAGS = -Xlinker --section-start -Xlinker .faultpage=0x3ffff8
$(eval $(call guest-asm-check-target,fetch-page-boundary,rv32i_zicsr,,$(FETCH_PAGE_LDFLAGS)))

# The program must stop at its first instruction page fault, reporting it and
# failing, instead of retrying the fetch forever.
.PHONY: check-insn-page-fault
check-insn-page-fault: $(BIN) tests/insn-page-fault.S | $(OUT)
	$(Q)$(call guest-asm-build,insn-page-fault,rv32i_zicsr)
	$(Q)$(PRINTF) "Running insn-page-fault ... "; \
	if output="$$($(BIN) $(OUT)/insn-page-fault 2>&1)"; then \
	    $(PRINTF) "Failed.\n"; exit 1; \
	elif ! echo "$$output" | grep -q "Instruction page fault"; then \
	    $(PRINTF) "Failed.\n"; exit 1; \
	fi; \
	$(call notice, [OK])
$(eval $(call guest-asm-check-target,jit-alu-alias,rv32i))
# Guard emission additionally requires JIT_INDIRECT_TARGETS, which is off in
# tiered builds; the program still covers history recording and chaining.
$(eval $(call guest-asm-check-target,jit-indirect-targets,rv32i))
$(eval $(call guest-asm-check-target,jit-identity-normalize,rv32im))
$(eval $(call guest-asm-check-target,jit-signed-div,rv32im))
$(eval $(call guest-asm-check-target,jit-mul,rv32im))
$(eval $(call guest-asm-check-target,jit-muldiv,rv32im))
$(eval $(call guest-asm-check-target,const-arith,rv32im))
$(eval $(call guest-asm-check-target,const-operand,rv32im))
$(eval $(call guest-asm-check-target,const-coperand,rv32ic))
$(eval $(call guest-asm-check-target,const-shift,rv32i))
$(eval $(call guest-asm-check-target,jit-interp-diff,rv32im))
$(eval $(call guest-asm-check-target,jit-misalign,rv32i_zicsr))

# The same programs assembled with compressed encodings must agree, which is
# what exercises the compressed load/store generators. In jit-misalign, the
# misaligned bases in s0 and s1 turn the accesses through them into C.LW/C.SW.
$(eval $(call guest-asm-check-target,jit-interp-diff,rv32imc,-rvc))
$(eval $(call guest-asm-check-target,jit-misalign,rv32ic_zicsr,-rvc))
$(eval $(call guest-asm-check-target,jit-misalign,rv32i,-nocsr,-DNO_CSR))
$(eval $(call guest-asm-check-target,jit-misalign,rv32i_zicsr,-nohandler,-DNO_HANDLER))
$(eval $(call guest-asm-check-target,jit-cycles,rv32i_zicsr))
$(eval $(call guest-asm-check-target,jit-misalign,rv32ic,-nocsr-rvc,-DNO_CSR))

# Host checks and flat supervisor kernels do not require a user ELF loader.
CHECK_TARGETS := check-trace-match $(SYSTEM_ASM_CHECK_TARGETS)
ifeq ($(RUN_USER_ELF),y)
CHECK_TARGETS += check-hello $(GUEST_CHECK_TARGETS) \
	$(GUEST_ASM_CHECK_TARGETS) $(addprefix check-,$(CHECK_ELF_FILES))
endif
ifeq ($(CONFIG_EXT_V)$(RUN_USER_ELF),yy)
EXPECTED_rvv_smoke = RVV smoke OK
CHECK_TARGETS += check-rvv-smoke

check-rvv-smoke: $(BIN)
	$(Q)$(CROSS_COMPILE)gcc -march=rv32imfv_zicsr -mabi=ilp32f -nostdlib -static \
	    tests/rvv-smoke.S -o $(OUT)/rvv-smoke.elf
	$(call check-test, , $(OUT)/rvv-smoke.elf, rvv-smoke.elf, tail -n 1,$(EXPECTED_rvv_smoke))
endif
ifeq ($(CONFIG_SYSTEM),y)
ifneq ($(CONFIG_ELF_LOADER),y)
ifneq ($(CONFIG_RV32E),y)
ifeq ($(CONFIG_Zicsr),y)
ifeq ($(GUEST_ASM_ZICSR_WORKS),y)
WFI_OBJCOPY_WORKS := $(shell $(CROSS_COMPILE)objcopy --version \
	>/dev/null 2>&1 && echo y)
ifeq ($(WFI_OBJCOPY_WORKS),y)
CHECK_TARGETS += check-load-x0-system
ifneq ($(CONFIG_ROOTFS_EXT4),y)
CHECK_TARGETS += check-wfi-pending
endif
endif
endif
endif
endif
endif
endif
ifeq ($(CONFIG_SYSTEM)$(CONFIG_JIT)$(CONFIG_Zicsr),yyy)
ifneq ($(CONFIG_ELF_LOADER),y)
ifneq ($(CONFIG_T2C),y)
ifneq ($(CONFIG_RV32E),y)
ifeq ($(MEM_START),0)
ifeq ($(GUEST_ASM_ZICSR_WORKS)$(WFI_OBJCOPY_WORKS),yy)
CHECK_TARGETS += check-t1-bare-mmu
endif
endif
endif
endif
endif
endif
check: $(CHECK_TARGETS)

# System tests
EXPECTED_aes_sha1 = 89169ec034bec1c6bb2c556b26728a736d350ca3  -
misalign: $(BIN) artifact
	$(call check-test, -m, $(OUT)/riscv32/uaes, uaes.elf, $(SHA1SUM),$(EXPECTED_aes_sha1))

EXPECTED_misalign = MISALIGNED INSTRUCTION FETCH TEST PASSED!
misalign-in-blk-emu: $(BIN)
	$(call check-test, , tests/system/alignment/misalign.elf, misalign.elf, tail -n 1,$(EXPECTED_misalign))

EXPECTED_mmu = Redirected page fault test passed!
mmu-test: $(BIN)
	$(call check-test, , tests/system/mmu/vm.elf, vm.elf, tail -n 1,$(EXPECTED_mmu))

# A locally enabled pending interrupt must resume WFI when global SIE is clear.
# With no pending interrupt, WFI still advances to the timer compare.
EXPECTED_wfi_pending = WFI pending interrupt wakeup OK
WFI_BIN = $(OUT)/wfi-pending.bin
# This raw guest needs the system emulator without its ext4 rootfs device.
.PHONY: check-wfi-pending
check-wfi-pending: $(BIN) tests/wfi-pending.S | $(OUT)
	$(Q)$(CROSS_COMPILE)gcc -march=rv32i_zicsr -mabi=ilp32 -nostdlib -static \
	    -Wl,-e,_start -Wl,-Ttext=0x0 -o $(OUT)/wfi-pending.elf \
	    tests/wfi-pending.S
	$(Q)$(CROSS_COMPILE)objcopy -O binary $(OUT)/wfi-pending.elf \
	    $(WFI_BIN)
	$(call check-test, -k, $(WFI_BIN), wfi, tail -n 1,$(EXPECTED_wfi_pending))

# Raw system guests exercise cold loads and native MMIO helpers in JIT builds.
EXPECTED_load_x0_system = system loads preserve x0 OK
LOAD_X0_INPUT = $(OUT)/load-x0-system.in
LOAD_X0_FLAGS = -k
ifeq ($(CONFIG_ROOTFS_EXT4),y)
LOAD_X0_ROOTFS = $(OUT)/load-x0-system.img
LOAD_X0_FLAGS = -x vblk:$(LOAD_X0_ROOTFS),rootfs -k
$(LOAD_X0_ROOTFS): | $(OUT)
	$(Q)truncate -s 1M $@
endif
$(LOAD_X0_INPUT): | $(OUT)
	$(Q)printf 'ABCDEFGHIJ' > $@

define load-x0-system-target
check-load-x0-$(1): $(BIN) tests/load-x0-system.S $(LOAD_X0_INPUT) $(LOAD_X0_ROOTFS) | $(OUT)
	$$(Q)$$(CROSS_COMPILE)gcc -march=rv32i -mabi=ilp32 -nostdlib -static \
	    -DLOAD_X0_MODE=$(2) -Wl,-e,_start -Wl,-Ttext=0x0 \
	    -o $$(OUT)/load-x0-$(1).elf tests/load-x0-system.S
	$$(Q)$$(CROSS_COMPILE)objcopy -O binary $$(OUT)/load-x0-$(1).elf \
	    $$(OUT)/load-x0-$(1).bin
	$$(call check-test, $$(LOAD_X0_FLAGS), $$(OUT)/load-x0-$(1).bin < $$(LOAD_X0_INPUT), load-x0-$(1), tail -n 1,$$(EXPECTED_load_x0_system))
.PHONY: check-load-x0-$(1)
endef
$(eval $(call load-x0-system-target,cold,0))
$(eval $(call load-x0-system-target,ram,1))
$(eval $(call load-x0-system-target,mmio,2))
.PHONY: check-load-x0-system
check-load-x0-system: check-load-x0-cold check-load-x0-ram check-load-x0-mmio

# Exercise SYSTEM_MMIO T1 memory access from a raw image. The guest assumes
# RAM starts at zero and tests the configured final valid word.
EXPECTED_t1_bare_mmu = !T1 bare MMU path OK
T1_BARE_MMU_BIN = $(OUT)/t1-bare-mmu.bin
T1_BARE_MMU_DUMMY_ROOTFS = $(OUT)/t1-bare-mmu-rootfs.img
T1_BARE_MMU_ROOTFS_FLAGS = -x vblk:$(T1_BARE_MMU_DUMMY_ROOTFS),rootfs
T1_BARE_MMU_RUN_FLAGS = -k
ifeq ($(CONFIG_ROOTFS_EXT4),y)
T1_BARE_MMU_RUN_FLAGS = $(T1_BARE_MMU_ROOTFS_FLAGS) -k
T1_BARE_MMU_ROOTFS_DEP = $(T1_BARE_MMU_DUMMY_ROOTFS)
endif
$(T1_BARE_MMU_DUMMY_ROOTFS): | $(OUT)
	$(Q)truncate -s 1048576 $@
ifneq ($(filter check-t1-bare-mmu,$(MAKECMDGOALS)),)
ifneq ($(CONFIG_SYSTEM)$(CONFIG_JIT)$(CONFIG_Zicsr),yyy)
$(error check-t1-bare-mmu requires SYSTEM, JIT, and Zicsr)
endif
ifneq ($(CONFIG_ELF_LOADER),)
$(error check-t1-bare-mmu requires ELF_LOADER=n)
endif
ifeq ($(CONFIG_T2C),y)
$(error check-t1-bare-mmu requires T2C=n)
endif
ifeq ($(CONFIG_RV32E),y)
$(error check-t1-bare-mmu requires RV32E=n)
endif
ifneq ($(MEM_START),0)
$(error check-t1-bare-mmu requires MEM_START=0)
endif
endif
.PHONY: check-t1-bare-mmu
check-t1-bare-mmu: $(BIN) tests/t1-bare-mmu.S $(T1_BARE_MMU_ROOTFS_DEP) | $(OUT)
	$(Q)$(CROSS_COMPILE)gcc -march=rv32i_zicsr -mabi=ilp32 -nostdlib -static \
	    -DTEST_RAM_END=0x$(REAL_MEM_SIZE) -Wl,-e,_start -Wl,-Ttext=0x0 \
	    -o $(OUT)/t1-bare-mmu.elf \
	    tests/t1-bare-mmu.S
	$(Q)$(CROSS_COMPILE)objcopy -O binary $(OUT)/t1-bare-mmu.elf \
	    $(T1_BARE_MMU_BIN)
	$(call check-test, $(T1_BARE_MMU_RUN_FLAGS), $(T1_BARE_MMU_BIN), t1-bare-mmu, tail -n 1,$(EXPECTED_t1_bare_mmu))

.PHONY: tests run-test-cache run-test-block-edge run-test-map run-test-path \
	run-test-elf run-test-io run-test-decode run-test-set run-test-constopt
.PHONY: check $(CHECK_TARGETS) misalign misalign-in-blk-emu mmu-test

endif # _MK_TESTS_INCLUDED
