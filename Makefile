include mk/common.mk
include mk/toolchain.mk

OUT ?= build
BIN := $(OUT)/rv32emu

CFLAGS = -std=gnu99 -O2 -Wall -Wextra
CFLAGS += -Wno-unused-label
CFLAGS += -include src/common.h

# Set the default stack pointer
CFLAGS += -D DEFAULT_STACK_ADDR=0xFFFFF000

OBJS_EXT :=

# Control and Status Register (CSR)
ENABLE_Zicsr ?= 1
$(call set-feature, Zicsr)

# Instruction-Fetch Fence
ENABLE_Zifencei ?= 1
$(call set-feature, Zifencei)

# Integer Multiplication and Division instructions
ENABLE_EXT_M ?= 1
$(call set-feature, EXT_M)

# Atomic Instructions
ENABLE_EXT_A ?= 1
$(call set-feature, EXT_A)

# Compressed extension instructions
ENABLE_EXT_C ?= 1
$(call set-feature, EXT_C)

# Single-precision floating point instructions
ENABLE_EXT_F ?= 1
$(call set-feature, EXT_F)
ifeq ($(call has, EXT_F), 1)
LDFLAGS += -lm
endif

# Enable adaptive replacement cache policy, default is LRU 
ENABLE_ARC ?= 0
$(call set-feature, ARC)

# Experimental SDL oriented system calls
ENABLE_SDL ?= 1
ifeq ($(call has, SDL), 1)
ifeq (, $(shell which sdl2-config))
$(warning No sdl2-config in $$PATH. Check SDL2 installation in advance)
override ENABLE_SDL := 0
endif
endif
$(call set-feature, SDL)
ifeq ($(call has, SDL), 1)
OBJS_EXT += syscall_sdl.o
$(OUT)/syscall_sdl.o: CFLAGS += $(shell sdl2-config --cflags)
LDFLAGS += $(shell sdl2-config --libs)
endif

ENABLE_GDBSTUB ?= 1
$(call set-feature, GDBSTUB)
ifeq ($(call has, GDBSTUB), 1)
GDBSTUB_OUT = $(abspath $(OUT)/mini-gdbstub)
GDBSTUB_COMM = 127.0.0.1:1234
src/mini-gdbstub/Makefile:
	git submodule update --init $(dir $@)
GDBSTUB_LIB := $(GDBSTUB_OUT)/libgdbstub.a
$(GDBSTUB_LIB): src/mini-gdbstub/Makefile
	$(MAKE) -C $(dir $<) O=$(dir $@)
# FIXME: track gdbstub dependency properly
$(OUT)/decode.o: $(GDBSTUB_LIB)
OBJS_EXT += gdbstub.o breakpoint.o
CFLAGS += -D'GDBSTUB_COMM="$(GDBSTUB_COMM)"'
LDFLAGS += $(GDBSTUB_LIB) -lpthread
gdbstub-test: $(BIN)
	$(Q).ci/gdbstub-test.sh && $(call notice, [OK])
endif

# For tail-call elimination, we need a specific set of build flags applied.
# FIXME: On macOS + Apple Silicon, -fno-stack-protector might have a negative impact.
$(OUT)/emulate.o: CFLAGS += -fomit-frame-pointer -fno-stack-check -fno-stack-protector

# Clear the .DEFAULT_GOAL special variable, so that the following turns
# to the first target after .DEFAULT_GOAL is not set.
.DEFAULT_GOAL :=

all: $(BIN)

OBJS := \
	map.o \
	utils.o \
	decode.o \
	io.o \
	syscall.o \
	emulate.o \
	riscv.o \
	elf.o \
	cache.o \
	mpool.o \
	$(OBJS_EXT) \
	main.o

OBJS := $(addprefix $(OUT)/, $(OBJS))
deps := $(OBJS:%.o=%.o.d)

$(OUT)/%.o: src/%.c
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) -c -MMD -MF $@.d $<

$(BIN): $(OBJS)
	$(VECHO) "  LD\t$@\n"
	$(Q)$(CC) -o $@ $^ $(LDFLAGS)

# RISC-V Architecture Tests
include mk/riscv-arch-test.mk
include mk/tests.mk

CHECK_ELF_FILES := \
	hello \
	puzzle \
	pi

EXPECTED_hello = Hello World!
EXPECTED_puzzle = success in 2005 trials
EXPECTED_pi = 3.141592653589793238462643383279502884197169399375105820974944592307816406286208998628034825342117067982148086

check: $(BIN)
	$(Q)$(foreach e,$(CHECK_ELF_FILES),\
	    $(PRINTF) "Running $(e).elf ... "; \
	    if [ "$(shell $(BIN) $(OUT)/$(e).elf | uniq)" = "$(strip $(EXPECTED_$(e))) inferior exit code 0" ]; then \
	    $(call notice, [OK]); \
	    else \
	    $(PRINTF) "Failed.\n"; \
	    exit 1; \
	    fi; \
	)

EXPECTED_aes = Dec 15 2022 16:35:12 Test results AES-128 ECB encryption: PASSED! AES-128 ECB decryption: PASSED! AES-128 CBC encryption: PASSED! AES-128 CBC decryption: PASSED! AES-128 CFB encryption: PASSED! AES-128 CFB decryption: PASSED! AES-128 OFB encryption: PASSED! AES-128 OFB decryption: PASSED! AES-128 CTR encryption: PASSED! AES-128 CTR decryption: PASSED! AES-128 XTS encryption: PASSED! AES-128 XTS decryption: PASSED! AES-128 validate CMAC : PASSED! AES-128 Poly-1305 mac : PASSED! AES-128 GCM encryption: PASSED! AES-128 GCM decryption: PASSED! AES-128 CCM encryption: PASSED! AES-128 CCM decryption: PASSED! AES-128 OCB encryption: PASSED! AES-128 OCB decryption: PASSED! AES-128 SIV encryption: PASSED! AES-128 SIV decryption: PASSED! AES-128 GCMSIV encrypt: PASSED! AES-128 GCMSIV decrypt: PASSED! AES-128 EAX encryption: PASSED! AES-128 EAX decryption: PASSED! AES-128 key wrapping  : PASSED! AES-128 key unwrapping: PASSED! AES-128 FF1 encryption: PASSED! AES-128 FPE decryption: PASSED! +-> Let's do some extra tests AES-128 OCB encryption: PASSED! AES-128 OCB decryption: PASSED! AES-128 GCMSIV encrypt: PASSED! AES-128 GCMSIV decrypt: PASSED! AES-128 GCMSIV encrypt: PASSED! AES-128 GCMSIV decrypt: PASSED! AES-128 SIV encryption: PASSED! AES-128 SIV decryption: PASSED! AES-128 SIV encryption: PASSED! AES-128 SIV decryption: PASSED! AES-128 EAX encryption: PASSED! AES-128 EAX decryption: PASSED! AES-128 EAX encryption: PASSED! AES-128 EAX decryption: PASSED! AES-128 Poly-1305 mac : PASSED! inferior exit code 0
misalign: $(BIN)
	$(Q)$(PRINTF) "Running aes.elf ... "; 
	$(Q)if [ "$(shell $(BIN) --misalign $(OUT)/aes.elf | uniq)" = "$(EXPECTED_aes)" ]; then \
        $(call notice, [OK]); \
	else	\
        $(PRINTF) "Failed.\n"; \
    fi

include mk/external.mk

# Non-trivial demonstration programs
ifeq ($(call has, SDL), 1)
doom: $(BIN) $(DOOM_DATA)
	(cd $(OUT); ../$(BIN) doom.elf)
ifeq ($(call has, EXT_F), 1)
quake: $(BIN) $(QUAKE_DATA)
	(cd $(OUT); ../$(BIN) quake.elf)
endif
endif

clean:
	$(RM) $(BIN) $(OBJS) $(deps) $(CACHE_OUT)
distclean: clean
	-$(RM) $(DOOM_DATA) $(QUAKE_DATA)
	$(RM) -r $(OUT)/id1
	$(RM) *.zip
	$(RM) -r $(OUT)/mini-gdbstub

-include $(deps)
