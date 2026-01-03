# Unit tests for rv32emu components
#
# Uses test-framework templates from mk/common.mk

ifndef _MK_TESTS_INCLUDED
_MK_TESTS_INCLUDED := 1

# Test Definitions using Templates

# Cache test: tests LFU cache implementation
# Extra subdir needed for lfu outputs
$(eval $(call test-framework,cache,test-cache.o,$(OUT)/cache.o $(OUT)/mpool.o,$(OUT)/cache/lfu))

# Map test: tests red-black tree map implementation
$(eval $(call test-framework,map,test-map.o mt19937.o,$(OUT)/map.o,))

# Path test: tests path utility functions
$(eval $(call test-framework,path,test-path.o,$(OUT)/utils.o,))

# Test Runners

# Cache test uses file comparison (input -> output -> compare with expected)
$(eval $(call run-test-compare,cache,cache-new cache-put cache-get cache-replace))

# Map and path tests use simple exit code checking
$(eval $(call run-test-simple,map))
$(eval $(call run-test-simple,path))

# Main Test Target

tests: run-test-cache run-test-map run-test-path

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

check-hello: $(BIN)
	$(call check-test, , $(OUT)/hello.elf, hello.elf, uniq,$(EXPECTED_hello))

# Per-ELF check targets for parallelism (supports make -j)
define make-check-target
check-$(1): $(BIN) artifact
	$$(call check-test, , $$(OUT)/riscv32/$(1), $(1), uniq,$$(EXPECTED_$(1)))
endef
$(foreach e,$(CHECK_ELF_FILES),$(eval $(call make-check-target,$(e))))

CHECK_TARGETS := check-hello $(addprefix check-,$(CHECK_ELF_FILES))
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

.PHONY: tests run-test-cache run-test-map run-test-path
.PHONY: check $(CHECK_TARGETS) misalign misalign-in-blk-emu mmu-test

endif # _MK_TESTS_INCLUDED

