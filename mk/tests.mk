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
# The block edge helpers are compiled out unless JIT and block chaining are on.
$(OUT)/block-edge/test-block-edge.o: $(EFFECTIVE_CONFIG_STAMP)

# Map test: tests red-black tree map implementation
$(eval $(call test-framework,map,test-map.o mt19937.o,$(OUT)/map.o,))

# Path test: tests path utility functions
$(eval $(call test-framework,path,test-path.o,$(OUT)/utils.o,))

# Runtime loop tracking: generations, collisions, and capacity limits
$(eval $(call test-framework,loop-tracker,test-loop-tracker.o,$(OUT)/utils.o,))
# The tracker layout depends on SYSTEM/JIT through rv_hash_key_t.
$(OUT)/loop-tracker/test-loop-tracker.o: $(EFFECTIVE_CONFIG_STAMP)

# IO test: guest memory accessors reject out-of-range addresses
$(eval $(call test-framework,io,test-io.o,$(OUT)/io.o $(OUT)/log.o,))

# Decode test: reserved encodings and JIT indirect-target history updates
$(eval $(call test-framework,decode,test-decode.o,$(OUT)/decode.o,))
# The branch-history assertions depend on JIT and SYSTEM through decode.h.
$(OUT)/decode/test-decode.o: $(EFFECTIVE_CONFIG_STAMP)

# ELF test: rejects malformed ELF input without reading outside the file
$(eval $(call test-framework,elf,test-elf.o,$(OUT)/elf.o $(OUT)/io.o $(OUT)/map.o $(OUT)/utils.o $(OUT)/log.o,))

# Test Runners

# Cache test uses file comparison (input -> output -> compare with expected)
$(eval $(call run-test-compare,cache,cache-new cache-put cache-get cache-replace))
$(eval $(call run-test-simple,block-edge))

# Map and path tests use simple exit code checking
$(eval $(call run-test-simple,map))
$(eval $(call run-test-simple,path))
$(eval $(call run-test-simple,loop-tracker))
$(eval $(call run-test-simple,elf))
$(eval $(call run-test-simple,io))
$(eval $(call run-test-simple,decode))

# Main Test Target

tests: run-test-cache run-test-block-edge run-test-map run-test-path run-test-elf run-test-io run-test-decode run-test-loop-tracker

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
ifneq ($(CROSS_COMPILE),)
ifneq ($(CONFIG_RV32E),y)
ifneq ($(filter check,$(MAKECMDGOALS)),)
# Freestanding regressions do not require the rv32 libc multilib.
GUEST_ASM_WORKS := $(shell printf '' | $(CROSS_COMPILE)gcc $(GUEST_CFLAGS) \
	-nostdlib -static -Wl,-e,0 -x assembler -o /dev/null - \
	>/dev/null 2>&1 && echo y)
ifeq ($(GUEST_ASM_WORKS),y)
# Block-cache replacement is exercised by every execution mode.
GUEST_ASM_CHECK_TARGETS := check-block-eviction
# The jit-* programs assert tier-1 code generation, so they prove nothing when
# the emulator under test has no JIT. Report them as skipped rather than
# passing them through the interpreter and reporting green.
ifeq ($(CONFIG_JIT),y)
GUEST_ASM_CHECK_TARGETS += check-jit-alu-alias check-jit-memory-address \
	check-jit-indirect-targets
ifeq ($(CONFIG_EXT_M),y)
GUEST_ASM_CHECK_TARGETS += check-jit-identity-normalize
endif
else
$(info Skipping JIT codegen regressions: CONFIG_JIT is not enabled)
endif
endif
# An auto-detected toolchain may lack an rv32i/ilp32 multilib; probe it so such
# a toolchain skips these programs instead of failing make check.
GUEST_CC_WORKS := $(shell printf 'int main(void) { return 0; }\n' | \
	$(CROSS_COMPILE)gcc $(GUEST_CFLAGS) -x c -o /dev/null - >/dev/null 2>&1 && \
	echo y)
ifeq ($(GUEST_CC_WORKS),y)
GUEST_CHECK_TARGETS := $(addprefix check-,$(GUEST_CHECKS))
else
$(info Skipping guest regression programs: $(CROSS_COMPILE)gcc cannot build $(GUEST_CFLAGS) programs)
endif
endif
endif
endif

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

.PHONY: check-jit-alu-alias
check-jit-alu-alias: $(BIN) tests/jit-alu-alias.S | $(OUT)
	$(Q)$(CROSS_COMPILE)gcc $(GUEST_CFLAGS) -nostdlib -static -Wl,-e,_start \
	    -o $(OUT)/jit-alu-alias tests/jit-alu-alias.S
	$(Q)output="$$($(BIN) -q $(OUT)/jit-alu-alias)" || exit 1; \
	    test "$$output" = "JIT ALU aliases OK"

# Exercise base/destination aliases and signed offsets with and without RVC.
.PHONY: check-jit-memory-address
check-jit-memory-address: $(BIN) tests/jit-memory-address.S | $(OUT)
	$(Q)for arch in rv32i $(if $(filter y,$(CONFIG_EXT_C)),rv32ic); do \
	    $(CROSS_COMPILE)gcc -march=$$arch -mabi=ilp32 -nostdlib -static \
	        -Wl,-e,_start -o $(OUT)/jit-memory-address-$$arch \
	        tests/jit-memory-address.S || exit 1; \
	    output="$$($(BIN) -q $(OUT)/jit-memory-address-$$arch)" || exit 1; \
	    test "$$output" = "JIT memory address OK" || exit 1; \
	done

.PHONY: check-jit-identity-normalize
check-jit-identity-normalize: $(BIN) tests/jit-identity-normalize.S | $(OUT)
	$(Q)$(CROSS_COMPILE)gcc -march=rv32im -mabi=ilp32 -nostdlib -static \
	    -Wl,-e,_start -o $(OUT)/jit-identity-normalize \
	    tests/jit-identity-normalize.S
	$(Q)$(BIN) -q $(OUT)/jit-identity-normalize

# Guard emission additionally requires JIT_INDIRECT_TARGETS, which is off in
# tiered builds; the program still covers history recording and chaining.
.PHONY: check-jit-indirect-targets
check-jit-indirect-targets: $(BIN) tests/jit-indirect-targets.S | $(OUT)
	$(Q)$(CROSS_COMPILE)gcc $(GUEST_CFLAGS) -nostdlib -static -Wl,-e,_start \
	    -o $(OUT)/jit-indirect-targets tests/jit-indirect-targets.S
	$(Q)output="$$($(BIN) -q $(OUT)/jit-indirect-targets)" || exit 1; \
	    test "$$output" = "JIT indirect targets OK"

CHECK_TARGETS := check-hello $(GUEST_CHECK_TARGETS) $(GUEST_ASM_CHECK_TARGETS) \
	check-trace-match \
	$(addprefix check-,$(CHECK_ELF_FILES))
ifeq ($(CONFIG_EXT_V),y)
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
	run-test-elf run-test-io run-test-decode run-test-loop-tracker
.PHONY: check $(CHECK_TARGETS) misalign misalign-in-blk-emu mmu-test

endif # _MK_TESTS_INCLUDED
