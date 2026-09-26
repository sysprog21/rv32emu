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

# ELF test: rejects malformed ELF input without reading outside the file
$(eval $(call test-framework,elf,test-elf.o,$(OUT)/elf.o $(OUT)/io.o $(OUT)/map.o $(OUT)/utils.o $(OUT)/log.o,))

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

# Main Test Target

tests: run-test-cache run-test-block-edge run-test-map run-test-path run-test-elf run-test-io run-test-decode run-test-set

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
EXPECTED_jit-misalign = JIT misaligned accesses OK
EXPECTED_jit-memory-address = JIT memory address OK
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
GUEST_ASM_ZICSR_WORKS := $(call guest-asm-arch-works,rv32i_zicsr)
GUEST_ASM_C_ZICSR_WORKS := $(call guest-asm-arch-works,rv32ic_zicsr)

ifeq ($(GUEST_ASM_WORKS)$(RUN_USER_ELF),yy)
# Block-cache replacement is exercised by every execution mode.
GUEST_ASM_CHECK_TARGETS := check-block-eviction
# LR/SC runs in the interpreter under every configuration, since the pair is
# marked untranslatable, so this needs only the A extension.
ifeq ($(CONFIG_EXT_A)$(GUEST_ASM_A_WORKS),yy)
GUEST_ASM_CHECK_TARGETS += check-lrsc
endif
# The jit-* programs assert tier-1 code generation, so they prove nothing when
# the emulator under test has no JIT. Report them as skipped rather than
# passing them through the interpreter and reporting green.
ifeq ($(CONFIG_JIT),y)
GUEST_ASM_CHECK_TARGETS += check-jit-alu-alias check-jit-indirect-targets
GUEST_ASM_CHECK_TARGETS += check-jit-memory-address
ifeq ($(CONFIG_EXT_C)$(GUEST_ASM_C_WORKS),yy)
GUEST_ASM_CHECK_TARGETS += check-jit-memory-address-rvc
endif
ifeq ($(CONFIG_EXT_M)$(GUEST_ASM_M_WORKS),yy)
GUEST_ASM_CHECK_TARGETS += check-jit-identity-normalize check-jit-signed-div \
	check-jit-interp-diff
ifeq ($(CONFIG_EXT_C)$(GUEST_ASM_MC_WORKS),yy)
GUEST_ASM_CHECK_TARGETS += check-jit-interp-diff-rvc
endif
endif
# check-jit-misalign installs a machine-mode trap vector, so it needs Zicsr,
# and a user-mode emulator: system builds run the program in supervisor mode.
ifneq ($(CONFIG_SYSTEM),y)
ifeq ($(CONFIG_Zicsr)$(GUEST_ASM_ZICSR_WORKS),yy)
GUEST_ASM_CHECK_TARGETS += check-jit-misalign
ifeq ($(CONFIG_EXT_C)$(GUEST_ASM_C_ZICSR_WORKS),yy)
GUEST_ASM_CHECK_TARGETS += check-jit-misalign-rvc
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
# An optional suffix $(3) names a variant built for another ISA.
guest-asm-build = $(CROSS_COMPILE)gcc -march=$(2) -mabi=ilp32 -nostdlib \
	-static -Wl,-e,_start -o $(OUT)/$(1)$(3) tests/$(1).S

define guest-asm-check-target
.PHONY: check-$(1)$(3)
check-$(1)$(3): $$(BIN) tests/$(1).S | $$(OUT)
	$$(Q)$$(call guest-asm-build,$(1),$(2),$(3))
	$$(call check-test, , $$(OUT)/$(1)$(3), $(1)$(3), tail -n 1,$$(EXPECTED_$(1)))
endef

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
$(eval $(call guest-asm-check-target,jit-memory-address,rv32ic,-rvc))

$(eval $(call guest-asm-check-target,lrsc,rv32ia))
$(eval $(call guest-asm-check-target,jit-alu-alias,rv32i))
# Guard emission additionally requires JIT_INDIRECT_TARGETS, which is off in
# tiered builds; the program still covers history recording and chaining.
$(eval $(call guest-asm-check-target,jit-indirect-targets,rv32i))
$(eval $(call guest-asm-check-target,jit-identity-normalize,rv32im))
$(eval $(call guest-asm-check-target,jit-signed-div,rv32im))
$(eval $(call guest-asm-check-target,jit-interp-diff,rv32im))
$(eval $(call guest-asm-check-target,jit-misalign,rv32i_zicsr))

# The same programs assembled with compressed encodings must agree, which is
# what exercises the compressed load/store generators. In jit-misalign, the
# misaligned bases in s0 and s1 turn the accesses through them into C.LW/C.SW.
$(eval $(call guest-asm-check-target,jit-interp-diff,rv32imc,-rvc))
$(eval $(call guest-asm-check-target,jit-misalign,rv32ic_zicsr,-rvc))

# check-trace-match builds and runs a host program, so it is independent of
# whether the emulator can load a user ELF. Everything else here is a guest
# ELF and belongs behind the same gate as the regressions above.
CHECK_TARGETS := check-trace-match
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
check: $(CHECK_TARGETS)

# System tests
EXPECTED_aes_sha1 = 89169ec034bec1c6bb2c556b26728a736d350ca3  -
misalign: $(BIN) artifact
	$(call check-test, -m, $(OUT)/riscv32/uaes, uaes.elf, $(SHA1SUM),$(EXPECTED_aes_sha1))

EXPECTED_misalign = MISALIGNED INSTRUCTION FETCH TEST PASSED!
misalign-in-blk-emu: $(BIN)
	$(call check-test, , tests/system/alignment/misalign.elf, misalign.elf, tail -n 1,$(EXPECTED_misalign))

EXPECTED_mmu = Store page fault test passed!
mmu-test: $(BIN)
	$(call check-test, , tests/system/mmu/vm.elf, vm.elf, tail -n 1,$(EXPECTED_mmu))

.PHONY: tests run-test-cache run-test-block-edge run-test-map run-test-path \
	run-test-elf run-test-io run-test-decode run-test-set
.PHONY: check $(CHECK_TARGETS) misalign misalign-in-blk-emu mmu-test

endif # _MK_TESTS_INCLUDED
